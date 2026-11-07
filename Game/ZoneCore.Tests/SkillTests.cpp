// 스킬(단일/범위 즉시 시전) 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "HeroData.h"
#include "BlockMap.h"
#include "TestCommon.h"

#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <set>
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

// ── 시전 시간 / 투사체 / 상태 효과 ──────────────────────

struct FxZone : CFieldZone<>
{
	explicit FxZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
	~FxZone() override { Stop(); }

	std::map<uint32, SStatusTemplate> statuses;
	int casts = 0, rejects = 0, castStarted = 0, castCancelled = 0, launched = 0, impacts = 0, applied = 0, ended = 0, endedExpired = 0, endedDied = 0, endedBroken = 0, deaths = 0;
	std::vector<std::pair<uint64, int32>> heals;		// (회복된 엔티티, 실제 회복량)
	SkillCancelReason lastCancel = SkillCancelReason::Moved;
	SkillResult lastReject = SkillResult::Ok;
	uint32 lastHitCount = 99, lastFlight = 0, lastImpactHits = 99;
	Vec2 lastImpact{};
	uint64 lastKiller = 0;
	std::vector<std::pair<uint64, int32>> damages;		// (피해자, 피해량)
	std::vector<uint64> damageSources;					// 공격자 ID (없으면 0)

	int Damages(uint64 victim) const { int n = 0; for( const auto& d : damages ) if( d.first == victim ) ++n; return n; }

protected:
	const SStatusTemplate* FindStatusTemplate(uint32 id) const override { auto it = statuses.find(id); return it == statuses.end() ? nullptr : &it->second; }
	void OnEntityDamaged(CZoneEntity* a, CZoneEntity& v, int32 d) override { damages.push_back({ v.GetId(), d }); damageSources.push_back(a ? a->GetId() : 0); }
	void OnEntityDied(CZoneEntity&, CZoneEntity* k) override { ++deaths; lastKiller = k ? k->GetId() : 0; }
	void OnSkillCast(CZoneEntity&, const SSkillTemplate&, uint64, const Vec2&, uint32 hits) override { ++casts; lastHitCount = hits; }
	void OnSkillRejected(CZoneEntity&, const SSkillTemplate&, SkillResult r) override { ++rejects; lastReject = r; }
	void OnSkillCastStarted(CZoneEntity&, const SSkillTemplate&, uint64, const Vec2&) override { ++castStarted; }
	void OnSkillCastCancelled(CZoneEntity&, const SSkillTemplate&, SkillCancelReason r) override { ++castCancelled; lastCancel = r; }
	void OnProjectileLaunched(CZoneEntity&, const SSkillTemplate&, uint32, uint64, const Vec2&, const Vec2&, uint32 flight) override { ++launched; lastFlight = flight; }
	void OnProjectileImpact(CZoneEntity&, const SSkillTemplate&, uint32, const Vec2& c, uint32 hits) override { ++impacts; lastImpact = c; lastImpactHits = hits; }
	void OnStatusApplied(CZoneEntity&, const SStatusTemplate&, uint64) override { ++applied; }
	void OnStatusEnded(CZoneEntity&, uint32, StatusEndReason r) override { ++ended; if( r == StatusEndReason::Expired ) ++endedExpired; else if( r == StatusEndReason::Broken ) ++endedBroken; else ++endedDied; }
	void OnEntityHealed(CZoneEntity& e, int32 amount) override { heals.push_back({ e.GetId(), amount }); }
};

static CZoneEntity* AddFx(FxZone& z, uint64 id, EntityType t, Vec2 pos, const SCombatStats& s, float speed = 6.0f)
{
	auto e = std::make_unique<SkEntity>(id, t);
	e->SetSpawnPos(pos);
	e->SetCombatStats(s);
	e->SetSpeed(speed);
	return z.AddEntity(std::move(e));
}

template <typename F>
static void RunFx(FxZone& z, F fn)
{
	std::promise<void> p;
	auto f = p.get_future();
	z.Post([&](CZone&) { fn(); p.set_value(); });
	f.wait();
}

static SSkillTemplate Timed(uint32 id, uint32 castMs)
{
	SSkillTemplate s = Single(id);
	s.castTimeMs = castMs;
	s.cooldownMs = 1000;
	return s;
}

static SStatusTemplate Dot(uint32 id, uint32 durationMs, uint32 tickMs, uint32 pct, int32 flat = 0)
{
	SStatusTemplate t; t.id = id; t.name = "dot"; t.kind = EStatusKind::Dot; t.durationMs = durationMs; t.tickMs = tickMs; t.damagePercent = pct; t.flatDamage = flat;
	return t;
}

static SStatusTemplate Buff(uint32 id, uint32 durationMs, int32 atk, int32 def, int32 spd)
{
	SStatusTemplate t; t.id = id; t.name = "buff"; t.kind = EStatusKind::Buff; t.durationMs = durationMs; t.tickMs = 0;
	t.attackPercent = atk; t.defensePercent = def; t.speedPercent = spd;
	return t;
}

static void TestEffectData()
{
	std::printf("[Skill effect data]\n");
	CHECK(CalcDotDamage(Dot(1, 1000, 200, 50), 20) == 10);
	CHECK(CalcDotDamage(Dot(1, 1000, 200, 0, 7), 20) == 7);
	CHECK(CalcDotDamage(Dot(1, 1000, 200, 1), 1) == 1);								// 최소 1
	CHECK(ApplyStatPercent(20, 50) == 30 && ApplyStatPercent(20, -50) == 10 && ApplyStatPercent(20, -100) == 0);
	CHECK(ApplyStatPercent(INT32_MAX, 500) == INT32_MAX);

	auto make = [](const SSkillTemplate& sk, const SStatusTemplate* st)
	{
		auto d = std::make_shared<CHeroData>();
		SHeroTemplate h; h.id = 1; h.name = "h"; d->AddHero(h);
		if( st ) CHECK(d->AddStatus(*st));
		CHECK(d->AddSkill(sk));
		return d;
	};
	std::string err;
	SStatusTemplate dot = Dot(5, 1000, 200, 50);
	SSkillTemplate withStatus = Single(1); withStatus.statusId = 5;
	CHECK(make(withStatus, &dot)->Validate(err));
	CHECK(!make(withStatus, nullptr)->Validate(err));								// 없는 상태 효과
	SSkillTemplate pureStatus = Single(1); pureStatus.damagePercent = 0; pureStatus.flatDamage = 0; pureStatus.statusId = 5;
	CHECK(make(pureStatus, &dot)->Validate(err));									// 피해 없이 상태 효과만 거는 스킬
	pureStatus.statusId = 0;
	CHECK(!make(pureStatus, nullptr)->Validate(err));

	SSkillTemplate timed = Timed(1, 500);												CHECK(make(timed, nullptr)->Validate(err));
	timed.castTimeMs = kMaxCastTimeMs + 1;											CHECK(!make(timed, nullptr)->Validate(err));
	SSkillTemplate proj = Single(1); proj.projectileSpeed = 50.0f;						CHECK(make(proj, nullptr)->Validate(err));
	proj.projectileSpeed = -1.0f;													CHECK(!make(proj, nullptr)->Validate(err));
	proj.projectileSpeed = NAN;														CHECK(!make(proj, nullptr)->Validate(err));
	proj.projectileSpeed = kMaxProjectileSpeed + 1.0f;								CHECK(!make(proj, nullptr)->Validate(err));
	proj.projectileSpeed = 1.0f;													CHECK(!make(proj, nullptr)->Validate(err));	// 거리 20 / 속도 1 = 20초: 비행 시간 상한 초과

	auto badStatus = [&](const SStatusTemplate& t) { auto d = make(Single(1), &t); return d->Validate(err); };
	CHECK(badStatus(Dot(5, 1000, 200, 50)) && badStatus(Buff(5, 1000, 10, 0, 0)));
	CHECK(!badStatus(Dot(5, 0, 200, 50)));											// 지속시간 0
	CHECK(!badStatus(Dot(5, kMaxStatusDurationMs + 1, 200, 50)));
	CHECK(!badStatus(Dot(5, 1000, 50, 50)));										// 틱이 너무 짧다
	CHECK(!badStatus(Dot(5, 1000, 2000, 50)));										// 틱이 지속시간보다 길다
	CHECK(!badStatus(Dot(5, 1000, 200, 0, 0)));										// 피해 없음
	SStatusTemplate mixed = Dot(5, 1000, 200, 50); mixed.attackPercent = 10;			CHECK(!badStatus(mixed));
	CHECK(!badStatus(Buff(5, 1000, 0, 0, 0)));										// 보정 없음
	CHECK(!badStatus(Buff(5, 1000, kMaxStatPercent + 1, 0, 0)));
	CHECK(!badStatus(Buff(5, 1000, 0, 0, kMaxSpeedBuffPercent + 1)));
	CHECK(!badStatus(Buff(5, 1000, 0, kMinStatPercent - 1, 0)));
	SStatusTemplate buffTick = Buff(5, 1000, 10, 0, 0); buffTick.tickMs = 100;			CHECK(!badStatus(buffTick));

	auto d = std::make_shared<CHeroData>();
	CHECK(!d->AddStatus(SStatusTemplate{}));										// ID 0
	CHECK(d->AddStatus(Buff(3, 1000, 0, 0, 40)) && !d->AddStatus(Buff(3, 1000, 0, 0, 40)));
	CHECK(d->AddStatus(Buff(4, 1000, 0, 0, 80)) && d->GetMaxSpeedBuffPercent() == 80);
	CHECK(d->FindStatus(3) != nullptr && d->FindStatus(9) == nullptr && d->GetStatusCount() == 2);
}

