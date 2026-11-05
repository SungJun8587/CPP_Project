
//***************************************************************************
// GameDataLoader.cpp: implementation of the CGameDataLoader class.
//
//***************************************************************************

#include "pch.h"
#include "GameDataLoader.h"
#include "../GameCommon/ZoneCore/BlockMapFile.h"
#include <ConfigFile/JSONConfigFieldReader.h>
#include "../GameCommon/Generated/GeneratedCommon.h"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <type_traits>

using namespace GameDataRows;

// SkillData.h가 예전 방식(수동 enum)이면 아래 대입에서 알 수 없는 변환 오류가 나므로 원인을 바로 알 수 있게 확인한다.
static_assert(std::is_same_v<decltype(SStatusRow::kind), EStatusKind>
	&& std::is_same_v<decltype(SStatusRow::target), EStatusTarget>
	&& std::is_same_v<decltype(SSkillRow::shape), ESkillShape>
	&& std::is_same_v<decltype(SSkillRow::target), ESkillTarget>,
	"SkillData.h must use the generated enums: using GameDataEnums::ESkillShape / EStatusKind / EStatusTarget (replace SkillData.h)");

namespace
{
	//***************************************************************************
	// @brief <dir>/TRow::kFile JSON을 열어 TRow::kTable 키의 행 배열을 읽는다. 파일/키가 없으면 실패한다.
	//        (기획 데이터가 통째로 빠진 배포 실수를 빈 테이블로 넘기지 않기 위함)
	// @details 행 타입(GameCommon/Generated/GeneratedCommon.h, XlsxToJson이 엑셀 헤더로 생성)이 파일 이름, 테이블 키, 필드 규칙(Specs)을 함께 가지므로
	//          새 테이블은 엑셀에 시트를 추가하고 XlsxToJson을 다시 실행하면 된다.
	//***************************************************************************
	// 행 타입에 enumOk가 있으면(enum 열이 있는 시트) 그 값을, 없으면 true를 돌려준다.
	template <typename TRow>
	auto RowEnumOk(const TRow& row, int) -> decltype(static_cast<bool>(row.enumOk))
	{
		return row.enumOk;
	}

	template <typename TRow>
	bool RowEnumOk(const TRow&, long)
	{
		return true;
	}

	template <typename TRow>
	bool ReadTable(const std::filesystem::path& dir, std::vector<TRow>& rows)
	{
		const std::filesystem::path file = dir / TRow::kFile;
		const TCHAR* table = TRow::kTable;

		CRapidJSONUtil json;
		if( !json.LoadFromFile(file.c_str()) )
		{
			LOG_ERROR(_T("[GameData] load failed. path=%s"), file.c_str());
			return false;
		}
		if( !json.IsObject() )
		{
			LOG_ERROR(_T("[GameData] top-level value must be an object. path=%s"), file.c_str());
			return false;
		}

		const std::vector<_tstring> keys = json.GetKeys();
		if( std::find(keys.begin(), keys.end(), table) == keys.end() )
		{
			LOG_ERROR(_T("[GameData] table key missing. path=%s, key=%s"), file.c_str(), table);
			return false;
		}

		CJSONConfigFieldReader reader(json);
		reader.ReadObjects(table, rows, TRow::Specs());
		if( !reader.Validate() )
			return false;

		// enum 열에 Enum 시트에 없는 이름이 있으면(JSON을 손으로 고친 경우) 생성된 FromJSON이 enumOk를 내려 둔다.
		for( size_t i = 0; i < rows.size(); ++i )
		{
			if( !RowEnumOk(rows[i], 0) )
			{
				LOG_ERROR(_T("[GameData] unknown enum name. path=%s, row=%u"), file.c_str(), static_cast<uint32>(i));
				return false;
			}
		}
		return true;
	}

}

