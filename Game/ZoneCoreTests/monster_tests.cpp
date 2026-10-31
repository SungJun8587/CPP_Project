// 몬스터 스폰/AI/어그로 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "MonsterSystem.h"
#include "test_common.h"

#include <cmath>
#include <cstdio>
#include <future>
#include <memory>

// 한 실행 파일에 여러 테스트 파일을 묶으므로, 파일마다 헬퍼/타입 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{
	struct MonZone : CFieldZone<>
	{
		explicit MonZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
		~MonZone() override { Stop(); }

		std::unique_ptr<CMonsterSystem> sys;
		int died = 0, respawned = 0, healed = 0;

	protected:
		void OnUpdate(uint32 dt) override { CFieldZone<>::OnUpdate(dt); if( sys ) sys->Update(); }
		void OnEntityDamaged(CZoneEntity* a, CZoneEntity& v, int32 d) override { if( sys ) sys->OnDamaged(a, v, d); }
		void OnEntityDied(CZoneEntity& v, CZoneEntity*) override { ++died; if( sys ) sys->OnDied(v); }
		void OnEntityRespawned(CZoneEntity& e) override { ++respawned; if( sys ) sys->OnRespawned(e); }
		void OnEntityRemoved(CZoneEntity& e) override { if( sys ) sys->OnRemoved(e); }
		void OnEntityHealed(CZoneEntity&, int32) override { ++healed; }
		uint32 GetRespawnDelayMs(const CZoneEntity& v) const override
		{
			if( sys && sys->IsAiMonster(v) ) return sys->GetRespawnDelayMs(v);
			return CFieldZone<>::GetRespawnDelayMs(v);
		}
		Vec2 GetRespawnPos(const CZoneEntity& e) const override
		{
			Vec2 p;
			if( sys && sys->GetRespawnPos(e, p) ) return p;
			return CFieldZone<>::GetRespawnPos(e);
		}
	};

	struct Pl : CZoneEntity { Pl(uint64 id) : CZoneEntity(id, EntityType::Player) {} };

	static FieldZoneConfig Cfg()
	{
		FieldZoneConfig f;
		f.zone.zoneId = 1; f.zone.tickIntervalMs = 10;
		f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
		f.cellSize = 50; f.viewCells = 3;
		f.respawnDelayMs = 100; f.allowPvp = false; f.minDamage = 1; f.respawnInvulnMs = 0;
		return f;
	}

	static SMonsterTemplate Tmpl(uint32 id)
	{
		SMonsterTemplate t;
		t.id = id; t.name = "m";
		t.stats.maxHp = 100; t.stats.attack = 10; t.stats.defense = 0;
		t.stats.attackRange = 2.0f; t.stats.attackIntervalMs = 100;
		t.speed = 40.0f; t.aggroRange = 10.0f; t.leashRange = 30.0f; t.respawnMs = 500;
		return t;
	}

	static std::shared_ptr<CMonsterData> Data(const SMonsterTemplate& t, uint32 count, float x, float y, float r)
	{
		auto d = std::make_shared<CMonsterData>();
		d->AddMonster(t);
		SSpawnEntry s; s.zoneId = 1; s.monsterId = t.id; s.count = count; s.x = x; s.y = y; s.radius = r;
		d->AddSpawn(s);
		return d;
	}

	template <typename F> static void Run(MonZone& z, F fn);

	static CZoneEntity* AddPlayer(MonZone& z, uint64 id, Vec2 pos, int hp = 1000, int atk = 30)
	{
		if( z.IsRunning() && !z.IsZoneThread() )
		{
			CZoneEntity* r = nullptr;
			Run(z, [&] { r = AddPlayer(z, id, pos, hp, atk); });
			return r;
		}

		auto e = std::make_unique<Pl>(id);
		e->SetSpawnPos(pos);
		SCombatStats s; s.maxHp = hp; s.attack = atk; s.defense = 0; s.attackRange = 2.0f; s.attackIntervalMs = 100;
		e->SetCombatStats(s);
		e->SetSpeed(40.0f);
		return z.AddEntity(std::move(e));
	}

	template <typename F> static void Run(MonZone& z, F fn)
	{
		std::promise<void> p; auto f = p.get_future();
		z.Post([&](CZone&) { fn(); p.set_value(); });
		f.wait();
	}

	static CMonster* Mon(MonZone& z, uint64 serial) { return static_cast<CMonster*>(z.FindEntity(kMonsterIdBase | serial)); }

	static void Setup(MonZone& z, std::shared_ptr<const CMonsterData> d)
	{
		SMonsterSystemConfig c; c.thinkIntervalMs = 20;
		z.sys = std::make_unique<CMonsterSystem>(z, d, c);
	}

	static void TestValidateAndSpawn()
	{
		std::printf("[Monster validate/spawn]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 50, 500, 500, 20));
		std::string err;
		CHECK(z.sys->Validate(err));
		CHECK(z.sys->SpawnAll() == 50 && z.sys->GetMonsterCount() == 50);
		int inDisc = 0, ids = 0;
		for( uint64 i = 1; i <= 50; ++i )
		{
			CMonster* m = Mon(z, i);
			if( m == nullptr ) continue;
			++ids;
			const float dx = m->GetPos().x - 500, dy = m->GetPos().y - 500;
			if( dx * dx + dy * dy <= 20.0f * 20.0f + 0.01f ) ++inDisc;
			CHECK(m->GetType() == EntityType::Monster && z.sys->IsAiMonster(*m));
		}
		CHECK(ids == 50 && inDisc == 50);
		CHECK(z.sys->GetActiveCount() == 0);		// 플레이어가 없으면 AI 비용 0

		// 검증 실패: 스폰이 영역 밖 / 선공 거리가 AOI 시야 초과
		MonZone z2(Cfg());
		Setup(z2, Data(Tmpl(1), 1, 5000, 5, 0));
		CHECK(!z2.sys->Validate(err));
		SMonsterTemplate farTmpl = Tmpl(2); farTmpl.aggroRange = 1000; farTmpl.leashRange = 2000;
		MonZone z3(Cfg());
		Setup(z3, Data(farTmpl, 1, 500, 500, 0));
		CHECK(!z3.sys->Validate(err));
		CHECK(!(Tmpl(3).id == 0));
	}

	static void TestValidateOpponentSpeed()
	{
		std::printf("[Monster validate: opponent speed]\n");
		// 보장 시야 (3 - 0.2) * 50 = 140. 판정 거리 = 사거리 + 1 + 2 * 최고속도 * 0.3
		SMonsterTemplate t = Tmpl(1);
		t.stats.attackRange = 125.0f; t.speed = 10.0f;
		MonZone z(Cfg());
		Setup(z, Data(t, 1, 500, 500, 0));
		std::string err;
		CHECK(z.sys->Validate(err));					// 132 <= 140
		CHECK(z.sys->Validate(err, 10.0f));
		CHECK(!z.sys->Validate(err, 40.0f));			// 상대가 더 빠르면 150 > 140
		CHECK(z.sys->GetMaxSpawnSpeed() == 10.0f);
	}

	static void TestRemoveActiveMonster()
	{
		std::printf("[Monster remove while active]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 3, 500, 500, 5));
		CHECK(z.sys->SpawnAll() == 3);
		AddPlayer(z, 1, { 505, 500 });
		z.Start();
		SleepMs(150);

		size_t act = 0, cnt = 0;
		Run(z, [&] { act = z.sys->GetActiveCount(); });
		CHECK(act == 3);								// 시야에 플레이어가 있어 모두 활성
		Run(z, [&] { z.RemoveEntity(kMonsterIdBase | 1); act = z.sys->GetActiveCount(); cnt = z.sys->GetMonsterCount(); });
		CHECK(act == 2 && cnt == 2);					// 활성 목록과 몬스터 수에서 함께 빠진다
		SleepMs(250);									// 이후에도 AI가 해제된 몬스터를 건드리지 않는다 (ASan이 해제된 메모리 접근을 잡는다)
		Run(z, [&] { act = z.sys->GetActiveCount(); });
		CHECK(act == 2);
		z.Stop();
	}

	static void TestAggroChaseAttack()
	{
		std::printf("[Monster proximity aggro/chase/attack]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 1, 500, 500, 0));
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 800, 800 });					// 멀리: 시야 밖
		z.Start();
		SleepMs(100);
		bool act0 = true; Run(z, [&] { act0 = Mon(z, 1)->IsActive(); });
		CHECK(!act0);

		AddPlayer(z, 2, { 506, 500 });					// 선공 거리(10) 안
		SleepMs(300);
		int hp = 1000; EMonsterState st = EMonsterState::Idle; uint64 tg = 0;
		Run(z, [&] { hp = z.FindEntity(2)->GetHp(); st = Mon(z, 1)->GetState(); tg = Mon(z, 1)->GetTargetId(); });
		CHECK(hp < 1000);
		CHECK(st == EMonsterState::Chase && tg == 2);
		z.Stop();
	}

	static void TestDamageAggroAndSwitch()
	{
		std::printf("[Monster damage aggro / switch hysteresis / eviction]\n");
		SMonsterTemplate t = Tmpl(1); t.aggroRange = 0.0f;			// 선공 없음
		MonZone z(Cfg());
		Setup(z, Data(t, 1, 500, 500, 0));
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 520, 500 });
		AddPlayer(z, 2, { 500, 520 });
		z.Start();
		SleepMs(100);
		EMonsterState st = EMonsterState::Chase;
		Run(z, [&] { st = Mon(z, 1)->GetState(); });
		CHECK(st == EMonsterState::Idle);							// 선공하지 않으면 가만히 있는다

		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 50); });
		SleepMs(60);
		uint64 tg = 0; Run(z, [&] { tg = Mon(z, 1)->GetTargetId(); st = Mon(z, 1)->GetState(); });
		CHECK(tg == 1 && st == EMonsterState::Chase);

		// 2번이 105%만큼 때려도 전환하지 않고, 110% 초과여야 전환
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(2), 5); });
		SleepMs(60);
		Run(z, [&] { tg = Mon(z, 1)->GetTargetId(); });
		CHECK(tg == 1);
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(2), 1); });	// 이제 합계 6 이지만 1번은 50 -> 계속 1번
		SleepMs(60);
		Run(z, [&] { tg = Mon(z, 1)->GetTargetId(); });
		CHECK(tg == 1);
		z.Stop();

		// 어그로 표 용량: 9명이 때려도 8항목 이하이고 가장 낮은 위협이 밀려난다
		CMonster m(kMonsterIdBase | 99, t, nullptr, { 0, 0 }, 0);
		for( uint64 i = 1; i <= 8; ++i ) m.AddThreat(i, (int64)i * 10);
		CHECK(m.GetAggroCount() == 8);
		m.AddThreat(100, 5);						// 최저(10)보다 낮으면 들어오지 못함
		CHECK(m.GetAggroCount() == 8);
		m.AddThreat(101, 15);						// 최저(10)를 밀어낸다
		CHECK(m.GetAggroCount() == 8);
		m.AddThreat(3, std::numeric_limits<int64>::max());
		m.AddThreat(3, 100);						// 포화
		m.RemoveThreat(3);
		CHECK(m.GetAggroCount() == 7);
		m.ClearAggro();
		CHECK(m.GetAggroCount() == 0 && m.GetTargetId() == 0);
	}

	static void TestLeashReturnHeal()
	{
		std::printf("[Monster leash/return/heal]\n");
		SMonsterTemplate t = Tmpl(1); t.leashRange = 12.0f; t.aggroRange = 0.0f; t.speed = 30.0f;
		MonZone z(Cfg());
		Setup(z, Data(t, 1, 500, 500, 0));
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 504, 500 }, 100000, 5);
		z.Start();
		SleepMs(60);
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 40); });
		// 플레이어를 멀리 순간이동 -> 몬스터가 추적하다 leash 초과 -> 복귀
		Run(z, [&] { z.Teleport(1, { 540, 500 }); });
		bool seenReturn = false;
		for( int i = 0; i < 100 && !seenReturn; ++i )
		{
			SleepMs(20);
			Run(z, [&] { seenReturn = Mon(z, 1)->GetState() == EMonsterState::Return; });
		}
		CHECK(seenReturn);
		SleepMs(1500);
		int hp = 0; float dx = 0; EMonsterState st = EMonsterState::Chase; size_t ag = 99;
		Run(z, [&] { CMonster* m = Mon(z, 1); hp = m->GetHp(); dx = m->GetPos().x - 500; st = m->GetState(); ag = m->GetAggroCount(); });
		CHECK(hp == 100 && std::fabs(dx) < 1.0f);
		CHECK(st == EMonsterState::Idle && ag == 0);
		CHECK(z.healed >= 1);
		z.Stop();
	}

	static void TestDeathRespawn()
	{
		std::printf("[Monster death/respawn]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 1, 500, 500, 15));
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 505, 500 });
		z.Start();
		SleepMs(60);
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 100000); });
		bool alive = true, act = true; Run(z, [&] { alive = Mon(z, 1)->IsAlive(); act = Mon(z, 1)->IsActive(); });
		CHECK(!alive && !act && z.died == 1);
		SleepMs(300);
		Run(z, [&] { alive = Mon(z, 1)->IsAlive(); });
		CHECK(!alive);								// 템플릿 respawnMs(500) 전에는 부활하지 않는다
		SleepMs(400);
		float dx = 0, dy = 0; int hp = 0;
		Run(z, [&] { CMonster* m = Mon(z, 1); alive = m->IsAlive(); hp = m->GetHp(); dx = m->GetPos().x - 500; dy = m->GetPos().y - 500; });
		CHECK(alive && hp == 100 && z.respawned == 1);
		CHECK(dx * dx + dy * dy <= 15.0f * 15.0f + 0.01f);
		z.Stop();
	}

	static void TestTargetDiesAndDeactivate()
	{
		std::printf("[Monster target death/deactivate]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 1, 500, 500, 0));
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 504, 500 }, 20, 1);
		z.Start();
		SleepMs(500);								// 플레이어(체력 20)가 몬스터에게 죽는다
		bool dead = false; Run(z, [&] { dead = !z.FindEntity(1)->IsAlive(); });
		CHECK(dead);
		EMonsterState st = EMonsterState::Chase; size_t ag = 9;
		Run(z, [&] { st = Mon(z, 1)->GetState(); ag = Mon(z, 1)->GetAggroCount(); });
		CHECK((st == EMonsterState::Idle || st == EMonsterState::Return) && ag == 0);
		Run(z, [&] { z.RemoveEntity(1); });			// 플레이어 퇴장 -> 시야 비면 비활성
		SleepMs(300);
		size_t act = 9; Run(z, [&] { act = z.sys->GetActiveCount(); });
		CHECK(act == 0);
		z.Stop();
	}

	static void TestMany()
	{
		std::printf("[Monster many + players churn]\n");
		MonZone z(Cfg());
		Setup(z, Data(Tmpl(1), 400, 500, 500, 100));
		std::string err; CHECK(z.sys->Validate(err));
		CHECK(z.sys->SpawnAll() == 400);
		z.Start();
		for( uint64 i = 1; i <= 20; ++i ) AddPlayer(z, i, { 450.0f + (float)i * 5, 500 }, 100000, 20);
		SleepMs(400);
		for( int r = 0; r < 20; ++r )
		{
			Run(z, [&] { for( uint64 i = 1; i <= 20; ++i ) z.Teleport(i, { 400.0f + (float)((i * 37 + r * 11) % 200), 400.0f + (float)((i * 53 + r * 7) % 200) }); });
			SleepMs(30);
			Run(z, [&] { for( uint64 i = 1; i <= 20; ++i ) if( z.FindEntity(i) ) z.RequestAttack(i, kMonsterIdBase | (1 + (i * 13 + r) % 400)); });
		}
		for( uint64 i = 1; i <= 20; ++i ) Run(z, [&] { z.RemoveEntity(i); });
		SleepMs(2500);
		size_t act = 9; Run(z, [&] { act = z.sys->GetActiveCount(); });
		CHECK(act == 0);
		z.Stop();
	}

	int Main()
	{
		TestValidateAndSpawn();
		TestAggroChaseAttack();
		TestDamageAggroAndSwitch();
		TestLeashReturnHeal();
		TestDeathRespawn();
		TestTargetDiesAndDeactivate();
		TestMany();
		TestValidateOpponentSpeed();
		TestRemoveActiveMonster();
		std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
		return g_failed ? 1 : 0;
	}
}

int RunMonsterTests()
{
	return Main();
}