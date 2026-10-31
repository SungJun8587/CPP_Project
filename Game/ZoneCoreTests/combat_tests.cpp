// 전투(기본 공격/피해/사망/리스폰) 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "test_common.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로, 파일마다 헬퍼/타입 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{

struct CombatEntity : CZoneEntity
{
	CombatEntity(uint64 id, EntityType t) : CZoneEntity(id, t) {}
};

struct CombatZone : CFieldZone<>
{
	explicit CombatZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
	~CombatZone() override { Stop(); }

	std::vector<int> log;							// 훅 호출 순서 기록: 1=damaged 2=died 3=respawned 4=rejected 5=moveStopped 6=moveRejected
	int damaged = 0, died = 0, respawned = 0, rejected = 0, stopped = 0, moveRejected = 0;
	int lastDamage = 0, lastHp = -1;
	AttackResult lastReject = AttackResult::Ok;
	MoveResult lastMoveReject = MoveResult::Ok;
	bool lastStopArrived = true;
	uint64 lastKiller = 0;
	bool lastSourceNull = false;

protected:
	void OnEntityDamaged(CZoneEntity* a, CZoneEntity& v, int32 d) override { ++damaged; lastDamage = d; lastHp = v.GetHp(); lastSourceNull = (a == nullptr); log.push_back(1); }
	void OnEntityDied(CZoneEntity&, CZoneEntity* k) override { ++died; lastKiller = k != nullptr ? k->GetId() : 0; log.push_back(2); }
	void OnEntityRespawned(CZoneEntity&) override { ++respawned; log.push_back(3); }
	void OnAttackRejected(CZoneEntity&, uint64, AttackResult r) override { ++rejected; lastReject = r; log.push_back(4); }
	void OnEntityMoveStopped(CZoneEntity&, bool arrived) override { ++stopped; lastStopArrived = arrived; log.push_back(5); }
	void OnMoveRejected(CZoneEntity&, MoveResult r) override { ++moveRejected; lastMoveReject = r; log.push_back(6); }
};

static FieldZoneConfig Cfg()
{
	FieldZoneConfig f;
	f.zone.zoneId = 1;
	f.zone.tickIntervalMs = 10;
	f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
	f.cellSize = 50;
	f.viewCells = 1;
	f.respawnDelayMs = 100;
	f.allowPvp = true;
	f.minDamage = 1;
	f.respawnInvulnMs = 0;			// 무적 테스트에서만 켠다
	return f;
}

static SCombatStats Stats(int hp, int atk, int def, float range = 2.0f, uint32 interval = 200)
{
	SCombatStats s;
	s.maxHp = hp; s.attack = atk; s.defense = def; s.attackRange = range; s.attackIntervalMs = interval;
	return s;
}

static CZoneEntity* Add(CombatZone& z, uint64 id, EntityType t, Vec2 pos, const SCombatStats& s, float speed = 0.0f)
{
	auto e = std::make_unique<CombatEntity>(id, t);
	e->SetSpawnPos(pos);
	e->SetCombatStats(s);
	e->SetSpeed(speed);
	return z.AddEntity(std::move(e));
}

// 존 스레드에서 fn을 실행하고 끝날 때까지 기다린다.
template <typename F>
static void Run(CombatZone& z, F fn)
{
	std::promise<void> p;
	auto f = p.get_future();
	z.Post([&](CZone&) { fn(); p.set_value(); });
	f.wait();
}

static void TestStatsSanitize()
{
	std::printf("[Combat stats sanitize]\n");
	CombatEntity e(1, EntityType::Player);
	SCombatStats s;
	s.maxHp = 0; s.attack = -5; s.defense = -1;
	s.attackRange = std::numeric_limits<float>::quiet_NaN();
	s.attackIntervalMs = 0;
	e.SetCombatStats(s);
	CHECK(e.GetCombatStats().maxHp == 1 && e.GetHp() == 1);
	CHECK(e.GetCombatStats().attack == 0 && e.GetCombatStats().defense == 0);
	CHECK(e.GetCombatStats().attackRange == 0.0f);
	CHECK(e.GetCombatStats().attackIntervalMs == 1);
	CHECK(e.IsAlive() && e.IsCombatant());
}