//***************************************************************************
// @brief 영웅 데이터를 읽고 검증한다.
// @param dir 기획 데이터 폴더 (Hero.json)
// @return 검증을 통과한 불변 데이터(영웅 1개 이상), 하나라도 실패하면 nullptr (원인은 로그)
//***************************************************************************
std::shared_ptr<const CHeroData> CGameDataLoader::LoadHeroes(const _tstring& dir)
{
	std::vector<SHeroRow> rows;
	if( !ReadTable(std::filesystem::path(dir), rows) )
		return nullptr;

	std::shared_ptr<CHeroData> data = std::make_shared<CHeroData>();

	for( const SHeroRow& r : rows )
	{
		SHeroTemplate h;
		h.id = r.id;
		h.name = TStringToString(r.name.c_str());
		h.stats.maxHp = r.maxHp;
		h.stats.attack = r.attack;
		h.stats.defense = r.defense;
		h.stats.attackRange = static_cast<float>(r.attackRange);
		h.stats.attackIntervalMs = r.attackIntervalMs;
		h.speed = static_cast<float>(r.speed);

		if( !data->AddHero(h) )
		{
			LOG_ERROR(_T("[GameData] duplicate hero id. id=%u"), r.id);
			return nullptr;
		}
	}

	// 상태 효과(선택). 스킬이 참조하므로 스킬보다 먼저 읽는다.
	const std::filesystem::path statusFile = std::filesystem::path(dir) / SStatusRow::kFile;
	if( std::filesystem::exists(statusFile) )
	{
		std::vector<SStatusRow> statusRows;
		if( !ReadTable(std::filesystem::path(dir), statusRows) )
			return nullptr;

		for( const SStatusRow& r : statusRows )
		{
			SStatusTemplate t;
			t.id = r.id;
			t.name = TStringToString(r.name.c_str());
			t.kind = r.kind;
			t.target = r.target;
			t.durationMs = r.durationMs;
			t.tickMs = r.tickMs;
			t.damagePercent = r.damagePercent;
			t.flatDamage = r.flatDamage;
			t.attackPercent = r.attackPercent;
			t.defensePercent = r.defensePercent;
			t.speedPercent = r.speedPercent;
			t.healPercent = r.healPercent;
			t.flatHeal = r.flatHeal;

			if( !data->AddStatus(t) )
			{
				LOG_ERROR(_T("[GameData] duplicate status id. id=%u"), r.id);
				return nullptr;
			}
		}
	}

	// 스킬(선택). 파일이 없으면 스킬 없이 구동하고, 있는데 형식이 틀리면 구동하지 않는다.
	const std::filesystem::path skillFile = std::filesystem::path(dir) / SSkillRow::kFile;
	if( std::filesystem::exists(skillFile) )
	{
		std::vector<SSkillRow> skillRows;
		if( !ReadTable(std::filesystem::path(dir), skillRows) )
			return nullptr;

		for( const SSkillRow& r : skillRows )
		{
			SSkillTemplate s;
			s.id = r.id;
			s.name = TStringToString(r.name.c_str());
			s.shape = r.shape;
			s.damagePercent = r.damagePercent;
			s.flatDamage = r.flatDamage;
			s.range = static_cast<float>(r.range);
			s.radius = static_cast<float>(r.radius);
			s.cooldownMs = r.cooldownMs;
			s.maxTargets = r.maxTargets;
			s.castTimeMs = r.castTimeMs;
			s.projectileSpeed = static_cast<float>(r.projectileSpeed);
			s.statusId = r.statusId;
			s.target = r.target;
			s.healPercent = r.healPercent;
			s.flatHeal = r.flatHeal;
			s.angle = static_cast<float>(r.angle);

			if( !data->AddSkill(s) )
			{
				LOG_ERROR(_T("[GameData] duplicate skill id. id=%u"), r.id);
				return nullptr;
			}
		}

		const std::filesystem::path linkFile = std::filesystem::path(dir) / SHeroSkillRow::kFile;
		if( std::filesystem::exists(linkFile) )
		{
			std::vector<SHeroSkillRow> linkRows;
			if( !ReadTable(std::filesystem::path(dir), linkRows) )
				return nullptr;

			for( const SHeroSkillRow& r : linkRows )
			{
				if( !data->AssignSkill(r.heroId, r.skillId) )
				{
					LOG_ERROR(_T("[GameData] hero-skill link rejected (unknown hero/skill, duplicate, or more than %u skills). heroId=%u, skillId=%u"),
						kMaxHeroSkills, r.heroId, r.skillId);
					return nullptr;
				}
			}
		}
		else
			LOG_WARNING(_T("[GameData] %s has skills but %s is missing: no hero can use them."), SSkillRow::kFile, SHeroSkillRow::kFile);
	}

	std::string error;
	if( !data->Validate(error) )
	{
		LOG_ERROR(_T("[GameData] hero validation failed. %hs"), error.c_str());
		return nullptr;
	}

	LOG_INFO(_T("[GameData] loaded. heroes=%u, skills=%u, statuses=%u"), static_cast<uint32>(data->GetHeroCount()), static_cast<uint32>(data->GetSkillCount()), static_cast<uint32>(data->GetStatusCount()));
	return data;
}

