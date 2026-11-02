// 스킬(단일/범위 즉시 시전) 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "HeroData.h"
#include "BlockMap.h"
#include "TestCommon.h"

#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{

struct SkEntity : CZoneEntity
{
	SkEntity(uint64 id, EntityType t) : CZoneEntity(id, t) {}
};

struct SkZone : CFieldZone<>
{
	explicit SkZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
	~SkZone() override { Stop(); }

	int casts = 0, rejects = 0;
	uint32 lastHitCount = 0;
	uint64 lastTargetId = 0;
	Vec2 lastCenter{};
	SkillResult lastReject = SkillResult::Ok;
	std::vector<std::pair<uint64, int32>> damages;		// (피해자, 피해량)

protected:
	void OnEntityDamaged(CZoneEntity*, CZoneEntity& v, int32 d) override { damages.push_back({ v.GetId(), d }); }
	void OnSkillCast(CZoneEntity&, const SSkillTemplate&, uint64 t, const Vec2& c, uint32 hits) override { ++casts; lastTargetId = t; lastCenter = c; lastHitCount = hits; }
	void OnSkillRejected(CZoneEntity&, const SSkillTemplate&, SkillResult r) override { ++rejects; lastReject = r; }
};

static FieldZoneConfig Cfg()
{
	FieldZoneConfig f;
	f.zone.zoneId = 1;
	f.zone.tickIntervalMs = 10;
	f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
	f.cellSize = 50;
	f.viewCells = 2;
	f.minDamage = 1;
	f.respawnInvulnMs = 0;
	return f;
}

static SCombatStats Stats(int hp, int atk, int def)
{
	SCombatStats s; s.maxHp = hp; s.attack = atk; s.defense = def; s.attackRange = 2.0f; s.attackIntervalMs = 100;
	return s;
}

static CZoneEntity* Add(SkZone& z, uint64 id, EntityType t, Vec2 pos, const SCombatStats& s)
{
	auto e = std::make_unique<SkEntity>(id, t);
	e->SetSpawnPos(pos);
	e->SetCombatStats(s);
	return z.AddEntity(std::move(e));
}

template <typename F>
static void Run(SkZone& z, F fn)
{
	std::promise<void> p;
	auto f = p.get_future();
	z.Post([&](CZone&) { fn(); p.set_value(); });
	f.wait();
}

static SSkillTemplate Single(uint32 id)
{
	SSkillTemplate s; s.id = id; s.name = "strike"; s.shape = ESkillShape::Single; s.damagePercent = 150; s.flatDamage = 5;
	s.range = 20.0f; s.radius = 0.0f; s.cooldownMs = 500; s.maxTargets = 1;
	return s;
}

static SSkillTemplate Area(uint32 id, uint32 maxTargets)
{
	SSkillTemplate s; s.id = id; s.name = "blast"; s.shape = ESkillShape::Area; s.damagePercent = 100; s.flatDamage = 0;
	s.range = 30.0f; s.radius = 5.0f; s.cooldownMs = 500; s.maxTargets = maxTargets;
	return s;
}

static void TestData()
{
	std::printf("[Skill data]\n");
	CHECK(CalcSkillDamage(Single(1), 20, 10, 1) == 25);			// 20*150/100 + 5 - 10
	CHECK(CalcSkillDamage(Single(1), 1, 999, 3) == 3);			// 최소 피해
	CHECK(CalcSkillDamage(Single(1), INT32_MAX, 0, 1) == INT32_MAX);	// 오버플로 없음

	auto make = [](const SSkillTemplate& s)
	{
		auto d = std::make_shared<CHeroData>();
		SHeroTemplate h; h.id = 1; h.name = "h"; d->AddHero(h);
		CHECK(d->AddSkill(s));
		return d;
	};
	std::string err;
	CHECK(make(Single(1))->Validate(err));
	CHECK(make(Area(2, 8))->Validate(err));

	SSkillTemplate bad = Single(3); bad.range = 0;						CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.cooldownMs = 0;								CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.radius = 2;									CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.maxTargets = 2;								CHECK(!make(bad)->Validate(err));
	bad = Area(3, 8); bad.radius = 0;									CHECK(!make(bad)->Validate(err));
	bad = Area(3, 0);													CHECK(!make(bad)->Validate(err));
	bad = Area(3, kMaxSkillTargets + 1);								CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.damagePercent = 0; bad.flatDamage = 0;		CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.damagePercent = kMaxSkillDamagePercent + 1;	CHECK(!make(bad)->Validate(err));
	bad = Single(3); bad.range = NAN;									CHECK(!make(bad)->Validate(err));

	// 연결: 없는 영웅/스킬, 중복, 영웅당 상한
	auto d = std::make_shared<CHeroData>();
	SHeroTemplate h; h.id = 1; h.name = "h"; d->AddHero(h);
	for( uint32 i = 1; i <= kMaxHeroSkills + 1; ++i ) d->AddSkill(Single(i));
	CHECK(!d->AddSkill(Single(1)));											// 중복 ID
	CHECK(!d->AssignSkill(99, 1) && !d->AssignSkill(1, 999));
	for( uint32 i = 1; i <= kMaxHeroSkills; ++i ) CHECK(d->AssignSkill(1, i));
	CHECK(!d->AssignSkill(1, 1) && !d->AssignSkill(1, kMaxHeroSkills + 1));
	CHECK(d->FindHeroSkill(*d->FindHero(1), 3) != nullptr && d->FindHeroSkill(*d->FindHero(1), kMaxHeroSkills + 1) == nullptr);
}