static void TestCastTime()
{
	std::printf("[Skill cast time]\n");
	FxZone z(Cfg());
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	CZoneEntity* target = AddFx(z, 2, EntityType::Monster, { 310, 300 }, Stats(1000, 5, 10));
	z.Start();

	const SSkillTemplate skill = Timed(1, 200);
	uint64 ready = 0;
	SkillResult r{}, other{};
	AttackResult atk{};
	bool casting = false;
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); casting = caster->IsCasting(); });
	CHECK(r == SkillResult::Ok && casting && z.castStarted == 1 && z.casts == 0 && z.damages.empty());
	CHECK(ready >= 1000);																	// 쿨다운은 시전 시작 시점부터

	// 시전 중에는 기본 공격과 다른 스킬이 거부된다
	SleepMs(30);
	RunFx(z, [&] { atk = z.RequestAttack(*caster, 2); });
	SleepMs(20);
	uint64 ready2 = 0;
	RunFx(z, [&] { other = z.CastSkill(*caster, Single(2), 2, {}, ready2); });
	CHECK(atk == AttackResult::Casting && other == SkillResult::Casting && z.lastReject == SkillResult::Casting && ready2 == 0);

	SleepMs(300);
	RunFx(z, [&] { casting = caster->IsCasting(); });
	CHECK(!casting && z.casts == 1 && z.lastHitCount == 1 && z.Damages(2) == 1 && z.damages[0].second == 25 && z.castCancelled == 0);

	// 이동하면 취소되고 효과가 없다. 쿨다운은 돌려주지 않는다.
	SleepMs(900);
	z.damages.clear();
	Vec2 pos{};
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); pos = caster->GetPos(); });
	SleepMs(40);
	MoveResult mr{};
	const uint64 readyBefore = ready;
	RunFx(z, [&] { mr = z.RequestMove(*caster, caster->GetPos(), Vec2{ pos.x - 20, pos.y }); casting = caster->IsCasting(); });
	CHECK(r == SkillResult::Ok && mr == MoveResult::Ok && !casting && z.castCancelled == 1 && z.lastCancel == SkillCancelReason::Moved);
	CHECK(ready == readyBefore);
	SleepMs(300);
	CHECK(z.damages.empty() && z.casts == 1);
	RunFx(z, [&] { z.StopMove(1); z.Teleport(1, Vec2{ 300, 300 }); });

	// 이동 중에 시전을 시작하면 그 자리에 멈춘다
	SleepMs(900);
	bool moving = true;
	RunFx(z, [&] { z.RequestMove(*caster, caster->GetPos(), Vec2{ 300, 340 }); });
	SleepMs(60);
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); moving = caster->IsMoving(); });
	CHECK(r == SkillResult::Ok && !moving);
	SleepMs(300);
	RunFx(z, [&] { pos = caster->GetPos(); });
	CHECK(z.casts == 2 && pos.y < 340.0f);
	RunFx(z, [&] { z.Teleport(1, Vec2{ 300, 300 }); });

	// 시전 중 사망하면 취소된다
	SleepMs(1000);
	z.damages.clear();
	const int cancelsBefore = z.castCancelled;
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); });
	SleepMs(40);
	RunFx(z, [&] { z.DealDamage(*caster, nullptr, 100000); });
	CHECK(z.castCancelled == cancelsBefore + 1 && z.lastCancel == SkillCancelReason::Died);
	SleepMs(300);
	CHECK(z.casts == 2 && z.Damages(2) == 0);
	RunFx(z, [&] { z.Respawn(1); });

	// 완료 시점에 대상이 사거리 밖이면 취소, 사망해도 취소
	SleepMs(1000);
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); });
	SleepMs(40);
	RunFx(z, [&] { z.Teleport(2, Vec2{ 400, 300 }); });
	SleepMs(300);
	CHECK(z.lastCancel == SkillCancelReason::OutOfRange && z.casts == 2);
	RunFx(z, [&] { z.Teleport(2, Vec2{ 310, 300 }); });
	SleepMs(1000);
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); });
	SleepMs(40);
	RunFx(z, [&] { z.DealDamage(*target, nullptr, 100000); });
	SleepMs(300);
	CHECK(z.lastCancel == SkillCancelReason::InvalidTarget && z.casts == 2);

	// 시전 중 퇴장 -> 타이머가 남아도 안전하고 효과는 없다
	RunFx(z, [&] { z.Respawn(2); });
	SleepMs(1000);
	z.damages.clear();
	const int castsBefore = z.casts;
	RunFx(z, [&] { r = z.CastSkill(*caster, skill, 2, {}, ready); });
	SleepMs(40);
	bool removed = false;
	RunFx(z, [&] { removed = z.RemoveEntity(1); });
	SleepMs(300);
	CHECK(removed && z.casts == castsBefore && z.damages.empty());
	z.Stop();
}