//***************************************************************************
// @brief 몬스터/스폰 데이터를 읽고 검증한다.
// @param dir 기획 데이터 폴더 (Monster.json, Spawn.json)
// @return 검증을 통과한 불변 데이터, 하나라도 실패하면 nullptr (원인은 로그)
//***************************************************************************
std::shared_ptr<const CMonsterData> CGameDataLoader::LoadMonsters(const _tstring& dir)
{
	const std::filesystem::path base(dir);

	std::vector<SMonsterRow> monsters;
	if( !ReadTable(base, monsters) )
		return nullptr;

	std::vector<SSpawnRow> spawns;
	if( !ReadTable(base, spawns) )
		return nullptr;

	std::shared_ptr<CMonsterData> data = std::make_shared<CMonsterData>();

	for( const SMonsterRow& r : monsters )
	{
		SMonsterTemplate t;
		t.id = r.id;
		t.name = TStringToString(r.name.c_str());
		t.stats.maxHp = r.maxHp;
		t.stats.attack = r.attack;
		t.stats.defense = r.defense;
		t.stats.attackRange = static_cast<float>(r.attackRange);
		t.stats.attackIntervalMs = r.attackIntervalMs;
		t.speed = static_cast<float>(r.speed);
		t.aggroRange = static_cast<float>(r.aggroRange);
		t.leashRange = static_cast<float>(r.leashRange);
		t.respawnMs = r.respawnMs;
		t.assistRange = static_cast<float>(r.assistRange);
		t.skillId = r.skillId;

		if( !data->AddMonster(t) )
		{
			LOG_ERROR(_T("[GameData] duplicate monster id. id=%u"), r.id);
			return nullptr;
		}
	}

	for( const SSpawnRow& r : spawns )
	{
		SSpawnEntry s;
		s.zoneId = r.zoneId;
		s.monsterId = r.monsterId;
		s.count = r.count;
		s.x = static_cast<float>(r.x);
		s.y = static_cast<float>(r.y);
		s.radius = static_cast<float>(r.radius);
		data->AddSpawn(s);
	}

	std::string error;
	if( !data->Validate(error) )
	{
		LOG_ERROR(_T("[GameData] validation failed. %hs"), error.c_str());
		return nullptr;
	}

	LOG_INFO(_T("[GameData] loaded. monsters=%u, spawns=%u"), static_cast<uint32>(data->GetMonsterCount()), static_cast<uint32>(data->GetSpawns().size()));
	return data;
}