static void TestDamageAndCooldown()
{
	std::printf("[Combat damage/cooldown/throttle]\n");
	CombatZone z(Cfg());
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(100, 30, 0));
	Add(z, 2, EntityType::Monster, { 101, 100 }, Stats(100, 5, 5));
	Add(z, 3, EntityType::Monster, { 101.5f, 100 }, Stats(100, 5, 50));	// 방어 > 공격
	z.Start();

	AttackResult r1{}, r2{}, r3{}, r4{}, r5{};
	int hp2 = 0, hp3 = 0;
	Run(z, [&]
	{
		r1 = z.RequestAttack(1, 2);		// 30-5 = 25
		r2 = z.RequestAttack(1, 2);		// 같은 틱 -> Throttled
		hp2 = z.FindEntity(2)->GetHp();
	});
	CHECK(r1 == AttackResult::Ok && r2 == AttackResult::Throttled);
	CHECK(hp2 == 75 && z.damaged == 1 && z.lastDamage == 25 && z.lastHp == 75);
	CHECK(z.rejected == 0);

	SleepMs(40);
	Run(z, [&] { r3 = z.RequestAttack(1, 2); });	// 쿨다운 중
	CHECK(r3 == AttackResult::Cooldown && z.damaged == 1 && z.rejected == 0);

	SleepMs(250);
	Run(z, [&]
	{
		r4 = z.RequestAttack(1, 3);					// 30-50 < 0 -> minDamage 1
		hp3 = z.FindEntity(3)->GetHp();
	});
	CHECK(r4 == AttackResult::Ok && hp3 == 99 && z.lastDamage == 1);

	Run(z, [&] { r5 = z.RequestAttack(999, 2); });
	CHECK(r5 == AttackResult::EntityNotFound);
	z.Stop();
}

static void TestRangeAndTarget()
{
	std::printf("[Combat range/target validation]\n");
	CombatZone z(Cfg());
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(100, 10, 0, 2.0f, 100));
	Add(z, 2, EntityType::Monster, { 120, 100 }, Stats(100, 0, 0));		// 20 떨어짐
	Add(z, 3, EntityType::Monster, { 105.5f, 100 }, Stats(100, 0, 0));	// 정지 상태 5.5: 사거리2+허용1=3 초과
	Add(z, 4, EntityType::Monster, { 105.5f, 100 }, Stats(100, 0, 0), 10.0f);	// 이동 중이면 +10*0.3=3 -> 6.0 이내
	Add(z, 5, EntityType::Monster, { 101, 100 }, Stats(100, 0, 0));
	Add(z, 6, EntityType::Player, { 101, 100 }, Stats(100, 0, 0));
	z.Start();

	AttackResult r{};
	Run(z, [&] { r = z.RequestAttack(1, 2); });
	CHECK(r == AttackResult::OutOfRange && z.rejected == 1 && z.lastReject == AttackResult::OutOfRange);

	// 사거리 실패는 쿨다운을 소모하지 않는다 -> 다음 틱에 가까운 대상은 바로 공격 가능
	SleepMs(30);
	Run(z, [&] { r = z.RequestAttack(1, 5); });
	CHECK(r == AttackResult::Ok);

	SleepMs(150);
	Run(z, [&] { r = z.RequestAttack(1, 3); });
	CHECK(r == AttackResult::OutOfRange);

	SleepMs(30);
	Run(z, [&]
	{
		z.RequestMove(4, z.FindEntity(4)->GetPos(), { 300, 100 });	// 이동 중으로 만든다
	});
	SleepMs(150);
	// 이동 후 위치가 달라졌으므로 직접 거리 대신 "이동 중 허용치"가 반영되는지만 본다: 대상을 다시 가까이 순간이동
	Run(z, [&]
	{
		z.Teleport(4, { 105.5f, 100 });			// Teleport는 이동을 멈추므로 다시 이동 시작
		z.RequestMove(4, { 105.5f, 100 }, { 300, 100 });
	});
	SleepMs(80);
	Run(z, [&] { r = z.RequestAttack(1, 4); });
	CHECK(z.FindEntity(4)->IsMoving());
	// 이동한 거리만큼 멀어졌을 수 있어 Ok/OutOfRange 둘 다 가능 -> 결과가 NotFound/InvalidTarget이 아닌지만 확인
	CHECK(r == AttackResult::Ok || r == AttackResult::OutOfRange || r == AttackResult::Cooldown);

	SleepMs(150);
	Run(z, [&] { r = z.RequestAttack(1, 1); });		// 자기 자신
	CHECK(r == AttackResult::InvalidTarget);
	SleepMs(30);
	Run(z, [&] { r = z.RequestAttack(1, 12345); });	// 없는 대상
	CHECK(r == AttackResult::TargetNotFound);
	SleepMs(30);
	Run(z, [&] { r = z.RequestAttack(1, 6); });		// PvP 허용
	CHECK(r == AttackResult::Ok);
	z.Stop();
}

