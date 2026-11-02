
//***************************************************************************
// GameDataLoader.cpp: implementation of the CGameDataLoader class.
//
//***************************************************************************

#include "pch.h"
#include "GameDataLoader.h"
#include "../GameCommon/ZoneCore/BlockMapFile.h"
#include <ConfigFile/JSONConfigFieldReader.h>

#include <algorithm>
#include <filesystem>
#include <initializer_list>

namespace
{
	constexpr int64 kU32Max = 0xFFFFFFFFll;
	constexpr int64 kI32Max = 0x7FFFFFFFll;

	//***************************************************************************
	// @brief Hero.json의 한 행.
	//***************************************************************************
	struct SHeroRow
	{
		uint32		id = 0;
		_tstring	name;
		int32		maxHp = 0;
		int32		attack = 0;
		int32		defense = 0;
		double		attackRange = 0;
		uint32		attackIntervalMs = 0;
		double		speed = 0;

		static constexpr const TCHAR* kFile = _T("Hero.json");
		static constexpr const TCHAR* kTable = _T("Hero");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1, kU32Max },
				{ _T("Name"), EFieldType::String },
				{ _T("MaxHp"), EFieldType::Int, 1, kI32Max },
				{ _T("Attack"), EFieldType::Int, 0, kI32Max },
				{ _T("Defense"), EFieldType::Int, 0, kI32Max },
				{ _T("AttackRange"), EFieldType::Number },
				{ _T("AttackIntervalMs"), EFieldType::UInt, 1, kU32Max },
				{ _T("Speed"), EFieldType::Number },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), id, allocator);
			value.AddMember(_T("Name"), _tValue(name.c_str(), allocator), allocator);
			value.AddMember(_T("MaxHp"), maxHp, allocator);
			value.AddMember(_T("Attack"), attack, allocator);
			value.AddMember(_T("Defense"), defense, allocator);
			value.AddMember(_T("AttackRange"), attackRange, allocator);
			value.AddMember(_T("AttackIntervalMs"), attackIntervalMs, allocator);
			value.AddMember(_T("Speed"), speed, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			id = value[_T("Id")].GetUint();
			name = value[_T("Name")].GetString();
			maxHp = value[_T("MaxHp")].GetInt();
			attack = value[_T("Attack")].GetInt();
			defense = value[_T("Defense")].GetInt();
			attackRange = value[_T("AttackRange")].GetDouble();
			attackIntervalMs = value[_T("AttackIntervalMs")].GetUint();
			speed = value[_T("Speed")].GetDouble();
		}
	};

	//***************************************************************************
	// @brief Monster.json의 한 행. 필드 존재/타입은 읽기 전에 SFieldSpec으로 검증하므로 FromJSON은 값만 옮긴다.
	//***************************************************************************
	struct SMonsterRow
	{
		uint32		id = 0;
		_tstring	name;
		int32		maxHp = 0;
		int32		attack = 0;
		int32		defense = 0;
		double		attackRange = 0;
		uint32		attackIntervalMs = 0;
		double		speed = 0;
		double		aggroRange = 0;
		double		leashRange = 0;
		uint32		respawnMs = 0;

		static constexpr const TCHAR* kFile = _T("Monster.json");
		static constexpr const TCHAR* kTable = _T("Monster");

		// 필드 이름/타입/범위의 단일 출처. FromJSON/ToJSON과 같은 구조체에 두어 컬럼을 바꿀 때 한 곳에서 맞춘다.
		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1, kU32Max },
				{ _T("Name"), EFieldType::String },
				{ _T("MaxHp"), EFieldType::Int, 1, kI32Max },
				{ _T("Attack"), EFieldType::Int, 0, kI32Max },
				{ _T("Defense"), EFieldType::Int, 0, kI32Max },
				{ _T("AttackRange"), EFieldType::Number },
				{ _T("AttackIntervalMs"), EFieldType::UInt, 1, kU32Max },
				{ _T("Speed"), EFieldType::Number },
				{ _T("AggroRange"), EFieldType::Number },
				{ _T("LeashRange"), EFieldType::Number },
				{ _T("RespawnMs"), EFieldType::UInt, 0, kU32Max },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), id, allocator);
			value.AddMember(_T("Name"), _tValue(name.c_str(), allocator), allocator);
			value.AddMember(_T("MaxHp"), maxHp, allocator);
			value.AddMember(_T("Attack"), attack, allocator);
			value.AddMember(_T("Defense"), defense, allocator);
			value.AddMember(_T("AttackRange"), attackRange, allocator);
			value.AddMember(_T("AttackIntervalMs"), attackIntervalMs, allocator);
			value.AddMember(_T("Speed"), speed, allocator);
			value.AddMember(_T("AggroRange"), aggroRange, allocator);
			value.AddMember(_T("LeashRange"), leashRange, allocator);
			value.AddMember(_T("RespawnMs"), respawnMs, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			id = value[_T("Id")].GetUint();
			name = value[_T("Name")].GetString();
			maxHp = value[_T("MaxHp")].GetInt();
			attack = value[_T("Attack")].GetInt();
			defense = value[_T("Defense")].GetInt();
			attackRange = value[_T("AttackRange")].GetDouble();
			attackIntervalMs = value[_T("AttackIntervalMs")].GetUint();
			speed = value[_T("Speed")].GetDouble();
			aggroRange = value[_T("AggroRange")].GetDouble();
			leashRange = value[_T("LeashRange")].GetDouble();
			respawnMs = value[_T("RespawnMs")].GetUint();
		}
	};

	//***************************************************************************
	// @brief Spawn.json의 한 행.
	//***************************************************************************
	struct SSpawnRow
	{
		uint32		zoneId = 0;
		uint32		monsterId = 0;
		uint32		count = 0;
		double		x = 0;
		double		y = 0;
		double		radius = 0;

		static constexpr const TCHAR* kFile = _T("Spawn.json");
		static constexpr const TCHAR* kTable = _T("Spawn");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("ZoneId"), EFieldType::UInt, 0, kU32Max },
				{ _T("MonsterId"), EFieldType::UInt, 1, kU32Max },
				{ _T("Count"), EFieldType::UInt, 1, kU32Max },
				{ _T("X"), EFieldType::Number },
				{ _T("Y"), EFieldType::Number },
				{ _T("Radius"), EFieldType::Number },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("ZoneId"), zoneId, allocator);
			value.AddMember(_T("MonsterId"), monsterId, allocator);
			value.AddMember(_T("Count"), count, allocator);
			value.AddMember(_T("X"), x, allocator);
			value.AddMember(_T("Y"), y, allocator);
			value.AddMember(_T("Radius"), radius, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			zoneId = value[_T("ZoneId")].GetUint();
			monsterId = value[_T("MonsterId")].GetUint();
			count = value[_T("Count")].GetUint();
			x = value[_T("X")].GetDouble();
			y = value[_T("Y")].GetDouble();
			radius = value[_T("Radius")].GetDouble();
		}
	};

	//***************************************************************************
	// @brief Skill.json의 한 행.
	//***************************************************************************
	struct SSkillRow
	{
		uint32		id = 0;
		_tstring	name;
		_tstring	shape;
		uint32		damagePercent = 0;
		int32		flatDamage = 0;
		double		range = 0;
		double		radius = 0;
		uint32		cooldownMs = 0;
		uint32		maxTargets = 0;

		static constexpr const TCHAR* kFile = _T("Skill.json");
		static constexpr const TCHAR* kTable = _T("Skill");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1, kU32Max },
				{ _T("Name"), EFieldType::String },
				{ _T("Shape"), EFieldType::String },
				{ _T("DamagePercent"), EFieldType::UInt, 0, kU32Max },
				{ _T("FlatDamage"), EFieldType::Int, 0, kI32Max },
				{ _T("Range"), EFieldType::Number },
				{ _T("Radius"), EFieldType::Number },
				{ _T("CooldownMs"), EFieldType::UInt, 1, kU32Max },
				{ _T("MaxTargets"), EFieldType::UInt, 1, kU32Max },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), id, allocator);
			value.AddMember(_T("Name"), _tValue(name.c_str(), allocator), allocator);
			value.AddMember(_T("Shape"), _tValue(shape.c_str(), allocator), allocator);
			value.AddMember(_T("DamagePercent"), damagePercent, allocator);
			value.AddMember(_T("FlatDamage"), flatDamage, allocator);
			value.AddMember(_T("Range"), range, allocator);
			value.AddMember(_T("Radius"), radius, allocator);
			value.AddMember(_T("CooldownMs"), cooldownMs, allocator);
			value.AddMember(_T("MaxTargets"), maxTargets, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			id = value[_T("Id")].GetUint();
			name = value[_T("Name")].GetString();
			shape = value[_T("Shape")].GetString();
			damagePercent = value[_T("DamagePercent")].GetUint();
			flatDamage = value[_T("FlatDamage")].GetInt();
			range = value[_T("Range")].GetDouble();
			radius = value[_T("Radius")].GetDouble();
			cooldownMs = value[_T("CooldownMs")].GetUint();
			maxTargets = value[_T("MaxTargets")].GetUint();
		}
	};

	//***************************************************************************
	// @brief HeroSkill.json의 한 행. 영웅이 쓸 수 있는 스킬을 연결한다.
	//***************************************************************************
	struct SHeroSkillRow
	{
		uint32		heroId = 0;
		uint32		skillId = 0;

		static constexpr const TCHAR* kFile = _T("HeroSkill.json");
		static constexpr const TCHAR* kTable = _T("HeroSkill");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("HeroId"), EFieldType::UInt, 1, kU32Max },
				{ _T("SkillId"), EFieldType::UInt, 1, kU32Max },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("HeroId"), heroId, allocator);
			value.AddMember(_T("SkillId"), skillId, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			heroId = value[_T("HeroId")].GetUint();
			skillId = value[_T("SkillId")].GetUint();
		}
	};

	//***************************************************************************
	// @brief <dir>/TRow::kFile JSON을 열어 TRow::kTable 키의 행 배열을 읽는다. 파일/키가 없으면 실패한다.
	//        (기획 데이터가 통째로 빠진 배포 실수를 빈 테이블로 넘기지 않기 위함)
	// @details 행 타입이 파일 이름, 테이블 키, 필드 규칙(Specs)을 함께 가지므로 새 테이블은 행 타입만 추가하면 된다.
	//***************************************************************************
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
		return reader.Validate();
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
			if( r.shape == _T("Single") )
				s.shape = ESkillShape::Single;
			else if( r.shape == _T("Area") )
				s.shape = ESkillShape::Area;
			else
			{
				LOG_ERROR(_T("[GameData] skill %u: Shape must be \"Single\" or \"Area\". value=%s"), r.id, r.shape.c_str());
				return nullptr;
			}
			s.damagePercent = r.damagePercent;
			s.flatDamage = r.flatDamage;
			s.range = static_cast<float>(r.range);
			s.radius = static_cast<float>(r.radius);
			s.cooldownMs = r.cooldownMs;
			s.maxTargets = r.maxTargets;

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

	LOG_INFO(_T("[GameData] loaded. heroes=%u, skills=%u"), static_cast<uint32>(data->GetHeroCount()), static_cast<uint32>(data->GetSkillCount()));
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