//***************************************************************************
// @brief 보스 전투 데이터를 읽고 검증한다. Boss.json이 없으면 보스 없이 구동한다(성공). 있으면 BossPhase.json, BossAction.json도 있어야 한다.
// @details 행들을 보스 > 페이즈 > 행동으로 묶고 행동은 (OnEnter 먼저, Seq 순)으로 정렬한다.
//          어느 보스/페이즈에도 속하지 않는 행, 같은 페이즈가 두 번 나오는 경우는 오류다.
// @param dir      기획 데이터 폴더
// @param monsters 검증된 몬스터 데이터 (참조 대상)
// @param skills   검증된 스킬/상태 효과 데이터 (참조 대상)
// @return 검증을 통과한 불변 데이터(보스가 없을 수도 있음), 실패하면 nullptr (원인은 로그)
//***************************************************************************
std::shared_ptr<const CBossData> CGameDataLoader::LoadBosses(const _tstring& dir, const CMonsterData& monsters, const CHeroData& skills)
{
	const std::filesystem::path base(dir);
	std::shared_ptr<CBossData> data = std::make_shared<CBossData>();

	if( !std::filesystem::exists(base / SBossRow::kFile) )
	{
		LOG_INFO(_T("[GameData] %s not found. no bosses. dir=%s"), SBossRow::kFile, dir.c_str());
		return data;
	}

	std::vector<SBossRow> bossRows;
	std::vector<SBossPhaseRow> phaseRows;
	std::vector<SBossActionRow> actionRows;
	if( !ReadTable(base, bossRows) || !ReadTable(base, phaseRows) || !ReadTable(base, actionRows) )
		return nullptr;

	std::vector<SBossTemplate> bosses;
	bosses.reserve(bossRows.size());
	for( const SBossRow& r : bossRows )
	{
		SBossTemplate t;
		t.id = r.id;
		t.monsterId = r.monsterId;
		t.name = TStringToString(r.name.c_str());
		t.enrageAfterMs = r.enrageAfterMs;
		t.enrageStatusId = r.enrageStatusId;
		bosses.push_back(std::move(t));
	}

	auto findBoss = [&](uint32 id) -> SBossTemplate*
		{
			for( SBossTemplate& b : bosses )
				if( b.id == id )
					return &b;
			return nullptr;
		};
	auto findPhase = [](SBossTemplate& boss, uint32 phase) -> SBossPhaseTemplate*
		{
			for( SBossPhaseTemplate& p : boss.phases )
				if( p.phase == phase )
					return &p;
			return nullptr;
		};

	for( const SBossPhaseRow& r : phaseRows )
	{
		SBossTemplate* boss = findBoss(r.bossId);
		if( boss == nullptr )
		{
			LOG_ERROR(_T("[GameData] %s: unknown BossId. bossId=%u, phase=%u"), SBossPhaseRow::kFile, r.bossId, r.phase);
			return nullptr;
		}
		if( findPhase(*boss, r.phase) != nullptr )
		{
			LOG_ERROR(_T("[GameData] %s: duplicate phase. bossId=%u, phase=%u"), SBossPhaseRow::kFile, r.bossId, r.phase);
			return nullptr;
		}

		SBossPhaseTemplate p;
		p.phase = r.phase;
		p.hpBelowPercent = r.hpBelowPercent;
		p.enterStatusId = r.enterStatusId;
		p.enterHealPercent = r.enterHealPercent;
		p.transitionMs = r.transitionMs;
		boss->phases.push_back(std::move(p));
	}

	for( SBossTemplate& boss : bosses )
		std::sort(boss.phases.begin(), boss.phases.end(), [](const SBossPhaseTemplate& a, const SBossPhaseTemplate& b) { return a.phase < b.phase; });

	for( const SBossActionRow& r : actionRows )
	{
		SBossTemplate* boss = findBoss(r.bossId);
		SBossPhaseTemplate* phase = boss != nullptr ? findPhase(*boss, r.phase) : nullptr;
		if( phase == nullptr )
		{
			LOG_ERROR(_T("[GameData] %s: unknown BossId/Phase. bossId=%u, phase=%u, seq=%u"), SBossActionRow::kFile, r.bossId, r.phase, r.seq);
			return nullptr;
		}

		SBossActionTemplate a;
		a.seq = r.seq;
		a.trigger = r.trigger;
		a.type = r.type;
		a.target = r.target;
		a.skillId = r.skillId;
		a.monsterId = r.monsterId;
		a.count = r.count;
		a.spreadRadius = static_cast<float>(r.spreadRadius);
		a.delayMs = r.delayMs;
		phase->actions.push_back(a);
	}

	for( SBossTemplate& boss : bosses )
	{
		for( SBossPhaseTemplate& phase : boss.phases )
		{
			// OnEnter 행동이 앞, 그다음 Loop 행동. 각각 Seq 오름차순 (Seq 중복은 Validate가 거부한다)
			std::stable_sort(phase.actions.begin(), phase.actions.end(), [](const SBossActionTemplate& a, const SBossActionTemplate& b)
				{
					const int ka = a.trigger == EBossTrigger::OnEnter ? 0 : 1;
					const int kb = b.trigger == EBossTrigger::OnEnter ? 0 : 1;
					return ka != kb ? ka < kb : a.seq < b.seq;
				});

			phase.loopStart = 0;
			while( phase.loopStart < phase.actions.size() && phase.actions[phase.loopStart].trigger == EBossTrigger::OnEnter )
				++phase.loopStart;
		}

		if( !data->AddBoss(boss) )
		{
			LOG_ERROR(_T("[GameData] duplicate boss id or monster id. bossId=%u, monsterId=%u"), boss.id, boss.monsterId);
			return nullptr;
		}
	}

	std::string error;
	if( !data->Validate(monsters, skills, error) )
	{
		LOG_ERROR(_T("[GameData] boss validation failed. %hs"), error.c_str());
		return nullptr;
	}

	LOG_INFO(_T("[GameData] loaded. bosses=%u"), static_cast<uint32>(data->GetBossCount()));
	return data;
}

//***************************************************************************
// @brief 이 존의 장애물을 읽어 차단 맵을 만든다. 실제 읽기와 검증은 LoadBlockMapFile()이 한다(봇 클라이언트와 공유).
// @details Block.json이 없으면 장애물 없는 존으로 보고 성공한다(Monster/Spawn/Hero와 달리 선택 데이터).
//          파일이 있는데 형식이 틀리면 실패한다(장애물 없이 조용히 뜨는 것이 더 위험하다).
// @param dir    기획 데이터 폴더 (Block.json)
// @param zoneId 이 존의 ID (다른 존의 행은 검증만 하고 쓰지 않음)
// @param minX   존 영역 최소 X
// @param minY   존 영역 최소 Y
// @param maxX   존 영역 최대 X
// @param maxY   존 영역 최대 Y
// @param out    [out] 차단 맵. 파일이 없거나 이 존의 행이 없으면 nullptr
// @return 성공(파일이 없는 경우 포함) 시 true, 형식 오류/값 오류/셀 수 초과 시 false (원인은 로그)
//***************************************************************************
bool CGameDataLoader::LoadBlockMap(const _tstring& dir, uint32 zoneId, float minX, float minY, float maxX, float maxY,
	std::shared_ptr<const CBlockMap>& out)
{
	out.reset();

	const std::filesystem::path file = std::filesystem::path(dir) / _T("Block.json");
	if( !std::filesystem::exists(file) )
	{
		LOG_INFO(_T("[GameData] Block.json not found. no obstacles. dir=%s"), dir.c_str());
		return true;
	}

	return LoadBlockMapFile(file.native(), zoneId, minX, minY, maxX, maxY, kBlockCellSize, out);
}