static void TestSingle()
{
	std::printf("[Skill single]\n");
	SkZone z(Cfg());
	CZoneEntity* caster = Add(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	Add(z, 2, EntityType::Monster, { 310, 300 }, Stats(1000, 5, 10));
	Add(z, 3, EntityType::Monster, { 400, 300 }, Stats(1000, 5, 10));			// 사거리 밖
	z.Start();

	const SSkillTemplate skill = Single(1);
	uint64 ready = 0;
	SkillResult r1{}, r2{}, r3{}, r4{}, r5{}, r6{};
	Run(z, [&] { r1 = z.CastSkill(*caster, skill, 2, {}, ready); });
	CHECK(r1 == SkillResult::Ok && z.casts == 1 && z.lastTargetId == 2 && z.lastHitCount == 1);
	CHECK(z.damages.size() == 1 && z.damages[0].first == 2 && z.damages[0].second == 25);	// 20*150/100 + 5 - 10

	SleepMs(40);
	const uint64 readyAfterCast = ready;
	Run(z, [&] { r2 = z.CastSkill(*caster, skill, 2, {}, ready); });					// 재사용 대기
	CHECK(r2 == SkillResult::Cooldown && z.lastReject == SkillResult::Cooldown && ready == readyAfterCast && z.damages.size() == 1);

	SleepMs(550);
	Run(z, [&] { r3 = z.CastSkill(*caster, skill, 2, {}, ready); });
	CHECK(r3 == SkillResult::Ok && z.damages.size() == 2);

	SleepMs(550);
	Run(z, [&] { r4 = z.CastSkill(*caster, skill, 3, {}, ready); });					// 사거리 밖
	SleepMs(40);
	Run(z, [&] { r5 = z.CastSkill(*caster, skill, 999, {}, ready); });				// 없는 대상: 사거리 밖과 같은 사유
	SleepMs(40);
	Run(z, [&] { r6 = z.CastSkill(*caster, skill, 1, {}, ready); });					// 자기 자신
	CHECK(r4 == SkillResult::OutOfRange && r5 == SkillResult::OutOfRange && r6 == SkillResult::InvalidTarget);
	CHECK(z.damages.size() == 2);

	// 같은 틱의 중복 요청은 조용히 무시된다(거부 통지 없음)
	SleepMs(550);
	const int rejectsBefore = z.rejects;
	SkillResult a{}, b{};
	Run(z, [&] { a = z.CastSkill(*caster, skill, 2, {}, ready); b = z.CastSkill(*caster, skill, 2, {}, ready); });
	CHECK(a == SkillResult::Ok && b == SkillResult::Throttled && z.rejects == rejectsBefore);

	// 사망한 시전자
	Run(z, [&] { z.DealDamage(*caster, nullptr, 100000); });
	SleepMs(40);
	SkillResult d{};
	uint64 fresh = 0;
	Run(z, [&] { d = z.CastSkill(*caster, skill, 2, {}, fresh); });
	CHECK(d == SkillResult::CasterDead && fresh == 0);
	z.Stop();
}

static void TestArea()
{
	std::printf("[Skill area]\n");
	SkZone z(Cfg());
	CZoneEntity* caster = Add(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	Add(z, 10, EntityType::Monster, { 320, 300 }, Stats(1000, 5, 4));			// 중심
	Add(z, 11, EntityType::Monster, { 322, 300 }, Stats(1000, 5, 4));			// 중심에서 2
	Add(z, 12, EntityType::Monster, { 324.9f, 300 }, Stats(1000, 5, 4));		// 반경 안쪽 끝
	Add(z, 13, EntityType::Monster, { 326, 300 }, Stats(1000, 5, 4));			// 반경 밖(6)
	Add(z, 14, EntityType::Player, { 320, 302 }, Stats(1000, 5, 0));			// 같은 편 플레이어(PvP 기본 허용 여부와 무관하게 아래에서 검사)
	z.Start();

	const SSkillTemplate wide = Area(5, 8);
	uint64 ready = 0;
	SkillResult r{};
	Run(z, [&] { r = z.CastSkill(*caster, wide, 0, { 320, 300 }, ready); });
	CHECK(r == SkillResult::Ok && z.lastTargetId == 0 && z.lastCenter.x == 320.0f && z.lastCenter.y == 300.0f);

	bool got10 = false, got11 = false, got12 = false, got13 = false, got1 = false;
	for( const auto& d : z.damages )
	{
		if( d.first == 10 ) got10 = true;
		if( d.first == 11 ) got11 = true;
		if( d.first == 12 ) got12 = true;
		if( d.first == 13 ) got13 = true;
		if( d.first == 1 ) got1 = true;
		if( d.first == 10 || d.first == 11 || d.first == 12 ) CHECK(d.second == 16);	// 20*100/100 - 4
	}
	CHECK(got10 && got11 && got12 && !got13 && !got1);						// 반경 밖과 시전자 본인은 제외

	// 대상 수 상한: 중심에 가까운 순으로 자른다
	SleepMs(550);
	z.damages.clear();
	const SSkillTemplate narrow = Area(6, 2);
	uint64 ready2 = 0;
	Run(z, [&] { z.CastSkill(*caster, narrow, 0, { 320, 300 }, ready2); });
	int victims = 0; bool near10 = false, near11 = false;
	for( const auto& d : z.damages ) { if( d.first >= 10 && d.first <= 14 ) ++victims; if( d.first == 10 ) near10 = true; if( d.first == 11 ) near11 = true; }
	CHECK(z.lastHitCount == 2 && victims == 2 && near10 && near11);		// 가장 가까운 10(거리 0)과, 거리 2로 동률인 11/14 중 ID가 작은 11

	// 범위 밖 중심 / 영역 밖 / NaN
	SleepMs(40);
	SkillResult farR{}, outsideR{}, nanR{};
	uint64 ready3 = 0;
	Run(z, [&] { farR = z.CastSkill(*caster, wide, 0, { 400, 300 }, ready3); });
	SleepMs(40);
	Run(z, [&] { outsideR = z.CastSkill(*caster, wide, 0, { -50, 300 }, ready3); });
	SleepMs(40);
	Run(z, [&] { nanR = z.CastSkill(*caster, wide, 0, { NAN, 300 }, ready3); });
	CHECK(farR == SkillResult::OutOfRange && outsideR == SkillResult::OutOfRange && nanR == SkillResult::OutOfRange && ready3 == 0);

	// 대상이 없어도 시전은 성공하고 재사용 대기가 시작된다
	SleepMs(40);
	SkillResult empty{};
	uint64 ready4 = 0;
	Run(z, [&] { empty = z.CastSkill(*caster, wide, 0, { 300, 320 }, ready4); });
	CHECK(empty == SkillResult::Ok && z.lastHitCount == 0 && ready4 != 0);
	z.Stop();
}

static void TestLineOfSight()
{
	std::printf("[Skill line of sight]\n");
	auto bm = std::make_shared<CBlockMap>();
	CHECK(bm->Init(0, 0, 1000, 1000, 1.0f));
	bm->AddRect(305, 0, 2, 1000);					// x=305..307 세로 벽

	FieldZoneConfig cfg = Cfg(); cfg.blockMap = bm;
	SkZone z(cfg);
	CZoneEntity* caster = Add(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	Add(z, 2, EntityType::Monster, { 312, 300 }, Stats(1000, 5, 0));			// 벽 건너편
	Add(z, 3, EntityType::Monster, { 300, 312 }, Stats(1000, 5, 0));			// 같은 쪽
	Add(z, 4, EntityType::Monster, { 303, 300 }, Stats(1000, 5, 0));			// 같은 쪽(폭발 중심 쪽)
	Add(z, 5, EntityType::Monster, { 309, 300 }, Stats(1000, 5, 0));			// 벽 건너편(중심과 같은 쪽 아님)
	z.Start();

	uint64 ready = 0;
	SkillResult s1{}, s2{};
	Run(z, [&] { s1 = z.CastSkill(*caster, Single(1), 2, {}, ready); });
	CHECK(s1 == SkillResult::NoLineOfSight && ready == 0 && z.damages.empty());		// 거부는 재사용 대기를 쓰지 않는다
	SleepMs(40);
	Run(z, [&] { s2 = z.CastSkill(*caster, Single(1), 3, {}, ready); });
	CHECK(s2 == SkillResult::Ok);

	// 범위 스킬: 시전자->중심 선분이 벽을 지나면 거부, 중심과 대상 사이에 벽이 있으면 그 대상은 맞지 않는다
	SleepMs(550);
	z.damages.clear();
	SkillResult a1{}, a2{};
	uint64 r2 = 0;
	Run(z, [&] { a1 = z.CastSkill(*caster, Area(6, 8), 0, { 312, 300 }, r2); });
	CHECK(a1 == SkillResult::NoLineOfSight);
	SleepMs(40);
	Run(z, [&] { a2 = z.CastSkill(*caster, Area(6, 8), 0, { 303, 300 }, r2); });	// 중심은 벽 앞쪽, 반경 5 안에 벽 너머 5번(309)은 없음
	CHECK(a2 == SkillResult::Ok);
	bool hit4 = false, hit5 = false, hit2 = false;
	for( const auto& d : z.damages ) { if( d.first == 4 ) hit4 = true; if( d.first == 5 ) hit5 = true; if( d.first == 2 ) hit2 = true; }
	CHECK(hit4 && !hit5 && !hit2);
	z.Stop();
}

static int Main()
{
	TestData();
	TestSingle();
	TestArea();
	TestLineOfSight();
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
}

int RunSkillTests()
{
	return Main();
}