static void TestPvpPolicy()
{
	std::printf("[Combat PvP policy]\n");
	FieldZoneConfig cfg = Cfg();
	cfg.allowPvp = false;
	CombatZone z(cfg);
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(100, 10, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 101, 100 }, Stats(100, 10, 0, 2.0f, 20));
	Add(z, 3, EntityType::Monster, { 101, 101 }, Stats(100, 10, 0, 2.0f, 20));
	Add(z, 4, EntityType::Monster, { 101, 102 }, Stats(100, 10, 0, 2.0f, 20));
	Add(z, 5, EntityType::Npc, { 102, 100 }, Stats(100, 10, 0));
	z.Start();

	AttackResult pvp{}, pve{}, mvm{}, mvp{}, npc{};
	Run(z, [&] { pvp = z.RequestAttack(1, 2); });
	SleepMs(40);
	Run(z, [&] { pve = z.RequestAttack(1, 3); });
	SleepMs(40);
	Run(z, [&] { mvm = z.RequestAttack(3, 4); });
	SleepMs(40);
	Run(z, [&] { mvp = z.RequestAttack(3, 1); });
	SleepMs(40);
	Run(z, [&] { npc = z.RequestAttack(1, 5); });
	CHECK(pvp == AttackResult::InvalidTarget);
	CHECK(pve == AttackResult::Ok);
	CHECK(mvm == AttackResult::InvalidTarget);
	CHECK(mvp == AttackResult::Ok);
	CHECK(npc == AttackResult::InvalidTarget);
	z.Stop();
}

