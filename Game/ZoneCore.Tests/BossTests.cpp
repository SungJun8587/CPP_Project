// 보스 전투 단위 테스트 - 데이터 검증, 로더, 시전(예고/회피), 페이즈, 격노, 소환, 어그로 교체, 대상 규칙, 리셋
#include "pch.h"
#include "FieldZone.h"
#include "BossSystem.h"
#include "MonsterSystem.h"
#include "GameDataLoader.h"
#include "TestSuites.h"
#include "TestCommon.h"
#include "TestZoneHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{
	namespace fs = std::filesystem;

	// 스킬/상태 효과 ID
	constexpr uint32 kSlam = 1;		// 예고가 있는 범위 공격 (시전 120ms, 반경 3, 사거리 12)
	constexpr uint32 kRoar = 2;		// 보스 중심 범위 공격 (즉시)
	constexpr uint32 kBite = 3;		// 단일 공격 (즉시, 사거리 12)
	constexpr uint32 kHuge = 4;		// 사거리가 AOI 시야를 넘는 스킬 (검증 실패용)
	constexpr uint32 kRage = 1;		// Self 버프 공격력 +100%
	constexpr uint32 kEnrage = 2;	// Self 버프 공격력 +200%
	constexpr uint32 kSlow = 3;		// Hit 디버프 (Self가 아님)

	constexpr uint32 kBossMon = 1;
	constexpr uint32 kAddMon = 2;
	constexpr uint32 kBossMon2 = 3;

	struct BZone : CFieldZone<>
	{
		explicit BZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
		~BZone() override { Stop(); }

		std::shared_ptr<const CHeroData> skills;
		std::unique_ptr<CMonsterSystem> sys;
		std::unique_ptr<CBossSystem> boss;

		// 존 스레드에서 기록하고 Run()으로 읽는다.
		struct SCast { uint32 skillId; uint64 targetId; Vec2 center; uint32 hits; Vec2 casterPos; };
		std::vector<uint32> castStarted;
		std::vector<Vec2> castStartCasterPos;
		std::vector<SCast> casts;
		int respawned = 0;

	protected:
		void OnUpdate(uint32 dt) override { CFieldZone<>::OnUpdate(dt); if( sys ) sys->Update(); }
		void OnEntityDamaged(CZoneEntity* a, CZoneEntity& v, int32 d) override { if( sys ) sys->OnDamaged(a, v, d); }
		void OnEntityDied(CZoneEntity& v, CZoneEntity*) override { if( sys ) sys->OnDied(v); }
		void OnEntityRespawned(CZoneEntity& e) override { ++respawned; if( sys ) sys->OnRespawned(e); }
		void OnEntityRemoved(CZoneEntity& e) override { if( sys ) sys->OnRemoved(e); }
		void OnSkillCastStarted(CZoneEntity& c, const SSkillTemplate& s, uint64, const Vec2&) override { castStarted.push_back(s.id); castStartCasterPos.push_back(c.GetPos()); }
		void OnSkillCast(CZoneEntity& c, const SSkillTemplate& s, uint64 t, const Vec2& center, uint32 hits) override { casts.push_back({ s.id, t, center, hits, c.GetPos() }); }
		const SStatusTemplate* FindStatusTemplate(uint32 id) const override { return skills != nullptr ? skills->FindStatus(id) : nullptr; }
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

	FieldZoneConfig Cfg()
	{
		FieldZoneConfig f;
		f.zone.zoneId = 1; f.zone.tickIntervalMs = 10;
		f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
		f.cellSize = 50; f.viewCells = 3;
		f.respawnDelayMs = 100; f.allowPvp = false; f.minDamage = 1; f.respawnInvulnMs = 0;
		return f;
	}

	SSkillTemplate Skill(uint32 id, ESkillShape shape, uint32 dmgPct, float range, float radius, uint32 castMs, uint32 cooldownMs = 100)
	{
		SSkillTemplate s;
		s.id = id; s.name = "s"; s.shape = shape; s.damagePercent = dmgPct; s.flatDamage = 0;
		s.range = range; s.radius = radius; s.cooldownMs = cooldownMs; s.maxTargets = shape == ESkillShape::Single ? 1 : 8;
		s.castTimeMs = castMs;
		return s;
	}

	SStatusTemplate Buff(uint32 id, EStatusTarget target, int32 attackPct)
	{
		SStatusTemplate t;
		t.id = id; t.name = "b"; t.kind = EStatusKind::Buff; t.target = target; t.durationMs = 600000; t.tickMs = 0; t.attackPercent = attackPct;
		return t;
	}

	std::shared_ptr<CHeroData> Skills()
	{
		auto h = std::make_shared<CHeroData>();
		h->AddSkill(Skill(kSlam, ESkillShape::Area, 100, 12.0f, 3.0f, 120));
		h->AddSkill(Skill(kRoar, ESkillShape::Area, 100, 1.0f, 6.0f, 0));
		h->AddSkill(Skill(kBite, ESkillShape::Single, 100, 12.0f, 0.0f, 0));
		h->AddSkill(Skill(kHuge, ESkillShape::Single, 100, 200.0f, 0.0f, 0));
		h->AddStatus(Buff(kRage, EStatusTarget::Self, 100));
		h->AddStatus(Buff(kEnrage, EStatusTarget::Self, 200));
		h->AddStatus(Buff(kSlow, EStatusTarget::Hit, -30));
		return h;
	}

	SMonsterTemplate BossTmpl(uint32 id, float speed = 30.0f)
	{
		SMonsterTemplate t;
		t.id = id; t.name = "boss";
		t.stats.maxHp = 1000; t.stats.attack = 10; t.stats.defense = 0;
		t.stats.attackRange = 3.0f; t.stats.attackIntervalMs = 100;
		t.speed = speed; t.aggroRange = 20.0f; t.leashRange = 200.0f; t.respawnMs = 300;
		return t;
	}

	SMonsterTemplate AddTmpl(uint32 id)
	{
		SMonsterTemplate t = BossTmpl(id);
		t.name = "add"; t.stats.maxHp = 30; t.stats.attack = 2; t.respawnMs = 100;
		return t;
	}

	std::shared_ptr<CMonsterData> Monsters(float bossSpeed = 30.0f, bool spawnBoss = true)
	{
		auto d = std::make_shared<CMonsterData>();
		d->AddMonster(BossTmpl(kBossMon, bossSpeed));
		d->AddMonster(AddTmpl(kAddMon));
		d->AddMonster(BossTmpl(kBossMon2));
		if( spawnBoss )
		{
			SSpawnEntry s; s.zoneId = 1; s.monsterId = kBossMon; s.count = 1; s.x = 500; s.y = 500; s.radius = 0;
			d->AddSpawn(s);
		}
		return d;
	}

	// ── 보스 데이터 만들기 ───────────────────────────────────
	SBossActionTemplate Act(EBossActionType type, EBossTarget target, uint32 skillId, uint32 delayMs, EBossTrigger trig = EBossTrigger::Loop,
		uint32 monsterId = 0, uint32 count = 0, float spread = 0.0f)
	{
		SBossActionTemplate a;
		a.trigger = trig; a.type = type; a.target = target; a.skillId = skillId; a.delayMs = delayMs;
		a.monsterId = monsterId; a.count = count; a.spreadRadius = spread;
		return a;
	}

	SBossActionTemplate Cast(EBossTarget target, uint32 skillId, uint32 delayMs, EBossTrigger trig = EBossTrigger::Loop)
	{
		return Act(EBossActionType::Cast, target, skillId, delayMs, trig);
	}

	// 행동을 OnEnter 먼저, 각 그룹 안에서는 주어진 순서대로 Seq를 1부터 매겨 정렬합니다.
	SBossPhaseTemplate Phase(uint32 n, uint32 hpBelow, std::vector<SBossActionTemplate> acts, uint32 status = 0, uint32 heal = 0, uint32 transition = 0)
	{
		SBossPhaseTemplate p;
		p.phase = n; p.hpBelowPercent = hpBelow; p.enterStatusId = status; p.enterHealPercent = heal; p.transitionMs = transition;
		uint32 enterSeq = 0, loopSeq = 0;
		for( SBossActionTemplate& a : acts )
			a.seq = a.trigger == EBossTrigger::OnEnter ? ++enterSeq : ++loopSeq;
		std::stable_sort(acts.begin(), acts.end(), [](const SBossActionTemplate& a, const SBossActionTemplate& b)
			{
				return (a.trigger == EBossTrigger::OnEnter ? 0 : 1) < (b.trigger == EBossTrigger::OnEnter ? 0 : 1);
			});
		p.loopStart = enterSeq;
		p.actions = std::move(acts);
		return p;
	}

	SBossTemplate Boss(uint32 id, uint32 monsterId, std::vector<SBossPhaseTemplate> phases, uint32 enrageMs = 0, uint32 enrageStatus = 0)
	{
		SBossTemplate b;
		b.id = id; b.monsterId = monsterId; b.name = "boss"; b.enrageAfterMs = enrageMs; b.enrageStatusId = enrageStatus;
		b.phases = std::move(phases);
		return b;
	}

	std::shared_ptr<CBossData> Bosses(std::initializer_list<SBossTemplate> list)
	{
		auto d = std::make_shared<CBossData>();
		for( const SBossTemplate& b : list )
			d->AddBoss(b);
		return d;
	}

	void Setup(BZone& z, std::shared_ptr<const CMonsterData> m, std::shared_ptr<const CBossData> b, std::shared_ptr<const CHeroData> skills = nullptr)
	{
		z.skills = skills != nullptr ? skills : Skills();
		SMonsterSystemConfig c; c.thinkIntervalMs = 20;
		z.sys = std::make_unique<CMonsterSystem>(z, m, c);
		z.boss = std::make_unique<CBossSystem>(z, *z.sys, std::move(b), z.skills, m);
		z.sys->SetBossSystem(z.boss.get());
	}

	template <typename F> void Run(BZone& z, F fn) { TestZone::RunOnZone(z, fn); }

	CZoneEntity* AddPlayer(BZone& z, uint64 id, Vec2 pos, int hp = 100000, int atk = 30) { return TestZone::AddTestPlayer(z, id, pos, hp, atk); }

	CMonster* Mon(BZone& z, uint64 serial) { return static_cast<CMonster*>(z.FindEntity(kMonsterIdBase | serial)); }
	constexpr uint64 kBossId = kMonsterIdBase | 1;

	// 가장 단순한 보스: 1페이즈에서 현재 대상에게 kSlam(예고) 반복
	std::shared_ptr<CBossData> SlamBoss(uint32 delayMs = 100)
	{
		return Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Cast(EBossTarget::Current, kSlam, delayMs) }) }) });
	}

	// ── 데이터 검증 ──────────────────────────────────────────
	std::string Check(const CBossData& d, const CMonsterData& m, const CHeroData& h)
	{
		std::string err;
		return d.Validate(m, h, err) ? std::string() : err;
	}

	bool Has(const std::string& text, const char* word) { return text.find(word) != std::string::npos; }

	void TestDataValidation()
	{
		std::printf("[Boss data validation]\n");
		auto mons = Monsters();
		auto heroes = Skills();
		auto check = [&](const SBossTemplate& b) { CBossData d; d.AddBoss(b); return Check(d, *mons, *heroes); };

		auto basePhases = [] { return std::vector<SBossPhaseTemplate>{
			Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600) }),
				Phase(2, 50, { Cast(EBossTarget::Self, kRoar, 600) }, kRage, 10, 500) }; };
		CHECK(check(Boss(1, kBossMon, basePhases(), 1000, kEnrage)).empty());
		CHECK(check(Boss(1, kBossMon, basePhases())).empty());

		std::string e = check(Boss(1, 99, basePhases()));
		CHECK(Has(e, "MonsterId"));

		{	// 페이즈 번호 구멍 / 1페이즈 100% / 체력 기준 순서
			auto p = basePhases(); p[1].phase = 3;
			CHECK(Has(check(Boss(1, kBossMon, p)), "no gaps"));
			p = basePhases(); p[0].hpBelowPercent = 90;
			CHECK(Has(check(Boss(1, kBossMon, p)), "HpBelowPercent=100"));
			p = basePhases(); p[1].hpBelowPercent = 100;
			CHECK(Has(check(Boss(1, kBossMon, p)), "smaller"));
			p = basePhases(); p[1].hpBelowPercent = 0;
			CHECK(Has(check(Boss(1, kBossMon, p)), "smaller"));
			p = basePhases(); p[1].enterHealPercent = 101;
			CHECK(Has(check(Boss(1, kBossMon, p)), "EnterHealPercent"));
			p = basePhases(); p[1].enterStatusId = kSlow;
			CHECK(Has(check(Boss(1, kBossMon, p)), "Target=Self"));
			p = basePhases(); p[1].enterStatusId = 77;
			CHECK(Has(check(Boss(1, kBossMon, p)), "unknown status"));
			CHECK(Has(check(Boss(1, kBossMon, {})), "phases"));
		}

		// 격노는 시간과 상태 효과를 함께 줘야 하고, 상태 효과는 Self
		CHECK(Has(check(Boss(1, kBossMon, basePhases(), 1000, 0)), "set together"));
		CHECK(Has(check(Boss(1, kBossMon, basePhases(), 0, kEnrage)), "set together"));
		CHECK(Has(check(Boss(1, kBossMon, basePhases(), 1000, kSlow)), "Target=Self"));

		auto withAction = [&](SBossActionTemplate a) { return check(Boss(1, kBossMon, { Phase(1, 100, { a }) })); };
		CHECK(Has(withAction(Cast(EBossTarget::Current, 99, 600)), "SkillId"));
		CHECK(Has(withAction(Cast(EBossTarget::Self, kBite, 600)), "Area skill"));			// Self는 범위 스킬 전용
		CHECK(withAction(Cast(EBossTarget::Self, kRoar, 600)).empty());
		CHECK(Has(withAction(Cast(EBossTarget::Current, kBite, 600000 + 1)), "DelayMs"));
		CHECK(Has(withAction(Cast(EBossTarget::Current, kBite, 100)), "500ms"));			// 한 바퀴가 너무 짧음
		CHECK(withAction(Cast(EBossTarget::Current, kBite, 500)).empty());
		CHECK(withAction(Cast(EBossTarget::Current, kSlam, 380)).empty());					// 시전 시간(120)이 한 바퀴에 들어간다

		CHECK(Has(withAction(Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, 99, 2, 3)), "MonsterId"));
		CHECK(Has(withAction(Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, kAddMon, 0, 3)), "Count"));
		CHECK(Has(withAction(Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, kAddMon, kMaxBossAdds + 1, 3)), "Count"));
		CHECK(Has(withAction(Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, kAddMon, 2, 31)), "SpreadRadius"));
		CHECK(Has(withAction(Act(EBossActionType::Summon, EBossTarget::Random, 0, 1000, EBossTrigger::Loop, kAddMon, 2, 3)), "Target=Self"));
		CHECK(withAction(Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, kAddMon, 2, 3)).empty());

		CHECK(Has(withAction(Act(EBossActionType::Aggro, EBossTarget::Current, 0, 1000)), "Aggro"));
		CHECK(Has(withAction(Act(EBossActionType::Aggro, EBossTarget::Self, 0, 1000)), "Aggro"));
		CHECK(Has(withAction(Act(EBossActionType::Aggro, EBossTarget::Farthest, 99, 1000)), "SkillId"));
		CHECK(withAction(Act(EBossActionType::Aggro, EBossTarget::Farthest, 0, 1000)).empty());

		{	// Seq 중복 / 정렬 / 행동 수 상한 / 보스를 소환
			SBossPhaseTemplate p = Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600), Cast(EBossTarget::Current, kBite, 600) });
			p.actions[1].seq = 1;
			CHECK(Has(check(Boss(1, kBossMon, { p })), "Seq"));
			p = Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600), Cast(EBossTarget::Current, kBite, 600) });
			std::swap(p.actions[0], p.actions[1]);
			CHECK(Has(check(Boss(1, kBossMon, { p })), "Seq"));
			p = Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600, EBossTrigger::OnEnter), Cast(EBossTarget::Current, kBite, 600) });
			std::swap(p.actions[0], p.actions[1]);
			CHECK(Has(check(Boss(1, kBossMon, { p })), "OnEnter"));

			std::vector<SBossActionTemplate> many;
			for( uint32 i = 0; i < kMaxBossActionsPerPhase; ++i ) many.push_back(Cast(EBossTarget::Current, kBite, 600));
			CHECK(check(Boss(1, kBossMon, { Phase(1, 100, many) })).empty());
			many.push_back(Cast(EBossTarget::Current, kBite, 600));
			CHECK(Has(check(Boss(1, kBossMon, { Phase(1, 100, many) })), "too many"));

			CBossData two;
			two.AddBoss(Boss(1, kBossMon, { Phase(1, 100, { Act(EBossActionType::Summon, EBossTarget::Self, 0, 1000, EBossTrigger::Loop, kBossMon2, 1, 3) }) }));
			two.AddBoss(Boss(2, kBossMon2, { Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600) }) }));
			CHECK(Has(Check(two, *mons, *heroes), "cannot be summoned"));
		}

		// 같은 보스 ID/몬스터를 두 번 등록할 수 없다
		CBossData dup;
		CHECK(dup.AddBoss(Boss(1, kBossMon, basePhases())));
		CHECK(!dup.AddBoss(Boss(1, kBossMon2, basePhases())));
		CHECK(!dup.AddBoss(Boss(2, kBossMon, basePhases())));
		CHECK(dup.AddBoss(Boss(2, kBossMon2, basePhases())));
		CHECK(dup.FindByMonster(kBossMon) != nullptr && dup.FindByMonster(kAddMon) == nullptr && dup.GetBossCount() == 2);
	}

	// ── 로더 ─────────────────────────────────────────────────
	_tstring ToTString(const fs::path& path)
	{
#ifdef _UNICODE
		return path.native();
#else
		return path.string();
#endif
	}

	void WriteFile(const fs::path& path, const std::string& text)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << text;
	}

	const char* kBossJson = "{\"Boss\":[{\"Id\":1,\"MonsterId\":1,\"Name\":\"b\",\"EnrageAfterMs\":1000,\"EnrageStatusId\":2}]}";
	const char* kPhaseJson = "{\"BossPhase\":[{\"BossId\":1,\"Phase\":2,\"HpBelowPercent\":50,\"EnterStatusId\":1,\"EnterHealPercent\":5,\"TransitionMs\":100},"
		"{\"BossId\":1,\"Phase\":1,\"HpBelowPercent\":100}]}";

	std::string ActionRow(int bossId, int phase, int seq, const char* trigger, const char* type, const char* target, int skill, int delay, int monster = 0, int count = 0)
	{
		char buf[512];
		std::snprintf(buf, sizeof(buf), "{\"BossId\":%d,\"Phase\":%d,\"Seq\":%d,\"Trigger\":\"%s\",\"Type\":\"%s\",\"Target\":\"%s\",\"SkillId\":%d,"
			"\"MonsterId\":%d,\"Count\":%d,\"SpreadRadius\":3.5,\"DelayMs\":%d}", bossId, phase, seq, trigger, type, target, skill, monster, count, delay);
		return buf;
	}

	std::string ActionsJson(const std::vector<std::string>& rows)
	{
		std::string s = "{\"BossAction\":[";
		for( size_t i = 0; i < rows.size(); ++i ) { if( i ) s += ","; s += rows[i]; }
		return s + "]}";
	}

	void TestLoader(const _tstring& dataDir)
	{
		std::printf("[Boss loader]\n");
		auto mons = Monsters();
		auto heroes = Skills();
		// 테스트 프로세스를 동시에 돌려도 서로의 임시 폴더를 지우지 않게 폴더 이름에 난수를 넣는다.
		const fs::path dir = fs::temp_directory_path() / ("boss_loader_test_" + std::to_string(std::random_device{}()));
		fs::remove_all(dir);
		fs::create_directories(dir);
		const _tstring d = ToTString(dir);

		// Boss.json이 없으면 보스 없이 성공
		auto none = CGameDataLoader::LoadBosses(d, *mons, *heroes);
		CHECK(none != nullptr && none->GetBossCount() == 0);

		// 정상: 행 순서가 뒤섞여도 페이즈/행동이 정렬된다 (OnEnter 먼저, Seq 순)
		const std::string good = ActionsJson({
			ActionRow(1, 2, 2, "Loop", "Cast", "Current", kBite, 700),
			ActionRow(1, 2, 1, "Loop", "Cast", "Self", kRoar, 600),
			ActionRow(1, 2, 1, "OnEnter", "Summon", "Self", 0, 300, kAddMon, 2),
			ActionRow(1, 1, 1, "Loop", "Aggro", "Farthest", 0, 1000) });
		WriteFile(dir / "Boss.json", kBossJson);
		WriteFile(dir / "BossPhase.json", kPhaseJson);
		WriteFile(dir / "BossAction.json", good);
		auto data = CGameDataLoader::LoadBosses(d, *mons, *heroes);
		CHECK(data != nullptr);
		if( data != nullptr )
		{
			const SBossTemplate* b = data->FindByMonster(kBossMon);
			CHECK(b != nullptr && b->id == 1 && b->enrageAfterMs == 1000 && b->enrageStatusId == 2 && b->phases.size() == 2);
			if( b != nullptr && b->phases.size() == 2 )
			{
				CHECK(b->phases[0].phase == 1 && b->phases[1].phase == 2 && b->phases[1].hpBelowPercent == 50);
				CHECK(b->phases[1].enterStatusId == 1 && b->phases[1].enterHealPercent == 5 && b->phases[1].transitionMs == 100);
				const SBossPhaseTemplate& p2 = b->phases[1];
				CHECK(p2.actions.size() == 3 && p2.loopStart == 1);
				if( p2.actions.size() == 3 )
				{
					CHECK(p2.actions[0].trigger == EBossTrigger::OnEnter && p2.actions[0].type == EBossActionType::Summon && p2.actions[0].count == 2);
					CHECK(p2.actions[1].seq == 1 && p2.actions[1].skillId == kRoar && p2.actions[2].seq == 2 && p2.actions[2].skillId == kBite);
					CHECK(p2.actions[0].spreadRadius == 3.5f);
				}
				CHECK(b->phases[0].actions.size() == 1 && b->phases[0].loopStart == 0 && b->phases[0].actions[0].type == EBossActionType::Aggro);
			}
		}

		auto rejected = [&](const char* what, const std::string& boss, const std::string& phase, const std::string& actions, bool phaseFile = true, bool actionFile = true)
			{
				WriteFile(dir / "Boss.json", boss);
				if( phaseFile ) WriteFile(dir / "BossPhase.json", phase); else fs::remove(dir / "BossPhase.json");
				if( actionFile ) WriteFile(dir / "BossAction.json", actions); else fs::remove(dir / "BossAction.json");
				const bool ok = CGameDataLoader::LoadBosses(d, *mons, *heroes) == nullptr;
				if( !ok ) std::printf("  accepted invalid data: %s\n", what);
				CHECK(ok);
			};

		rejected("missing BossPhase.json", kBossJson, kPhaseJson, good, false, true);
		rejected("missing BossAction.json", kBossJson, kPhaseJson, good, true, false);
		rejected("phase of unknown boss", kBossJson, "{\"BossPhase\":[{\"BossId\":9,\"Phase\":1,\"HpBelowPercent\":100}]}", good);
		rejected("duplicate phase", kBossJson, "{\"BossPhase\":[{\"BossId\":1,\"Phase\":1,\"HpBelowPercent\":100},{\"BossId\":1,\"Phase\":1,\"HpBelowPercent\":100}]}", ActionsJson({}));
		rejected("action of unknown phase", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 3, 1, "Loop", "Cast", "Self", kRoar, 600) }));
		rejected("action of unknown boss", kBossJson, kPhaseJson, ActionsJson({ ActionRow(5, 1, 1, "Loop", "Cast", "Self", kRoar, 600) }));
		rejected("unknown enum name", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 1, 1, "Loop", "Dance", "Self", kRoar, 600) }));
		rejected("unknown target name", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 1, 1, "Loop", "Cast", "Everyone", kRoar, 600) }));
		rejected("unknown skill", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 1, 1, "Loop", "Cast", "Self", 99, 600) }));
		rejected("duplicate seq", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 1, 1, "Loop", "Cast", "Self", kRoar, 600), ActionRow(1, 1, 1, "Loop", "Cast", "Self", kRoar, 600) }));
		rejected("Seq 0", kBossJson, kPhaseJson, ActionsJson({ ActionRow(1, 1, 0, "Loop", "Cast", "Self", kRoar, 600) }));
		rejected("unknown monster", "{\"Boss\":[{\"Id\":1,\"MonsterId\":42,\"Name\":\"b\"}]}", kPhaseJson, good);
		rejected("same monster twice", "{\"Boss\":[{\"Id\":1,\"MonsterId\":1,\"Name\":\"b\"},{\"Id\":2,\"MonsterId\":1,\"Name\":\"c\"}]}", kPhaseJson, good);
		rejected("missing DelayMs", kBossJson, kPhaseJson,
			"{\"BossAction\":[{\"BossId\":1,\"Phase\":1,\"Seq\":1,\"Trigger\":\"Loop\",\"Type\":\"Cast\",\"Target\":\"Self\",\"SkillId\":2}]}");
		rejected("Boss.json malformed", "{\"Boss\":[", kPhaseJson, good);

		// 선택 열(EnrageAfterMs 등)을 생략해도 읽힌다
		WriteFile(dir / "Boss.json", "{\"Boss\":[{\"Id\":1,\"MonsterId\":1,\"Name\":\"b\"}]}");
		WriteFile(dir / "BossPhase.json", "{\"BossPhase\":[{\"BossId\":1,\"Phase\":1,\"HpBelowPercent\":100}]}");
		WriteFile(dir / "BossAction.json", ActionsJson({ ActionRow(1, 1, 1, "Loop", "Cast", "Self", kRoar, 600) }));
		auto lean = CGameDataLoader::LoadBosses(d, *mons, *heroes);
		CHECK(lean != nullptr && lean->GetBossCount() == 1);

		fs::remove_all(dir);

		// 샘플 GameData(엑셀에서 변환한 실제 데이터)가 로드되고 존 설정과 맞는지
		const fs::path sample = fs::path(dataDir);
		if( fs::exists(sample / "Boss.json") )
		{
			auto sh = CGameDataLoader::LoadHeroes(dataDir);
			auto sm = CGameDataLoader::LoadMonsters(dataDir);
			CHECK(sh != nullptr && sm != nullptr);
			if( sh != nullptr && sm != nullptr )
			{
				auto sb = CGameDataLoader::LoadBosses(dataDir, *sm, *sh);
				CHECK(sb != nullptr && sb->GetBossCount() >= 1);
				if( sb != nullptr )
				{
					for( const SBossTemplate& b : sb->GetBosses() )
						CHECK(sm->FindMonster(b.monsterId) != nullptr && !b.phases.empty());

					BZone z(Cfg());
					z.skills = sh;
					SMonsterSystemConfig c;
					z.sys = std::make_unique<CMonsterSystem>(z, sm, c);
					z.boss = std::make_unique<CBossSystem>(z, *z.sys, sb, sh, sm);
					std::string err;
					CHECK(z.boss->Validate(err));
					if( !err.empty() ) std::printf("  %s\n", err.c_str());
				}
			}
		}
		else
			std::printf("  (sample Boss.json not found in the GameData folder; sample checks skipped)\n");
	}

	// ── 존에서의 동작 ────────────────────────────────────────
	void TestValidateZone()
	{
		std::printf("[Boss zone validation]\n");
		{
			BZone z(Cfg());
			Setup(z, Monsters(), SlamBoss(400));
			std::string err;
			CHECK(z.boss->Validate(err));
			if( !err.empty() ) std::printf("  %s\n", err.c_str());
		}
		{	// 사거리가 AOI 보장 시야를 넘는 스킬
			BZone z(Cfg());
			Setup(z, Monsters(), Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Cast(EBossTarget::Current, kHuge, 600) }) }) }));
			std::string err;
			CHECK(!z.boss->Validate(err) && Has(err, "view range"));
		}
		{	// 데이터 오류도 같은 경로로 걸러진다
			BZone z(Cfg());
			Setup(z, Monsters(), Bosses({ Boss(1, 99, { Phase(1, 100, { Cast(EBossTarget::Current, kBite, 600) }) }) }));
			std::string err;
			CHECK(!z.boss->Validate(err));
		}
		{	// 보스 데이터가 없으면 아무것도 하지 않는다
			BZone z(Cfg());
			Setup(z, Monsters(), nullptr);
			std::string err;
			CHECK(z.boss->Validate(err));
			z.sys->SpawnAll();
			CHECK(z.boss->GetBossCount() == 0 && Mon(z, 1) != nullptr && !Mon(z, 1)->IsBoss());
		}
	}

	void TestTelegraphAndDodge()
	{
		std::printf("[Boss cast: telegraph, standstill, dodge]\n");

		{	// 맞는 경우: 시전 시작 -> 120ms 뒤 중심 위치에서 폭발. 시전 중 보스는 움직이지 않는다
			BZone z(Cfg());
			Setup(z, Monsters(), SlamBoss(100));
			z.sys->SpawnAll();
			AddPlayer(z, 1, { 508, 500 });
			z.Start();
			SleepMs(500);
			std::vector<BZone::SCast> casts; std::vector<uint32> started; std::vector<Vec2> startPos; int hp = 0; bool isBoss = false;
			Run(z, [&] { casts = z.casts; started = z.castStarted; startPos = z.castStartCasterPos; hp = z.FindEntity(1)->GetHp(); isBoss = Mon(z, 1)->IsBoss(); });
			CHECK(isBoss);
			CHECK(!started.empty() && started.front() == kSlam);
			CHECK(!casts.empty());
			size_t slams = 0, sameSpot = 0;
			for( const auto& c : casts )
				if( c.skillId == kSlam )
				{
					++slams;
					CHECK(c.hits == 1);
					if( c.casterPos.x == startPos[slams - 1].x && c.casterPos.y == startPos[slams - 1].y ) ++sameSpot;
				}
			CHECK(slams >= 1);
			CHECK(sameSpot == slams);			// 시전을 시작한 자리에서 그대로 터졌다
			CHECK(hp < 100000);
			z.Stop();
		}

		{	// 피하는 경우: 시전 시작 직후 대상이 멀리 가면 폭발은 빈 곳에 터진다 (중심은 시전 시작 때 정해짐)
			BZone z(Cfg());
			Setup(z, Monsters(), SlamBoss(100));
			z.sys->SpawnAll();
			AddPlayer(z, 1, { 508, 500 });
			z.Start();
			bool seen = false;
			for( int i = 0; i < 100 && !seen; ++i )
			{
				SleepMs(5);
				Run(z, [&] { seen = !z.castStarted.empty(); });
			}
			CHECK(seen);
			Vec2 startCenter{ 0, 0 };
			Run(z, [&] { z.Teleport(1, { 508, 560 }); });		// 시전이 끝나기(120ms) 전에 벗어난다
			SleepMs(200);
			std::vector<BZone::SCast> casts; int hpAfter = 0;
			Run(z, [&] { casts = z.casts; hpAfter = z.FindEntity(1)->GetHp(); });
			CHECK(!casts.empty());
			if( !casts.empty() )
			{
				CHECK(casts.front().skillId == kSlam && casts.front().hits == 0);
				startCenter = casts.front().center;
				CHECK(std::fabs(startCenter.x - 508.0f) < 1.5f && std::fabs(startCenter.y - 500.0f) < 1.5f);		// 맞은 자리가 아니라 시전 시작 때의 자리
			}
			CHECK(hpAfter == 100000);
			z.Stop();
		}
	}

	void TestPhases()
	{
		std::printf("[Boss phases: enter status/heal/on-enter actions, no regress]\n");
		auto data = Bosses({ Boss(1, kBossMon, {
			Phase(1, 100, { Cast(EBossTarget::Current, kBite, 3000) }),
			Phase(2, 50, { Cast(EBossTarget::Self, kRoar, 100, EBossTrigger::OnEnter), Cast(EBossTarget::Current, kBite, 3000) }, kRage, 10),
			Phase(3, 25, { Cast(EBossTarget::Current, kBite, 3000) }, kEnrage) }) });
		BZone z(Cfg());
		Setup(z, Monsters(), data);
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 505, 500 }, 1000000);
		z.Start();
		SleepMs(300);

		size_t phase = 9; int attack = 0; size_t castsBefore = 0;
		Run(z, [&] { phase = z.boss->Find(kBossId)->phaseIdx; attack = Mon(z, 1)->GetAttack(); castsBefore = z.casts.size(); });
		CHECK(phase == 0 && attack == 10);

		// 체력 45%: 2페이즈. 상태 효과(+100%), 회복 10%, 진입 행동(포효)이 반복 행동(물기)보다 먼저
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 550); });
		SleepMs(250);
		int hp = 0; std::vector<BZone::SCast> casts;
		Run(z, [&] { phase = z.boss->Find(kBossId)->phaseIdx; attack = Mon(z, 1)->GetAttack(); hp = Mon(z, 1)->GetHp(); casts = z.casts; });
		CHECK(phase == 1);
		CHECK(attack == 20);
		CHECK(hp >= 550);			// 450 + 100(10%)
		CHECK(casts.size() > castsBefore && casts[castsBefore].skillId == kRoar);
		bool biteAfterRoar = false;
		for( size_t i = castsBefore + 1; i < casts.size(); ++i ) if( casts[i].skillId == kBite ) biteAfterRoar = true;
		CHECK(biteAfterRoar);

		// 회복해도 페이즈는 내려가지 않는다
		Run(z, [&] { z.Heal(*Mon(z, 1), 100000); });
		SleepMs(100);
		Run(z, [&] { phase = z.boss->Find(kBossId)->phaseIdx; attack = Mon(z, 1)->GetAttack(); });
		CHECK(phase == 1 && attack == 20);

		// 한꺼번에 20%까지 깎아도 한 단계씩 올라 모든 진입 효과가 적용된다 (1000 * 0.2 = 200)
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), Mon(z, 1)->GetHp() - 200); });
		SleepMs(250);
		Run(z, [&] { phase = z.boss->Find(kBossId)->phaseIdx; attack = Mon(z, 1)->GetAttack(); });
		CHECK(phase == 2);
		CHECK(attack == 40);		// 10 * (1 + 1.0 + 2.0)
		z.Stop();
	}

	void TestEnrageAndReset()
	{
		std::printf("[Boss enrage + reset on return]\n");
		auto data = Bosses({ Boss(1, kBossMon, {
			Phase(1, 100, { Cast(EBossTarget::Current, kBite, 3000) }),
			Phase(2, 50, { Cast(EBossTarget::Current, kBite, 3000) }, kRage) }, 250, kEnrage) });
		BZone z(Cfg());
		Setup(z, Monsters(), data);
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 505, 500 }, 1000000);
		z.Start();
		SleepMs(120);

		bool enraged = true; int attack = 0;
		Run(z, [&] { enraged = z.boss->Find(kBossId)->enraged; attack = Mon(z, 1)->GetAttack(); });
		CHECK(!enraged && attack == 10);			// 아직 시간이 안 됨

		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 600); });		// 2페이즈 (+100%)
		SleepMs(400);
		size_t phase = 0;
		Run(z, [&] { enraged = z.boss->Find(kBossId)->enraged; attack = Mon(z, 1)->GetAttack(); phase = z.boss->Find(kBossId)->phaseIdx; });
		CHECK(enraged && phase == 1);
		CHECK(attack == 40);						// 10 * (1 + 1.0 + 2.0)

		// 대상이 사라지면 보스는 복귀하고 페이즈/격노/상태 효과가 처음으로 돌아간다 (죽이면 곧 부활해 다시 노리므로 제거한다)
		Run(z, [&] { z.RemoveEntity(1); });
		SleepMs(300);
		bool engaged = true; EMonsterState st = EMonsterState::Chase; int hp = 0;
		Run(z, [&] { const SBossRuntime* rt = z.boss->Find(kBossId); enraged = rt->enraged; phase = rt->phaseIdx; engaged = rt->engaged;
		attack = Mon(z, 1)->GetAttack(); st = Mon(z, 1)->GetState(); hp = Mon(z, 1)->GetHp(); });
		CHECK(!enraged && phase == 0 && !engaged);
		CHECK(attack == 10);
		CHECK(st != EMonsterState::Chase);
		CHECK(hp == 1000);
		z.Stop();
	}

	void TestSummons()
	{
		std::printf("[Boss summon: cap, no respawn, corpse cleanup, boss death/respawn/remove]\n");
		auto data = Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Act(EBossActionType::Summon, EBossTarget::Self, 0, 500, EBossTrigger::Loop, kAddMon, 5, 4) }) }) });
		BZone z(Cfg());
		Setup(z, Monsters(), data);
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 510, 500 }, 10000000);
		z.Start();
		SleepMs(1500);

		size_t adds = 0, total = 0, targeted = 0, summoned = 0;
		std::vector<uint64> ids;
		Run(z, [&] {
			adds = z.boss->Find(kBossId)->adds.size(); total = z.sys->GetMonsterCount(); ids = z.boss->Find(kBossId)->adds;
			for( uint64 id : ids ) { CMonster* m = static_cast<CMonster*>(z.FindEntity(id)); if( m != nullptr ) { if( m->IsSummoned() && m->GetOwnerId() == kBossId ) ++summoned; if( m->GetTargetId() == 1 ) ++targeted; } } });
		CHECK(adds == kMaxBossAdds);					// 5 + 5 + 2 (상한)
		CHECK(total == 1 + kMaxBossAdds);
		CHECK(summoned == kMaxBossAdds);
		CHECK(targeted >= kMaxBossAdds - 1);			// 소환수는 보스의 대상을 노린다 (막 소환된 한두 마리는 아직일 수 있다)

		// 소환수를 죽이면 부활하지 않고, 시체는 잠시 뒤 치워지고, 빈 자리는 다음 소환으로 채워진다
		const uint64 victim = ids.front();
		Run(z, [&] { z.DealDamage(*z.FindEntity(victim), z.FindEntity(1), 1000); });
		int respawnedBefore = 0, respawnedAfter = 0; bool dead = false;
		Run(z, [&] { respawnedBefore = z.respawned; });
		SleepMs(400);
		Run(z, [&] { respawnedAfter = z.respawned; CZoneEntity* e = z.FindEntity(victim); dead = e != nullptr && !e->IsAlive(); });
		CHECK(respawnedAfter == respawnedBefore);
		CHECK(dead);
		SleepMs(CBossSystem::kAddCorpseMs + 300);
		bool gone = false; size_t adds2 = 0;
		Run(z, [&] { gone = z.FindEntity(victim) == nullptr; adds2 = z.boss->Find(kBossId)->adds.size(); });
		CHECK(gone);
		CHECK(adds2 <= kMaxBossAdds);

		// 보스가 죽으면 소환수가 모두 사라진다
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 100000); });
		SleepMs(100);
		size_t left = 99; size_t phase = 9;
		Run(z, [&] { left = z.sys->GetMonsterCount(); phase = z.boss->Find(kBossId)->adds.size(); });
		CHECK(left == 1);
		CHECK(phase == 0);

		// 부활한 보스는 처음부터 다시 시작한다
		SleepMs(500);
		int hp = 0; bool alive = false; bool engagedAgain = false;
		Run(z, [&] { hp = Mon(z, 1)->GetHp(); alive = Mon(z, 1)->IsAlive(); engagedAgain = z.boss->Find(kBossId)->engaged; });
		CHECK(alive && hp == 1000);
		(void)engagedAgain;

		// 보스를 존에서 제거하면 상태와 소환수도 함께 정리된다 (ASan이 해제된 메모리 접근을 잡는다)
		SleepMs(600);
		Run(z, [&] { z.RemoveEntity(kBossId); });
		SleepMs(100);
		size_t bossCount = 9, mons = 9;
		Run(z, [&] { bossCount = z.boss->GetBossCount(); mons = z.sys->GetMonsterCount(); });
		CHECK(bossCount == 0 && mons == 0);
		SleepMs(100);
		z.Stop();
	}

	void TestAggroSwap()
	{
		std::printf("[Boss aggro swap]\n");
		auto data = Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Act(EBossActionType::Aggro, EBossTarget::Farthest, 0, 600) }) }) });
		BZone z(Cfg());
		Setup(z, Monsters(0.5f), data);
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 504, 500 });
		AddPlayer(z, 2, { 512, 500 });
		z.Start();
		SleepMs(80);
		uint64 target = 0;
		Run(z, [&] { z.DealDamage(*Mon(z, 1), z.FindEntity(1), 400); });		// 1번이 큰 위협을 쌓는다
		// 어그로 교체 행동은 600ms마다 돈다. 한 바퀴 넘게 기다린 뒤 확인한다
		SleepMs(900);
		Run(z, [&] { target = Mon(z, 1)->GetTargetId(); });
		CHECK(target == 2);			// 가장 먼 2번이 어그로 1순위가 되었다
		z.Stop();
	}

	// 단일 스킬을 대상 규칙별로 반복 시전시켜 맞은 대상의 집합을 얻는다.
	std::set<uint64> RuleTargets(EBossTarget rule, std::initializer_list<std::pair<Vec2, int>> players)
	{
		auto data = Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Cast(rule, kBite, 500) }) }) });
		BZone z(Cfg());
		Setup(z, Monsters(0.5f), data);
		z.sys->SpawnAll();
		uint64 id = 1;
		for( const auto& p : players )
			AddPlayer(z, id++, p.first, p.second);
		z.Start();
		SleepMs(2500);
		std::set<uint64> hit;
		Run(z, [&] { for( const auto& c : z.casts ) if( c.skillId == kBite ) hit.insert(c.targetId); });
		z.Stop();
		return hit;
	}

	void TestTargetRules()
	{
		std::printf("[Boss target rules]\n");
		const Vec2 nearP{ 504, 500 }, midP{ 509, 500 }, farP{ 518, 500 };
		// 사거리(12) 안: nearP(4), midP(9). 사거리 밖: farP(18, 선공 거리 20 안이라 보스는 반응한다)
		CHECK((RuleTargets(EBossTarget::Nearest, { { nearP, 100000 }, { midP, 100000 }, { farP, 100000 } }) == std::set<uint64>{ 1 }));
		CHECK((RuleTargets(EBossTarget::Farthest, { { nearP, 100000 }, { midP, 100000 }, { farP, 100000 } }) == std::set<uint64>{ 2 }));		// far는 후보가 아니다
		CHECK((RuleTargets(EBossTarget::LowestHp, { { nearP, 100000 }, { midP, 50000 }, { farP, 10 } }) == std::set<uint64>{ 2 }));
		const std::set<uint64> random = RuleTargets(EBossTarget::Random, { { nearP, 100000 }, { midP, 100000 }, { farP, 100000 } });
		CHECK(!random.empty() && random.count(3) == 0);
		const std::set<uint64> current = RuleTargets(EBossTarget::Current, { { nearP, 100000 }, { midP, 100000 } });
		CHECK(current.size() == 1);
	}

	void TestActionTimeout()
	{
		std::printf("[Boss action timeout]\n");
		// 사거리 안에 대상이 없어 막힌 행동은 제한 시간 뒤 건너뛰고 다음 행동으로 넘어간다
		auto data = Bosses({ Boss(1, kBossMon, { Phase(1, 100, { Cast(EBossTarget::Random, kBite, 100), Cast(EBossTarget::Self, kRoar, 100) }) }) });
		BZone z(Cfg());
		Setup(z, Monsters(0.5f), data);
		z.boss->SetActionTimeoutMs(200);
		z.sys->SpawnAll();
		AddPlayer(z, 1, { 518, 500 });			// 선공 거리(20) 안, 물기 사거리(12) 밖
		z.Start();
		SleepMs(100);
		size_t roars = 0, bites = 0;
		Run(z, [&] { for( const auto& c : z.casts ) { if( c.skillId == kRoar ) ++roars; if( c.skillId == kBite ) ++bites; } });
		CHECK(roars == 0 && bites == 0);		// 제한 시간 전에는 기다린다
		SleepMs(500);
		Run(z, [&] { roars = bites = 0; for( const auto& c : z.casts ) { if( c.skillId == kRoar ) ++roars; if( c.skillId == kBite ) ++bites; } });
		CHECK(roars >= 1);
		CHECK(bites == 0);
		z.Stop();
	}

	int Main(const _tstring& dataDir)
	{
		TestDataValidation();
		TestLoader(dataDir);
		TestValidateZone();
		TestTelegraphAndDodge();
		TestPhases();
		TestEnrageAndReset();
		TestSummons();
		TestAggroSwap();
		TestTargetRules();
		TestActionTimeout();
		std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
		return g_failed ? 1 : 0;
	}
}

int RunBossTests(const _tstring& dataDir)
{
	return Main(dataDir);
}