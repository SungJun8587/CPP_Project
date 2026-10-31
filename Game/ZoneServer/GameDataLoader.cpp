
//***************************************************************************
// GameDataLoader.cpp: implementation of the CGameDataLoader class.
//
//***************************************************************************

#include "pch.h"
#include "GameDataLoader.h"
#include <ConfigFile/JSONConfigFieldReader.h>

#include <algorithm>
#include <filesystem>

namespace
{
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
	// @brief <dir>/<file> JSON을 열어 table 키의 행 배열을 읽는다. 파일/키가 없으면 실패한다.
	//        (기획 데이터가 통째로 빠진 배포 실수를 빈 테이블로 넘기지 않기 위함)
	//***************************************************************************
	template <typename TRow>
	bool ReadTable(const std::filesystem::path& file, const TCHAR* table, std::initializer_list<SFieldSpec> specs, std::vector<TRow>& rows)
	{
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
		reader.ReadObjects(table, rows, specs);
		return reader.Validate();
	}

	constexpr int64 kU32Max = 0xFFFFFFFFll;
	constexpr int64 kI32Max = 0x7FFFFFFFll;
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
	if( !ReadTable(base / _T("Monster.json"), _T("Monster"),
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
		}, monsters) )
		return nullptr;

	std::vector<SSpawnRow> spawns;
	if( !ReadTable(base / _T("Spawn.json"), _T("Spawn"),
		{
			{ _T("ZoneId"), EFieldType::UInt, 0, kU32Max },
			{ _T("MonsterId"), EFieldType::UInt, 1, kU32Max },
			{ _T("Count"), EFieldType::UInt, 1, kU32Max },
			{ _T("X"), EFieldType::Number },
			{ _T("Y"), EFieldType::Number },
			{ _T("Radius"), EFieldType::Number },
		}, spawns) )
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