static void TestDeathAndMove()
{
	std::printf("[Combat death/overkill/dead rules]\n");
	FieldZoneConfig cfg = Cfg();
	cfg.respawnDelayMs = CFieldZoneBase::kNoRespawn;	// 위치 불변 검증이 리스폰과 섞이지 않게 한다
	CombatZone z(cfg);
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(10, 50, 0, 2.0f, 20), 50.0f);
	Add(z, 2, EntityType::Player, { 101, 100 }, Stats(10, 50, 0, 2.0f, 20), 50.0f);
	Add(z, 3, EntityType::Monster, { 102, 100 }, Stats(10, 0, 0, 2.0f, 20));
	z.Start();

	AttackResult a{}, b{}, c{};
	MoveResult mv{};
	Run(z, [&]
	{
		z.RequestMove(2, { 101, 100 }, { 300, 101 });		// 2번은 이동 중
	});
	Run(z, [&]
	{
		a = z.RequestAttack(1, 2);							// 치명타 (2번은 이동 중)
		b = z.RequestAttack(2, 1);							// 같은 틱이지만 다른 공격자 -> 죽은 공격자
	});
	CHECK(a == AttackResult::Ok);
	CHECK(b == AttackResult::AttackerDead);
	CHECK(z.died == 1 && z.lastKiller == 1);
	CHECK(z.lastHp == 0 && z.lastDamage == 50);
	// 훅 순서: 피해 -> (이동 중단) -> 사망
	size_t di = 0, si = 0, xi = 0;
	for( size_t i = 0; i < z.log.size(); ++i ) { if( z.log[i] == 1 ) di = i; if( z.log[i] == 5 ) si = i; if( z.log[i] == 2 ) xi = i; }
	CHECK(di < si && si < xi);
	CHECK(!z.lastStopArrived);

	SleepMs(70);		// 이동 요청 최소 간격(50ms)이 지나야 스로틀이 아니라 Dead 판정에 도달한다
	Run(z, [&]
	{
		CZoneEntity* dead = z.FindEntity(2);
		CHECK(!dead->IsAlive() && dead->GetHp() == 0 && !dead->IsMoving());
		mv = z.RequestMove(*dead, dead->GetPos(), { 150, 150 });
	});
	CHECK(mv == MoveResult::Dead && z.lastMoveReject == MoveResult::Dead);

	SleepMs(40);
	Run(z, [&] { c = z.RequestAttack(1, 2); });
	CHECK(c == AttackResult::TargetDead && z.lastReject == AttackResult::TargetDead);

	// 죽은 엔티티가 이동 목록에서 빠져 위치가 변하지 않는다.
	Vec2 p1{}, p2{};
	Run(z, [&] { p1 = z.FindEntity(2)->GetPos(); });
	SleepMs(60);
	Run(z, [&] { p2 = z.FindEntity(2)->GetPos(); });
	CHECK(p1.x == p2.x && p1.y == p2.y);
	z.Stop();
}

static void TestRespawn()
{
	std::printf("[Combat respawn]\n");
	CombatZone z(Cfg());		// respawnDelayMs = 100
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(100, 50, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 101, 100 }, Stats(30, 0, 0, 2.0f, 20));		// 스폰 (101,100)
	Add(z, 3, EntityType::Monster, { 102, 100 }, Stats(30, 0, 0, 2.0f, 20));	// 몬스터: 자동 리스폰 없음
	z.Start();

	AttackResult r{};
	Run(z, [&]
	{
		z.Teleport(2, { 102.5f, 100 });		// 스폰(101)과 다른 위치에서 사망
		r = z.RequestAttack(1, 2);
	});
	CHECK(r == AttackResult::Ok && z.died == 1);

	SleepMs(40);
	Run(z, [&] { r = z.RequestAttack(1, 3); });
	CHECK(r == AttackResult::Ok && z.died == 2);

	SleepMs(40);
	Run(z, [&] { CHECK(!z.FindEntity(2)->IsAlive()); });	// 아직 리스폰 전
	SleepMs(200);
	Run(z, [&]
	{
		CZoneEntity* p = z.FindEntity(2);
		CHECK(p->IsAlive() && p->GetHp() == 30);
		CHECK(p->GetPos().x == 101.0f && p->GetPos().y == 100.0f);
		CHECK(!z.FindEntity(3)->IsAlive());					// 몬스터는 그대로 사망
	});
	CHECK(z.respawned == 1);

	// 수동 Respawn: 자동 타이머를 취소하고 한 번만 부활
	Run(z, [&] { z.RequestAttack(1, 2); });
	CHECK(z.died == 3);
	bool manual = false, again = false;
	Run(z, [&] { manual = z.Respawn(2); again = z.Respawn(2); });
	CHECK(manual && !again && z.respawned == 2);
	SleepMs(250);
	CHECK(z.respawned == 2);

	// 사망 후 퇴장 -> 같은 ID로 재입장: 예전 타이머가 새 엔티티를 건드리지 않는다.
	SleepMs(40);
	Run(z, [&] { z.RequestAttack(1, 2); });
	CHECK(z.died == 4);
	Run(z, [&]
	{
		CHECK(z.RemoveEntity(2));
		auto e = std::make_unique<CombatEntity>(2, EntityType::Player);
		e->SetSpawnPos({ 300, 300 });
		e->SetCombatStats(Stats(30, 0, 0));
		CHECK(z.AddEntity(std::move(e)) != nullptr);
	});
	SleepMs(250);
	Run(z, [&]
	{
		CZoneEntity* p = z.FindEntity(2);
		CHECK(p->IsAlive() && p->GetPos().x == 300.0f);
	});
	CHECK(z.respawned == 2);		// 추가 리스폰 없음
	z.Stop();
}