static void TestProjectile()
{
	std::printf("[Skill projectile]\n");
	FxZone z(Cfg());
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	CZoneEntity* target = AddFx(z, 2, EntityType::Monster, { 320, 300 }, Stats(1000, 5, 10));
	z.Start();

	SSkillTemplate bolt = Single(1);
	bolt.projectileSpeed = 50.0f;							// 거리 20 -> 400ms
	bolt.cooldownMs = 500;
	uint64 ready = 0;
	SkillResult r{};
	RunFx(z, [&] { r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	CHECK(r == SkillResult::Ok && z.casts == 1 && z.lastHitCount == 0 && z.launched == 1 && z.impacts == 0);
	CHECK(z.lastFlight >= 390 && z.lastFlight <= 410 && z.damages.empty());

	SleepMs(200);
	CHECK(z.impacts == 0 && z.damages.empty());			// 아직 날아가는 중
	SleepMs(400);
	CHECK(z.impacts == 1 && z.lastImpactHits == 1 && z.Damages(2) == 1 && z.damages[0].second == 25);

	// 대상은 추적된다: 날아가는 동안 움직여도 맞고, 착탄 지점은 대상의 위치
	SleepMs(300);
	z.damages.clear();
	RunFx(z, [&] { r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	SleepMs(100);
	RunFx(z, [&] { z.Teleport(2, Vec2{ 322, 300 }); });
	SleepMs(500);
	CHECK(z.impacts == 2 && z.lastImpactHits == 1 && z.lastImpact.x == 322.0f && z.Damages(2) == 1);

	// 날아가는 동안 대상이 죽으면 빗나간다 (착탄 알림은 적중 0)
	SleepMs(300);
	z.damages.clear();
	RunFx(z, [&] { z.Teleport(2, Vec2{ 320, 300 }); r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	SleepMs(100);
	RunFx(z, [&] { z.DealDamage(*target, nullptr, 100000); });
	z.damages.clear();
	SleepMs(500);
	CHECK(z.impacts == 3 && z.lastImpactHits == 0 && z.damages.empty());
	RunFx(z, [&] { z.Respawn(2); });

	// 범위 투사체: 발사 지점에서 터지며, 그 사이 들어온 대상은 맞고 빠져나간 대상은 안 맞는다
	SleepMs(300);
	z.damages.clear();
	RunFx(z, [&] {
		AddFx(z, 3, EntityType::Monster, { 340, 300 }, Stats(1000, 5, 0));		// 폭심(340,300) 근처
		AddFx(z, 4, EntityType::Monster, { 342, 300 }, Stats(1000, 5, 0)); });
	SSkillTemplate bomb = Area(2, 8); bomb.projectileSpeed = 100.0f; bomb.range = 50.0f; bomb.cooldownMs = 500;	// 거리 40 -> 400ms
	RunFx(z, [&] { r = z.CastSkill(*caster, bomb, 0, Vec2{ 340, 300 }, ready); });
	CHECK(r == SkillResult::Ok);
	SleepMs(100);
	RunFx(z, [&] { z.Teleport(4, Vec2{ 380, 300 }); z.Teleport(2, Vec2{ 341, 301 }); });		// 4는 빠져나가고 2가 들어온다
	SleepMs(500);
	CHECK(z.lastImpact.x == 340.0f && z.Damages(3) == 1 && z.Damages(4) == 0 && z.Damages(2) == 1);

	// 비행 중 시전자가 퇴장하면 사라진다(알림 없음)
	SleepMs(300);
	z.damages.clear();
	const int impactsBefore = z.impacts;
	RunFx(z, [&] { z.Teleport(2, Vec2{ 320, 300 }); r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	SleepMs(100);
	RunFx(z, [&] { z.RemoveEntity(1); });
	SleepMs(500);
	CHECK(z.impacts == impactsBefore && z.damages.empty());
	z.Stop();
}

static void TestProjectileWall()
{
	std::printf("[Skill projectile wall]\n");
	auto bm = std::make_shared<CBlockMap>();
	CHECK(bm->Init(0, 0, 1000, 1000, 1.0f));
	bm->AddRect(330, 0, 2, 400);				// x=330..332 세로 벽

	FieldZoneConfig cfg = Cfg(); cfg.blockMap = bm;
	FxZone z(cfg);
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	AddFx(z, 2, EntityType::Monster, { 320, 300 }, Stats(1000, 5, 0));
	z.Start();

	SSkillTemplate bolt = Single(1); bolt.projectileSpeed = 50.0f; bolt.cooldownMs = 500;
	uint64 ready = 0;
	SkillResult r{};

	// 막힘 없이 날아가면 맞는다
	RunFx(z, [&] { r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	SleepMs(600);
	CHECK(r == SkillResult::Ok && z.impacts == 1 && z.lastImpactHits == 1);

	// 날아가는 동안 대상이 벽 건너편으로 옮겨가면 발사 지점과의 사이가 막혀 빗나간다
	SleepMs(300);
	z.damages.clear();
	RunFx(z, [&] { r = z.CastSkill(*caster, bolt, 2, {}, ready); });
	SleepMs(100);
	RunFx(z, [&] { z.Teleport(2, Vec2{ 340, 300 }); });
	SleepMs(600);
	CHECK(z.impacts == 2 && z.lastImpactHits == 0 && z.damages.empty());
	z.Stop();
}

static void TestStatus()
{
	std::printf("[Skill status]\n");
	FxZone z(Cfg());
	z.statuses[1] = Dot(1, 1000, 200, 50);					// 시전자 공격력 20 -> 틱당 10
	z.statuses[2] = Buff(2, 600, 50, -50, 50);
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 4), 6.0f);
	CZoneEntity* target = AddFx(z, 2, EntityType::Monster, { 310, 300 }, Stats(1000, 5, 0));
	CZoneEntity* weak = AddFx(z, 3, EntityType::Monster, { 305, 300 }, Stats(30, 5, 0));
	z.Start();

	// Dot: 스킬 피해 1회 + 틱 5회(200~1000ms). 지속시간이 끝나면 해제된다.
	SSkillTemplate poison = Single(1); poison.statusId = 1;
	uint64 ready = 0;
	SkillResult r{};
	RunFx(z, [&] { r = z.CastSkill(*caster, poison, 2, {}, ready); });
	CHECK(r == SkillResult::Ok && z.applied == 1);
	bool has = false; size_t n = 0;
	RunFx(z, [&] { has = target->HasStatus(1); n = target->GetStatusCount(); });
	CHECK(has && n == 1);
	SleepMs(1400);
	RunFx(z, [&] { has = target->HasStatus(1); n = target->GetStatusCount(); });
	CHECK(!has && n == 0 && z.endedExpired == 1);
	int dotTicks = 0;
	for( size_t i = 0; i < z.damages.size(); ++i )
		if( z.damages[i].first == 2 && z.damages[i].second == 10 && z.damageSources[i] == 1 ) ++dotTicks;
	CHECK(dotTicks == 5 && z.Damages(2) == 6);

	// 같은 효과를 다시 걸면 중첩하지 않고 갱신한다
	SStatusTemplate dot = z.statuses[1];
	RunFx(z, [&] { z.ApplyStatus(*target, dot, caster); z.ApplyStatus(*target, dot, caster); n = target->GetStatusCount(); });
	CHECK(n == 1 && z.applied == 3);

	// Dot으로 죽으면 막타는 건 쪽으로 기록되고 효과는 사망 사유로 풀린다
	const int endedDiedBefore = z.endedDied;
	RunFx(z, [&] { z.ApplyStatus(*weak, dot, caster); });
	SleepMs(800);
	bool dead = false;
	RunFx(z, [&] { dead = !weak->IsAlive(); n = weak->GetStatusCount(); });
	CHECK(dead && n == 0 && z.lastKiller == 1 && z.endedDied == endedDiedBefore + 1);
	const int tickDamageAfterDeath = z.Damages(3);
	SleepMs(500);
	CHECK(z.Damages(3) == tickDamageAfterDeath);			// 죽은 뒤에는 더 들어가지 않는다
	bool applyDead = true;
	RunFx(z, [&] { applyDead = z.ApplyStatus(*weak, dot, caster); });
	CHECK(!applyDead);

	// Buff: 공격력 +50%, 방어력 -50%, 속도 +50%. 끝나면 원래대로.
	SStatusTemplate buff = z.statuses[2];
	int32 atk = 0, def = 0; float spd = 0;
	RunFx(z, [&] { z.ApplyStatus(*caster, buff, caster); atk = caster->GetAttack(); def = caster->GetDefense(); spd = caster->GetSpeed(); });
	CHECK(atk == 30 && def == 2 && std::fabs(spd - 9.0f) < 1e-4f);
	z.damages.clear(); z.damageSources.clear();
	AttackResult ar{};
	RunFx(z, [&] { z.Teleport(2, Vec2{ 301, 300 }); ar = z.RequestAttack(*caster, 2); });		// 기본 공격 사거리(2) 안으로
	bool boosted = false;
	for( size_t i = 0; i < z.damages.size(); ++i )
		if( z.damages[i].first == 2 && z.damageSources[i] == 1 && z.damages[i].second == 30 ) boosted = true;		// (대상에 걸린 Dot 틱은 10이라 구분된다)
	CHECK(ar == AttackResult::Ok && boosted);		// 기본 공격도 보정된 공격력
	SleepMs(900);
	RunFx(z, [&] { atk = caster->GetAttack(); def = caster->GetDefense(); spd = caster->GetSpeed(); });
	CHECK(atk == 20 && def == 4 && std::fabs(spd - 6.0f) < 1e-4f);

	// 합산과 상한/하한
	SStatusTemplate a = Buff(10, 5000, 400, 0, 0), b = Buff(11, 5000, 400, 0, 0), c = Buff(12, 5000, 0, -80, 0), d2 = Buff(13, 5000, 0, -80, 0);
	RunFx(z, [&] { z.ApplyStatus(*caster, a, caster); z.ApplyStatus(*caster, b, caster); z.ApplyStatus(*caster, c, caster); z.ApplyStatus(*caster, d2, caster);
		atk = caster->GetAttack(); def = caster->GetDefense(); });
	CHECK(atk == 120 && def == 0);							// +500% 상한 -> 20*6, -90% -> 4*0.1 = 0 (정수)

	// 엔티티당 상한
	int accepted = 0;
	RunFx(z, [&] { for( uint32 i = 20; i < 20 + kMaxStatusesPerEntity + 2; ++i ) if( z.ApplyStatus(*caster, Buff(i, 5000, 1, 0, 0), caster) ) ++accepted; n = caster->GetStatusCount(); });
	CHECK(n == kMaxStatusesPerEntity && accepted == static_cast<int>(kMaxStatusesPerEntity) - 4);

	// 사망하면 모두 풀리고 능력치도 원래대로
	RunFx(z, [&] { z.DealDamage(*caster, nullptr, 100000); n = caster->GetStatusCount(); atk = caster->GetAttack(); spd = caster->GetSpeed(); });
	CHECK(n == 0 && atk == 20 && std::fabs(spd - 6.0f) < 1e-4f);
	z.Stop();
}

static void TestStatusHolders()
{
	std::printf("[Skill status holders]\n");
	FxZone z(Cfg());
	z.statuses[1] = Dot(1, 600, 200, 100);
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 10, 0));
	CZoneEntity* e1 = AddFx(z, 11, EntityType::Monster, { 310, 300 }, Stats(1000, 1, 0));
	CZoneEntity* e2 = AddFx(z, 12, EntityType::Monster, { 311, 300 }, Stats(1000, 1, 0));
	CZoneEntity* e3 = AddFx(z, 13, EntityType::Monster, { 312, 300 }, Stats(1000, 1, 0));
	z.Start();

	// 보유 목록의 가운데를 제거해도 남은 둘의 틱이 계속 돈다 (swap-pop 색인 점검)
	RunFx(z, [&] { z.ApplyStatus(*e1, z.statuses[1], caster); z.ApplyStatus(*e2, z.statuses[1], caster); z.ApplyStatus(*e3, z.statuses[1], caster); });
	SleepMs(100);
	bool removed = false;
	RunFx(z, [&] { removed = z.RemoveEntity(12); });
	SleepMs(900);
	CHECK(removed && z.Damages(11) == 3 && z.Damages(13) == 3 && z.Damages(12) == 0);
	size_t n1 = 1, n3 = 1;
	RunFx(z, [&] { n1 = e1->GetStatusCount(); n3 = e3->GetStatusCount(); });
	CHECK(n1 == 0 && n3 == 0);

	// 먼저 끝나는 순서가 보유 목록의 앞쪽부터가 아니어도, 남은 엔티티의 틱이 끊기지 않는다 (swap-pop으로 옮겨간 엔티티의 색인 점검)
	z.damages.clear(); z.damageSources.clear();
	z.statuses[2] = Dot(2, 300, 100, 100);
	z.statuses[3] = Dot(3, 500, 100, 100);
	z.statuses[4] = Dot(4, 900, 100, 100);
	RunFx(z, [&] { z.ApplyStatus(*e1, z.statuses[2], caster); z.ApplyStatus(*e3, z.statuses[4], caster); z.ApplyStatus(*caster, z.statuses[3], caster); });
	SleepMs(1300);
	CHECK(z.Damages(11) == 3 && z.Damages(13) == 9 && z.Damages(1) == 5);

	// 제거 후 재투입한 같은 ID에는 이전 효과가 남아 있지 않다
	size_t n = 9;
	RunFx(z, [&] { CZoneEntity* again = AddFx(z, 12, EntityType::Monster, { 311, 300 }, Stats(1000, 1, 0)); n = again->GetStatusCount(); });
	CHECK(n == 0);
	z.Stop();
}

static void TestSkillWithStatus()
{
	std::printf("[Skill with status]\n");
	FxZone z(Cfg());
	z.statuses[2] = Buff(2, 2000, 50, 0, 0);
	z.statuses[3] = Buff(3, 2000, 0, 0, -50);
	z.statuses[3].target = EStatusTarget::Hit;
	z.statuses[2].target = EStatusTarget::Self;
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	CZoneEntity* near1 = AddFx(z, 10, EntityType::Monster, { 320, 300 }, Stats(1000, 5, 0));
	CZoneEntity* near2 = AddFx(z, 11, EntityType::Monster, { 322, 300 }, Stats(1000, 5, 0));
	CZoneEntity* far1 = AddFx(z, 12, EntityType::Monster, { 340, 300 }, Stats(1000, 5, 0));
	z.Start();

	// 자기 버프: 맞은 대상이 없어도 시전자에게 걸린다
	SSkillTemplate rally = Area(1, 8); rally.damagePercent = 0; rally.flatDamage = 0; rally.statusId = 2;
	uint64 ready = 0;
	SkillResult r{};
	int32 atk = 0;
	RunFx(z, [&] { r = z.CastSkill(*caster, rally, 0, Vec2{ 300, 320 }, ready); atk = caster->GetAttack(); });
	CHECK(r == SkillResult::Ok && z.lastHitCount == 0 && atk == 30 && z.damages.empty());

	// 피해 없는 디버프 범위 스킬: 맞은 대상에게만 걸리고 피해는 없다(최소 피해도 들어가지 않는다)
	SleepMs(40);
	SSkillTemplate slow = Area(2, 8); slow.damagePercent = 0; slow.flatDamage = 0; slow.statusId = 3;
	uint64 ready2 = 0;
	float s1 = 0, s2 = 0, s3 = 0;
	RunFx(z, [&] { r = z.CastSkill(*caster, slow, 0, Vec2{ 321, 300 }, ready2); s1 = near1->GetSpeed(); s2 = near2->GetSpeed(); s3 = far1->GetSpeed(); });
	CHECK(r == SkillResult::Ok && z.lastHitCount == 2 && z.damages.empty());
	CHECK(std::fabs(s1 - 3.0f) < 1e-4f && std::fabs(s2 - 3.0f) < 1e-4f && std::fabs(s3 - 6.0f) < 1e-4f);

	// 시전 시간 + 투사체 + 상태 효과 조합
	SleepMs(40);
	z.damages.clear();
	RunFx(z, [&] { z.Teleport(12, Vec2{ 318, 300 }); });
	SSkillTemplate combo = Single(3); combo.castTimeMs = 100; combo.projectileSpeed = 100.0f; combo.statusId = 3; combo.cooldownMs = 500;
	uint64 ready3 = 0;
	RunFx(z, [&] { r = z.CastSkill(*caster, combo, 12, {}, ready3); });
	CHECK(r == SkillResult::Ok && z.launched == 0);
	SleepMs(900);
	float s4 = 0;
	RunFx(z, [&] { s4 = far1->GetSpeed(); });
	CHECK(z.launched == 1 && z.impacts == 1 && z.lastImpactHits == 1 && z.Damages(12) == 1 && std::fabs(s4 - 3.0f) < 1e-4f);
	z.Stop();
}

// 무작위 연산: 시전/이동/순간이동/피해/제거·재투입/상태 효과를 섞어 돌리며 불변식을 확인한다 (ASan/UBSan과 함께 쓰는 것이 목적)
// ── 확장 스킬: 모양(Self/Cone/Line), 아군 치유, 기절/보호막/지속 치유 ──────────────────

static FieldZoneConfig NoPvpCfg()
{
	FieldZoneConfig f = Cfg();
	f.allowPvp = false;		// 플레이어끼리는 같은 편
	return f;
}

static SSkillTemplate Shaped(uint32 id, ESkillShape shape)
{
	SSkillTemplate s; s.id = id; s.name = "shaped"; s.shape = shape; s.damagePercent = 100; s.cooldownMs = 100; s.maxTargets = 8;
	switch( shape )
	{
	case ESkillShape::Self: s.range = 0.0f; s.radius = 6.0f; break;
	case ESkillShape::Cone: s.range = 10.0f; s.radius = 0.0f; s.angle = 90.0f; break;
	case ESkillShape::Line: s.range = 12.0f; s.radius = 1.5f; break;
	default: break;
	}
	return s;
}

static SSkillTemplate HealSkill(uint32 id, ESkillShape shape)
{
	SSkillTemplate s = Shaped(id, shape == ESkillShape::Single ? ESkillShape::Self : shape);
	s.shape = shape; s.target = ESkillTarget::Ally; s.damagePercent = 0; s.healPercent = 200; s.flatHeal = 5;
	if( shape == ESkillShape::Single ) { s.range = 20.0f; s.radius = 0.0f; s.maxTargets = 1; }
	return s;
}

static SStatusTemplate Stun(uint32 id, uint32 durationMs)
{
	SStatusTemplate t; t.id = id; t.name = "stun"; t.kind = EStatusKind::Stun; t.durationMs = durationMs; t.tickMs = 0;
	return t;
}

static SStatusTemplate Shield(uint32 id, uint32 durationMs, uint32 pct, int32 flat)
{
	SStatusTemplate t; t.id = id; t.name = "shield"; t.kind = EStatusKind::Shield; t.durationMs = durationMs; t.tickMs = 0; t.healPercent = pct; t.flatHeal = flat;
	return t;
}

static SStatusTemplate Hot(uint32 id, uint32 durationMs, uint32 tickMs, uint32 pct, int32 flat = 0)
{
	SStatusTemplate t; t.id = id; t.name = "hot"; t.kind = EStatusKind::Hot; t.durationMs = durationMs; t.tickMs = tickMs; t.healPercent = pct; t.flatHeal = flat;
	return t;
}

static void TestExtData()
{
	std::printf("[Skill extension data]\n");
	CHECK(CalcSkillHeal(HealSkill(1, ESkillShape::Single), 20) == 45);			// 20*200/100 + 5
	CHECK(CalcSkillHeal(HealSkill(1, ESkillShape::Single), INT32_MAX) == INT32_MAX);
	CHECK(CalcStatusAmount(Hot(1, 1000, 200, 0, 0), 20) == 1);					// 최소 1
	CHECK(CalcStatusAmount(Shield(1, 1000, 50, 3), 20) == 13);

	auto make = [](const SSkillTemplate& sk, const SStatusTemplate* st)
	{
		auto d = std::make_shared<CHeroData>();
		SHeroTemplate h; h.id = 1; h.name = "h"; d->AddHero(h);
		if( st ) CHECK(d->AddStatus(*st));
		CHECK(d->AddSkill(sk));
		return d;
	};
	std::string err;
	CHECK(make(Shaped(1, ESkillShape::Self), nullptr)->Validate(err));
	CHECK(make(Shaped(1, ESkillShape::Cone), nullptr)->Validate(err));
	CHECK(make(Shaped(1, ESkillShape::Line), nullptr)->Validate(err));
	CHECK(make(HealSkill(1, ESkillShape::Single), nullptr)->Validate(err));
	CHECK(make(HealSkill(1, ESkillShape::Self), nullptr)->Validate(err));

	SSkillTemplate bad = Shaped(1, ESkillShape::Self); bad.range = 3.0f;			CHECK(!make(bad, nullptr)->Validate(err));	// Self는 range 0
	bad = Shaped(1, ESkillShape::Self); bad.radius = 0.0f;							CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Cone); bad.angle = 0.0f;							CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Cone); bad.angle = 361.0f;						CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Cone); bad.angle = NAN;							CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Cone); bad.radius = 2.0f;							CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Line); bad.angle = 30.0f;							CHECK(!make(bad, nullptr)->Validate(err));	// angle은 Cone만
	bad = Shaped(1, ESkillShape::Line); bad.radius = 0.0f;							CHECK(!make(bad, nullptr)->Validate(err));
	bad = Shaped(1, ESkillShape::Cone); bad.projectileSpeed = 30.0f;				CHECK(!make(bad, nullptr)->Validate(err));	// 투사체는 Single/Area만
	bad = Shaped(1, ESkillShape::Line); bad.maxTargets = 0;						CHECK(!make(bad, nullptr)->Validate(err));
	bad = HealSkill(1, ESkillShape::Single); bad.target = ESkillTarget::Enemy;		CHECK(!make(bad, nullptr)->Validate(err));	// 치유는 Ally만
	bad = HealSkill(1, ESkillShape::Single); bad.damagePercent = 10;				CHECK(!make(bad, nullptr)->Validate(err));	// Ally는 피해 불가
	bad = HealSkill(1, ESkillShape::Single); bad.healPercent = 0; bad.flatHeal = 0;	CHECK(!make(bad, nullptr)->Validate(err));	// 효과 없음
	bad = HealSkill(1, ESkillShape::Single); bad.healPercent = kMaxSkillDamagePercent + 1;	CHECK(!make(bad, nullptr)->Validate(err));

	auto badStatus = [&](const SStatusTemplate& t) { auto d = make(Single(1), &t); return d->Validate(err); };
	CHECK(badStatus(Stun(5, 1000)) && badStatus(Shield(5, 1000, 50, 0)) && badStatus(Hot(5, 1000, 200, 10)));
	SStatusTemplate t = Stun(5, 1000); t.tickMs = 100;								CHECK(!badStatus(t));
	t = Stun(5, 1000); t.speedPercent = -10;										CHECK(!badStatus(t));
	t = Stun(5, 1000); t.healPercent = 10;											CHECK(!badStatus(t));
	CHECK(!badStatus(Shield(5, 1000, 0, 0)));										// 흡수량 없음
	t = Shield(5, 1000, 10, 0); t.tickMs = 200;									CHECK(!badStatus(t));
	CHECK(!badStatus(Hot(5, 1000, 50, 10)));										// 틱이 너무 짧다
	CHECK(!badStatus(Hot(5, 1000, 2000, 10)));										// 틱이 지속시간보다 길다
	CHECK(!badStatus(Hot(5, 1000, 200, 0, 0)));									// 회복량 없음
	t = Hot(5, 1000, 200, 10); t.damagePercent = 5;								CHECK(!badStatus(t));
	t = Buff(5, 1000, 10, 0, 0); t.healPercent = 10;								CHECK(!badStatus(t));	// Hot/Shield만 amount를 쓴다
}

static void TestShapes()
{
	std::printf("[Skill shapes]\n");
	FxZone z(NoPvpCfg());
	CZoneEntity* caster = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	AddFx(z, 10, EntityType::Monster, { 305, 300 }, Stats(1000, 5, 0));		// 정면 5
	AddFx(z, 11, EntityType::Monster, { 308, 304 }, Stats(1000, 5, 0));		// 정면에서 약 26도 위, 거리 8.9
	AddFx(z, 12, EntityType::Monster, { 300, 306 }, Stats(1000, 5, 0));		// 위쪽 90도 (부채꼴 밖), 선 밖
	AddFx(z, 13, EntityType::Monster, { 295, 300 }, Stats(1000, 5, 0));		// 뒤쪽
	AddFx(z, 14, EntityType::Monster, { 311, 300 }, Stats(1000, 5, 0));		// 정면 11 (부채꼴 길이 10 밖, 직선 12 안)
	AddFx(z, 15, EntityType::Monster, { 310, 302 }, Stats(1000, 5, 0));		// 선 옆 2 (폭 1.5 밖)
	AddFx(z, 16, EntityType::Monster, { 340, 300 }, Stats(1000, 5, 0));		// 아주 멀다
	AddFx(z, 20, EntityType::Player, { 303, 300 }, Stats(1000, 5, 0));			// 같은 편(PvP 금지)
	z.Start();

	uint64 ready = 0;
	SkillResult r{};
	auto hitIds = [&]
	{
		std::set<uint64> s;
		for( const auto& d : z.damages ) s.insert(d.first);
		return s;
	};
	const std::set<uint64> none;

	// Self: 시전자 위치 중심 반경 6. 좌표는 무시되고 사거리 검사도 없다. 시전자 본인과 같은 편은 맞지 않는다.
	SSkillTemplate self = Shaped(1, ESkillShape::Self);
	RunFx(z, [&] { r = z.CastSkill(*caster, self, 0, Vec2{ 999, 999 }, ready); });
	CHECK(r == SkillResult::Ok && z.lastHitCount == 3);
	CHECK((hitIds() == std::set<uint64>{ 10, 12, 13 }));

	// Cone: +x 방향 90도(좌우 45도), 길이 10. 정면 5, 26도 8.9만 맞는다.
	SleepMs(150);
	z.damages.clear();
	SSkillTemplate cone = Shaped(2, ESkillShape::Cone);
	uint64 readyC = 0;
	RunFx(z, [&] { r = z.CastSkill(*caster, cone, 0, Vec2{ 330, 300 }, readyC); });
	CHECK(r == SkillResult::Ok);
	CHECK((hitIds() == std::set<uint64>{ 10, 11 }));
	CHECK(z.lastHitCount == 2);

	// 방향만 보므로 멀리 찍든 가까이 찍든 같다. 위쪽(+y)으로 돌리면 12만 맞는다.
	SleepMs(150);
	z.damages.clear();
	RunFx(z, [&] { r = z.CastSkill(*caster, cone, 0, Vec2{ 300, 301 }, readyC); });
	CHECK(r == SkillResult::Ok && (hitIds() == std::set<uint64>{ 12 }));

	// 360도 부채꼴은 뒤쪽까지 포함하지만 길이 밖은 제외한다
	SleepMs(150);
	z.damages.clear();
	SSkillTemplate wide = cone; wide.angle = 360.0f;
	RunFx(z, [&] { r = z.CastSkill(*caster, wide, 0, Vec2{ 330, 300 }, readyC); });
	CHECK(r == SkillResult::Ok && (hitIds() == std::set<uint64>{ 10, 11, 12, 13 }));

	// 방향이 없는 좌표(시전자 위치)는 거부된다
	SleepMs(150);
	RunFx(z, [&] { r = z.CastSkill(*caster, cone, 0, Vec2{ 300, 300 }, readyC); });
	CHECK(r == SkillResult::InvalidTarget && z.lastReject == SkillResult::InvalidTarget);
	RunFx(z, [&] { r = z.CastSkill(*caster, cone, 0, Vec2{ NAN, 300 }, readyC); });
	CHECK(r == SkillResult::OutOfRange);
	RunFx(z, [&] { r = z.CastSkill(*caster, cone, 0, Vec2{ -50, 300 }, readyC); });
	CHECK(r == SkillResult::OutOfRange);						// 영역 밖

	// Line: +x 방향 길이 12, 폭 좌우 1.5. 10(5), 14(11)만 맞고 15(옆 2), 13(뒤)은 아니다.
	SleepMs(150);
	z.damages.clear();
	SSkillTemplate line = Shaped(3, ESkillShape::Line);
	uint64 readyL = 0;
	RunFx(z, [&] { r = z.CastSkill(*caster, line, 0, Vec2{ 301, 300 }, readyL); });
	CHECK(r == SkillResult::Ok && (hitIds() == std::set<uint64>{ 10, 14 }));

	// maxTargets: 가까운 순으로 자른다
	SleepMs(150);
	z.damages.clear();
	SSkillTemplate few = self; few.maxTargets = 2;
	RunFx(z, [&] { r = z.CastSkill(*caster, few, 0, {}, ready); });
	CHECK(r == SkillResult::Ok && z.lastHitCount == 2 && (hitIds() == std::set<uint64>{ 10, 13 }));	// 거리 5가 같으면 ID 순
	z.Stop();

	// 벽 뒤는 맞지 않는다 (Self/Cone/Line은 시전자 위치에서 판정)
	auto bm = std::make_shared<CBlockMap>();
	CHECK(bm->Init(0, 0, 1000, 1000, 1.0f));
	bm->AddRect(303, 290, 1, 20);
	FieldZoneConfig cfg = NoPvpCfg(); cfg.blockMap = bm;
	FxZone w(cfg);
	CZoneEntity* c2 = AddFx(w, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	AddFx(w, 10, EntityType::Monster, { 305, 300 }, Stats(1000, 5, 0));
	AddFx(w, 11, EntityType::Monster, { 296, 300 }, Stats(1000, 5, 0));
	w.Start();
	uint64 rw = 0;
	RunFx(w, [&] { r = w.CastSkill(*c2, self, 0, {}, rw); });
	CHECK(r == SkillResult::Ok && w.Damages(10) == 0 && w.Damages(11) == 1);
	SleepMs(150);
	RunFx(w, [&] { r = w.CastSkill(*c2, line, 0, Vec2{ 330, 300 }, rw); });
	CHECK(r == SkillResult::Ok && w.Damages(10) == 0);
	w.Stop();

	// PvP가 켜지면 같은 편이 아니라 적이다
	FieldZoneConfig pvp = Cfg(); pvp.allowPvp = true;
	FxZone p(pvp);
	CZoneEntity* a = AddFx(p, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	AddFx(p, 2, EntityType::Player, { 303, 300 }, Stats(1000, 5, 0));
	p.Start();
	uint64 rp = 0;
	RunFx(p, [&] { r = p.CastSkill(*a, self, 0, {}, rp); });
	CHECK(r == SkillResult::Ok && p.Damages(2) == 1 && p.Damages(1) == 0);
	p.Stop();
}

static void TestHeal()
{
	std::printf("[Skill heal]\n");
	FxZone z(NoPvpCfg());
	z.statuses[7] = Hot(7, 1000, 200, 0, 3);
	CZoneEntity* healer = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(100, 20, 0));
	CZoneEntity* ally = AddFx(z, 2, EntityType::Player, { 310, 300 }, Stats(100, 5, 0));
	CZoneEntity* foe = AddFx(z, 3, EntityType::Monster, { 305, 300 }, Stats(100, 5, 0));
	CZoneEntity* ally2 = AddFx(z, 4, EntityType::Player, { 304, 301 }, Stats(100, 5, 0));
	z.Start();

	uint64 ready = 0;
	SkillResult r{};
	RunFx(z, [&] { z.DealDamage(*ally, nullptr, 70); z.DealDamage(*healer, nullptr, 10); z.DealDamage(*ally2, nullptr, 50); });
	z.damages.clear();

	// 단일 치유: 20*200/100+5 = 45. 최대 체력을 넘지 않는다.
	SSkillTemplate heal = HealSkill(1, ESkillShape::Single);
	RunFx(z, [&] { r = z.CastSkill(*healer, heal, 2, {}, ready); });
	int hp = 0;
	RunFx(z, [&] { hp = ally->GetHp(); });
	CHECK(r == SkillResult::Ok && hp == 75 && z.lastHitCount == 1 && z.damages.empty());
	CHECK(z.heals.size() == 1 && z.heals[0].first == 2 && z.heals[0].second == 45);

	// 자신도 대상이 될 수 있다. 가득 차지 않은 만큼만 회복(실제 회복량 10).
	SleepMs(150);
	RunFx(z, [&] { r = z.CastSkill(*healer, heal, 1, {}, ready); hp = healer->GetHp(); });
	CHECK(r == SkillResult::Ok && hp == 100 && z.heals.size() == 2 && z.heals[1].second == 10);

	// 이미 가득 찬 대상에게는 회복 알림이 없다. 시전은 성공한다.
	SleepMs(150);
	RunFx(z, [&] { r = z.CastSkill(*healer, heal, 1, {}, ready); });
	CHECK(r == SkillResult::Ok && z.heals.size() == 2);

	// 적(몬스터)은 치유 대상이 아니다. 거부는 InvalidTarget이며 재사용 대기를 쓰지 않는다.
	SleepMs(150);
	uint64 before = ready;
	RunFx(z, [&] { r = z.CastSkill(*healer, heal, 3, {}, ready); });
	CHECK(r == SkillResult::InvalidTarget && ready == before);

	// 죽은 아군은 치유할 수 없다
	RunFx(z, [&] { z.DealDamage(*ally2, nullptr, 1000); });
	SleepMs(150);
	RunFx(z, [&] { r = z.CastSkill(*healer, heal, 4, {}, ready); });
	CHECK(r == SkillResult::InvalidTarget);

	// 범위 치유: 자기 중심 반경 6. 시전자 본인과 반경 안 아군만(적/사망자 제외). 상태 효과(Hot)도 같이 건다.
	RunFx(z, [&] { z.DealDamage(*ally, nullptr, 60); z.DealDamage(*healer, nullptr, 60); });
	z.heals.clear();
	SSkillTemplate aoe = HealSkill(2, ESkillShape::Self);
	aoe.radius = 12.0f; aoe.statusId = 7;
	SleepMs(150);
	RunFx(z, [&] { r = z.CastSkill(*healer, aoe, 0, {}, ready); });
	CHECK(r == SkillResult::Ok && z.lastHitCount == 2);			// 시전자 + 아군 2
	std::set<uint64> healed; for( const auto& h : z.heals ) healed.insert(h.first);
	CHECK((healed == std::set<uint64>{ 1, 2 }));
	CHECK(z.applied == 2);										// Hot은 맞은 아군 둘에게
	bool hot1 = false, hot3 = true;
	RunFx(z, [&] { hot1 = healer->HasStatus(7); hot3 = foe->HasStatus(7); });
	CHECK(hot1 && !hot3);

	// Hot: 틱당 3씩, 1000ms 동안 5틱
	z.heals.clear();
	SleepMs(1400);
	int ticks = 0;
	for( const auto& h : z.heals ) if( h.first == 1 && h.second == 3 ) ++ticks;
	CHECK(ticks == 5);
	bool still = true;
	RunFx(z, [&] { still = healer->HasStatus(7); });
	CHECK(!still && z.endedExpired == 2);
	z.Stop();
}

static void TestStunStatus()
{
	std::printf("[Skill stun]\n");
	FxZone z(Cfg());
	z.statuses[1] = Stun(1, 500);
	CZoneEntity* a = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	CZoneEntity* m = AddFx(z, 2, EntityType::Monster, { 301, 300 }, Stats(1000, 5, 0));
	z.Start();

	SSkillTemplate stunSkill = Single(1); stunSkill.statusId = 1; stunSkill.cooldownMs = 100;
	uint64 ready = 0, readyM = 0;
	SkillResult r{};
	RunFx(z, [&] { r = z.CastSkill(*a, stunSkill, 2, {}, ready); });
	CHECK(r == SkillResult::Ok);

	// 기절한 동안: 이동, 기본 공격, 스킬이 모두 막힌다
	MoveResult mr{}; AttackResult ar{}; SkillResult sr{};
	bool stunned = false;
	RunFx(z, [&] {
		stunned = z.IsStunned(*m);
		mr = z.RequestMove(*m, m->GetPos(), Vec2{ 320, 300 });
		SSkillTemplate s = Single(2);
		sr = z.CastSkill(*m, s, 1, {}, readyM);
	});
	CHECK(stunned && mr == MoveResult::Stunned && sr == SkillResult::Stunned && z.lastReject == SkillResult::Stunned);
	SleepMs(30);
	RunFx(z, [&] { ar = z.RequestAttack(*m, 1); });
	CHECK(ar == AttackResult::Stunned);
	bool moved = true;
	RunFx(z, [&] { moved = z.MoveEntity(*m, Vec2{ 320, 300 }); });
	CHECK(!moved);												// 서버 주도 이동도 막힌다

	// 시전자는 영향이 없다. 기절 거부는 재사용 대기를 쓰지 않는다.
	CHECK(readyM == 0);

	// 기절이 끝나면 다시 할 수 있다
	SleepMs(600);
	RunFx(z, [&] { stunned = z.IsStunned(*m); mr = z.RequestMove(*m, m->GetPos(), Vec2{ 320, 300 }); });
	CHECK(!stunned && mr == MoveResult::Ok && z.endedExpired == 1);

	// 걸리는 순간 이동이 멈춘다
	SleepMs(100);
	RunFx(z, [&] { r = z.CastSkill(*a, stunSkill, 2, {}, ready); });
	bool moving = true;
	RunFx(z, [&] { moving = m->IsMoving(); });
	CHECK(r == SkillResult::Ok && !moving);

	// 시전 중에 기절하면 시전이 취소된다. 재사용 대기는 돌려주지 않는다.
	SleepMs(600);
	SSkillTemplate slow = Timed(3, 300);
	bool casting = false;
	RunFx(z, [&] { r = z.CastSkill(*m, slow, 1, {}, readyM); casting = m->IsCasting(); });
	CHECK(r == SkillResult::Ok && casting);
	const int dmgBefore = z.Damages(1);
	RunFx(z, [&] { z.CastSkill(*a, stunSkill, 2, {}, ready); casting = m->IsCasting(); });
	CHECK(!casting && z.castCancelled == 1 && z.lastCancel == SkillCancelReason::Stunned);
	SleepMs(400);
	CHECK(z.Damages(1) == dmgBefore);							// 취소되어 효과가 나가지 않았다

	// 다시 걸면 갱신(중첩 없음)
	size_t n = 0;
	RunFx(z, [&] { z.ApplyStatus(*m, z.statuses[1], a); z.ApplyStatus(*m, z.statuses[1], a); n = m->GetStatusCount(); });
	CHECK(n == 1);

	// 죽은 엔티티는 기절 상태가 아니다
	RunFx(z, [&] { z.DealDamage(*m, a, 100000); stunned = z.IsStunned(*m); });
	CHECK(!stunned);
	z.Stop();
}

static void TestShieldStatus()
{
	std::printf("[Skill shield]\n");
	FxZone z(NoPvpCfg());
	z.statuses[1] = Shield(1, 800, 0, 30);			// 흡수 30
	z.statuses[2] = Shield(2, 800, 0, 20);			// 흡수 20
	CZoneEntity* healer = AddFx(z, 1, EntityType::Player, { 300, 300 }, Stats(100, 20, 0));
	CZoneEntity* ally = AddFx(z, 2, EntityType::Player, { 310, 300 }, Stats(100, 5, 0));
	z.Start();

	// 보호막을 거는 아군 스킬 (피해/치유 없이 상태 효과만)
	SSkillTemplate ward = HealSkill(1, ESkillShape::Single);
	ward.healPercent = 0; ward.flatHeal = 0; ward.statusId = 1;
	uint64 ready = 0;
	SkillResult r{};
	RunFx(z, [&] { r = z.CastSkill(*healer, ward, 2, {}, ready); });
	CHECK(r == SkillResult::Ok && z.applied == 1);

	// 피해 20: 전부 흡수, 체력 그대로, 피해 알림 없음
	int hp = 0;
	bool res = false;
	RunFx(z, [&] { res = z.DealDamage(*ally, nullptr, 20); hp = ally->GetHp(); });
	CHECK(res && hp == 100 && z.damages.empty() && z.endedBroken == 0);

	// 피해 25: 남은 10만 흡수하고 보호막이 깨진다. 체력은 15 깎인다.
	RunFx(z, [&] { res = z.DealDamage(*ally, nullptr, 25); hp = ally->GetHp(); });
	CHECK(res && hp == 85 && z.damages.size() == 1 && z.damages[0].second == 15 && z.endedBroken == 1);
	bool has = true; size_t n = 1;
	RunFx(z, [&] { has = ally->HasStatus(1); n = ally->GetStatusCount(); });
	CHECK(!has && n == 0);

	// 보호막이 둘이면 먼저 걸린 것부터 깎는다
	z.damages.clear();
	RunFx(z, [&] {
		z.ApplyStatus(*ally, z.statuses[1], healer);
		z.ApplyStatus(*ally, z.statuses[2], healer);
		z.DealDamage(*ally, nullptr, 40);						// 30 + 10
		hp = ally->GetHp(); n = ally->GetStatusCount();
	});
	CHECK(hp == 85 && n == 1 && z.endedBroken == 2 && z.damages.empty());
	RunFx(z, [&] { z.DealDamage(*ally, nullptr, 15); hp = ally->GetHp(); n = ally->GetStatusCount(); });	// 남은 10 + 체력 5
	CHECK(hp == 80 && n == 0 && z.endedBroken == 3);

	// 지속시간이 끝난 보호막은 흡수하지 않는다
	RunFx(z, [&] { z.ApplyStatus(*ally, z.statuses[1], healer); });
	SleepMs(900);
	RunFx(z, [&] { z.DealDamage(*ally, nullptr, 10); hp = ally->GetHp(); });
	CHECK(hp == 70 && z.endedExpired == 1);

	// 같은 보호막을 다시 걸면 새로 채워진다(중첩 없음)
	RunFx(z, [&] {
		z.ApplyStatus(*ally, z.statuses[1], healer);
		z.DealDamage(*ally, nullptr, 20);
		z.ApplyStatus(*ally, z.statuses[1], healer);
		z.DealDamage(*ally, nullptr, 25);
		hp = ally->GetHp(); n = ally->GetStatusCount();
	});
	CHECK(hp == 70 && n == 1);
	z.Stop();
}

static void TestEffectFuzz()
{
	std::printf("[Skill effect fuzz]\n");
	FxZone z(Cfg());
	z.statuses[1] = Dot(1, 400, 100, 50);
	z.statuses[2] = Buff(2, 300, 40, -30, 50);
	z.statuses[3] = Buff(3, 500, -20, 20, -40);
	z.statuses[3].target = EStatusTarget::Self;

	constexpr uint64 kEntities = 12;
	for( uint64 id = 1; id <= kEntities; ++id )
		AddFx(z, id, id <= 4 ? EntityType::Player : EntityType::Monster, { 300.0f + static_cast<float>(id) * 2.0f, 300.0f + static_cast<float>(id % 3) }, Stats(120, 10 + static_cast<int>(id), 2));
	z.Start();

	std::vector<SSkillTemplate> skills;
	{ SSkillTemplate s = Single(1); s.statusId = 1; skills.push_back(s); }
	{ SSkillTemplate s = Single(2); s.castTimeMs = 60; s.cooldownMs = 80; skills.push_back(s); }
	{ SSkillTemplate s = Single(3); s.projectileSpeed = 300.0f; s.statusId = 2; s.cooldownMs = 80; skills.push_back(s); }
	{ SSkillTemplate s = Area(4, 6); s.castTimeMs = 40; s.projectileSpeed = 400.0f; s.statusId = 3; s.cooldownMs = 80; skills.push_back(s); }
	{ SSkillTemplate s = Area(5, 8); s.damagePercent = 0; s.statusId = 2; s.cooldownMs = 80; skills.push_back(s); }
	std::vector<uint64> ready(kEntities * skills.size() + 8, 0);

	uint32 rng = 12345;
	auto rnd = [&](uint32 n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; };

	int bad = 0;
	for( int round = 0; round < 400; ++round )
	{
		RunFx(z, [&] {
			for( int op = 0; op < 6; ++op )
			{
				const uint64 id = 1 + rnd(static_cast<uint32>(kEntities));
				CZoneEntity* e = z.FindEntity(id);
				switch( rnd(8) )
				{
				case 0: case 1: case 2:
					if( e != nullptr )
					{
						const uint32 si = rnd(static_cast<uint32>(skills.size()));
						const uint64 target = 1 + rnd(static_cast<uint32>(kEntities));
						z.CastSkill(*e, skills[si], target, Vec2{ 300.0f + static_cast<float>(rnd(30)), 300.0f + static_cast<float>(rnd(6)) }, ready[(id - 1) * skills.size() + si]);
					}
					break;
				case 3:
					if( e != nullptr ) z.RequestMove(*e, e->GetPos(), Vec2{ 280.0f + static_cast<float>(rnd(50)), 295.0f + static_cast<float>(rnd(15)) });
					break;
				case 4:
					if( e != nullptr ) z.Teleport(id, Vec2{ 280.0f + static_cast<float>(rnd(50)), 295.0f + static_cast<float>(rnd(15)) });
					break;
				case 5:
					if( e != nullptr ) z.DealDamage(*e, nullptr, static_cast<int32>(rnd(150)));
					break;
				case 6:
					if( e != nullptr && rnd(4) == 0 ) z.RemoveEntity(id);
					else if( e == nullptr ) AddFx(z, id, id <= 4 ? EntityType::Player : EntityType::Monster, { 300.0f + static_cast<float>(id), 300.0f }, Stats(120, 10, 2));
					break;
				default:
					if( e != nullptr ) z.ApplyStatus(*e, z.statuses[1 + rnd(3)], z.FindEntity(1 + rnd(static_cast<uint32>(kEntities))));
					break;
				}
			}
		});
		SleepMs(5);

		RunFx(z, [&] {
			for( uint64 id = 1; id <= kEntities; ++id )
			{
				CZoneEntity* e = z.FindEntity(id);
				if( e == nullptr ) continue;
				if( e->GetStatusCount() > kMaxStatusesPerEntity ) ++bad;
				if( !e->IsAlive() && (e->IsCasting() || e->GetStatusCount() != 0) ) ++bad;		// 죽으면 시전/상태 효과가 남지 않는다
				if( !(e->GetSpeed() > 0.0f) ) ++bad;												// 이동속도가 0 이하로 뒤집히지 않는다
				if( e->GetAttack() < 0 || e->GetDefense() < 0 ) ++bad;
			}
		});
	}
	CHECK(bad == 0);
	SleepMs(800);		// 남은 시전/비행/지속 효과가 모두 정리되어도 문제가 없다
	RunFx(z, [&] { for( uint64 id = 1; id <= kEntities; ++id ) z.RemoveEntity(id); });
	SleepMs(300);
	z.Stop();
}

static void TestSharedGate()
{
	std::printf("[Skill shared gate]\n");
	SkZone z(Cfg());
	CZoneEntity* caster = Add(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	Add(z, 2, EntityType::Monster, { 301, 300 }, Stats(100000, 5, 0));
	z.Start();

	const SSkillTemplate skill = Single(1);
	uint64 ready = 0;
	SkillResult first{}, rejected{}, bad{};
	AttackResult attack{};
	Run(z, [&] { first = z.CastSkill(*caster, skill, 2, {}, ready); });
	CHECK(first == SkillResult::Ok);

	// 재사용 대기로 거부된 시전은 같은 틱의 기본 공격을 막지 않는다
	SleepMs(40);
	Run(z, [&] { rejected = z.CastSkill(*caster, skill, 2, {}, ready); attack = z.RequestAttack(*caster, 2); });
	CHECK(rejected == SkillResult::Cooldown && attack == AttackResult::Ok);

	// 사거리 밖(없는 대상)으로 거부된 시전도 마찬가지(재사용 대기가 끝난 뒤라 대상 검사까지 간다)
	SleepMs(550);
	Run(z, [&] { bad = z.CastSkill(*caster, skill, 999, {}, ready); attack = z.RequestAttack(*caster, 2); });
	CHECK(bad == SkillResult::OutOfRange && attack != AttackResult::Throttled);

	// 통과한 요청이 있으면 같은 틱의 다음 요청은 여전히 무시된다
	SleepMs(550);
	SkillResult ok{};
	AttackResult dup{};
	Run(z, [&] { ok = z.CastSkill(*caster, skill, 2, {}, ready); dup = z.RequestAttack(*caster, 2); });
	CHECK(ok == SkillResult::Ok && dup == AttackResult::Throttled);
	z.Stop();
}

static void TestSpeedBuffCap()
{
	std::printf("[Skill speed buff cap]\n");
	SkZone z(Cfg());
	CZoneEntity* e = Add(z, 1, EntityType::Player, { 300, 300 }, Stats(1000, 20, 0));
	e->SetSpeed(10.0f);
	z.Start();

	// 서로 다른 속도 버프를 합쳐도 kMaxSpeedBuffPercent(+100%)를 넘지 않는다
	float speed = 0.0f;
	Run(z, [&]
	{
		for( uint32 id = 1; id <= 3; ++id )
		{
			SStatusTemplate st;
			st.id = id; st.kind = EStatusKind::Buff; st.target = EStatusTarget::Self; st.durationMs = 5000; st.tickMs = 0;
			st.speedPercent = kMaxSpeedBuffPercent;
			z.ApplyStatus(*e, st, e);
		}
		speed = e->GetSpeed();
	});
	CHECK(speed == 10.0f * (100 + kMaxSpeedBuffPercent) * 0.01f);
	z.Stop();
}

static int Main()
{
	TestData();
	TestSingle();
	TestArea();
	TestLineOfSight();
	TestEffectData();
	TestCastTime();
	TestProjectile();
	TestProjectileWall();
	TestStatus();
	TestStatusHolders();
	TestSkillWithStatus();
	TestExtData();
	TestShapes();
	TestHeal();
	TestStunStatus();
	TestShieldStatus();
	TestEffectFuzz();
	TestSharedGate();
	TestSpeedBuffCap();
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
}

int RunSkillTests()
{
	return Main();
}