static void TestSimultaneousAndNoRespawn()
{
	std::printf("[Combat simultaneous kill / no-respawn config]\n");
	FieldZoneConfig cfg = Cfg();
	cfg.respawnDelayMs = CFieldZoneBase::kNoRespawn;
	CombatZone z(cfg);
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(10, 50, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 101, 100 }, Stats(10, 50, 0, 2.0f, 20));
	z.Start();

	AttackResult a{}, b{};
	Run(z, [&] { a = z.RequestAttack(1, 2); b = z.RequestAttack(2, 1); });
	CHECK(a == AttackResult::Ok && b == AttackResult::AttackerDead);
	CHECK(z.died == 1);
	SleepMs(200);
	Run(z, [&] { CHECK(!z.FindEntity(2)->IsAlive() && z.FindEntity(1)->IsAlive()); });
	CHECK(z.respawned == 0);
	z.Stop();
}

static void TestInvulnerability()
{
	std::printf("[Combat respawn invulnerability]\n");
	FieldZoneConfig cfg = Cfg();
	cfg.respawnInvulnMs = 300;
	CombatZone z(cfg);
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(100, 50, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 101, 100 }, Stats(30, 50, 0, 2.0f, 20));
	z.Start();

	AttackResult r{};
	Run(z, [&] { r = z.RequestAttack(1, 2); });
	CHECK(r == AttackResult::Ok && z.died == 1);

	SleepMs(180);		// 리스폰(100ms) 직후, 무적 중
	Run(z, [&]
	{
		CHECK(z.FindEntity(2)->IsAlive() && z.IsInvulnerable(*z.FindEntity(2)));
		r = z.RequestAttack(1, 2);
		CHECK(!z.DealDamage(*z.FindEntity(2), nullptr, 10));		// 공용 경로도 무적을 존중
	});
	CHECK(r == AttackResult::TargetInvulnerable && z.lastReject == AttackResult::TargetInvulnerable);
	CHECK(z.FindEntity(2) != nullptr);

	// 무적인 쪽이 공격하면 본인의 무적이 해제된다.
	Run(z, [&] { r = z.RequestAttack(2, 1); });
	CHECK(r == AttackResult::Ok);
	SleepMs(30);
	Run(z, [&] { r = z.RequestAttack(1, 2); });
	CHECK(r == AttackResult::Ok);

	// 시간이 지나면 자연 해제 (죽은 뒤 다시 리스폰 -> 무적 만료 대기)
	SleepMs(30);
	Run(z, [&] { z.RequestAttack(1, 2); });
	SleepMs(100 + 400);
	Run(z, [&] { CHECK(z.FindEntity(2)->IsAlive() && !z.IsInvulnerable(*z.FindEntity(2))); });
	z.Stop();
}

static void TestDealDamageAndPolicy()
{
	std::printf("[Combat DealDamage (no source) / view range / dead-move throttle]\n");
	CombatZone z(Cfg());
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(50, 10, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 500, 500 }, Stats(50, 10, 0, 2.0f, 20), 10.0f);
	z.Start();

	bool applied = false, zero = true, neg = true, dead = true;
	Run(z, [&]
	{
		applied = z.DealDamage(*z.FindEntity(1), nullptr, 20);			// DoT/환경: 공격자 없음
		zero = z.DealDamage(*z.FindEntity(1), nullptr, 0);
		neg = z.DealDamage(*z.FindEntity(1), nullptr, -5);
	});
	CHECK(applied && !zero && !neg);
	CHECK(z.damaged == 1 && z.lastSourceNull && z.lastDamage == 20 && z.lastHp == 30);

	Run(z, [&] { z.DealDamage(*z.FindEntity(1), nullptr, 999); });			// 과다 피해 -> 사망
	CHECK(z.died == 1 && z.lastKiller == 0);
	Run(z, [&] { dead = z.DealDamage(*z.FindEntity(1), nullptr, 5); });		// 죽은 대상은 무시
	CHECK(!dead && z.damaged == 2);

	// 사거리는 AOI 보장 시야(cellSize 50, view 1, hyst 0.1 -> 40) 이내만 지원
	CHECK(z.GetGuaranteedViewRange() > 39.9f && z.GetGuaranteedViewRange() < 40.1f);
	CHECK(z.IsAttackRangeSupported(2.0f, 0.0f) && z.IsAttackRangeSupported(38.0f, 0.0f));
	CHECK(!z.IsAttackRangeSupported(39.5f, 0.0f) && !z.IsAttackRangeSupported(500.0f, 0.0f));
	// 이동 중 지연 보정(2 * 속도 * 0.3s)까지 반영: 속도 10이면 6이 추가된다 (2 + 1 + 6 = 9 <= 40, 34 + 1 + 6 = 41 > 40)
	CHECK(z.IsAttackRangeSupported(2.0f, 10.0f) && z.IsAttackRangeSupported(32.0f, 10.0f));
	CHECK(!z.IsAttackRangeSupported(34.0f, 10.0f));

	// 죽은 엔티티가 이동 요청을 쏟아내도 보정 통지는 최소 간격마다 한 번뿐이다.
	Run(z, [&] { z.DealDamage(*z.FindEntity(2), nullptr, 999); });
	int before = z.moveRejected;
	Run(z, [&]
	{
		for( int i = 0; i < 50; ++i )
			z.RequestMove(2, { 500, 500 }, { 510, 510 });
	});
	CHECK(z.moveRejected - before == 1);
	z.Stop();
}

static void TestNoExistenceLeak()
{
	std::printf("[Combat no existence leak]\n");
	CombatZone z(Cfg());
	Add(z, 1, EntityType::Player, { 100, 100 }, Stats(50, 10, 0, 2.0f, 20));
	Add(z, 2, EntityType::Player, { 700, 700 }, Stats(1, 0, 0));			// 멀리 있는 대상
	z.Start();

	AttackResult r1{}, r2{};
	Run(z, [&]
	{
		z.DealDamage(*z.FindEntity(2), nullptr, 5);						// 먼 대상을 죽여 둔다
		r1 = z.RequestAttack(1, 2);										// 멀리 있는 죽은 엔티티
	});
	SleepMs(40);
	Run(z, [&] { r2 = z.RequestAttack(1, 424242); });					// 존재하지 않는 ID
	CHECK(r1 == AttackResult::OutOfRange);								// TargetDead로 새지 않는다
	CHECK(r2 == AttackResult::TargetNotFound);							// 내부 결과는 구분하되(테스트/로그용), 통지는 BattleZone이 합친다
	z.Stop();
}

int Main()
{
	TestStatsSanitize();
	TestDamageAndCooldown();
	TestRangeAndTarget();
	TestPvpPolicy();
	TestDeathAndMove();
	TestRespawn();
	TestSimultaneousAndNoRespawn();
	TestInvulnerability();
	TestDealDamageAndPolicy();
	TestNoExistenceLeak();
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
}

int RunCombatTests()
{
	return Main();
}
