
//***************************************************************************
// MonsterData.cpp : implementation of the CMonsterData class.
//
//***************************************************************************

#include "pch.h"
#include "MonsterData.h"

#include <cmath>
#include <cstdio>
#include <map>

namespace
{
	//***************************************************************************
	// @brief 오류 문장을 만듭니다.
	//***************************************************************************
	std::string Format(const char* fmt, unsigned a)
	{
		char buf[192];
		std::snprintf(buf, sizeof(buf), fmt, a);
		return buf;
	}

	bool IsFinitePositive(float v) { return std::isfinite(v) && v > 0.0f; }
	bool IsFiniteNonNegative(float v) { return std::isfinite(v) && v >= 0.0f; }
}

//***************************************************************************
// @brief 몬스터 템플릿을 추가합니다.
// @param monster 추가할 템플릿
// @return 추가했으면 true, ID가 이미 있으면 false
//***************************************************************************
bool CMonsterData::AddMonster(const SMonsterTemplate& monster)
{
	return _monsters.emplace(monster.id, monster).second;
}

//***************************************************************************
// @brief 스폰 지점을 추가합니다.
// @param spawn 추가할 스폰 지점
//***************************************************************************
void CMonsterData::AddSpawn(const SSpawnEntry& spawn)
{
	_spawns.push_back(spawn);
}

//***************************************************************************
// @brief 데이터의 값 범위와 테이블 간 참조를 검사합니다. 첫 번째 오류만 설명으로 돌려줍니다.
//        검사 항목:
//        - 템플릿: ID != 0, maxHp >= 1, attack/defense >= 0, 사거리/속도/간격 > 0, 시야 거리 유한,
//                  leashRange >= aggroRange, respawnMs >= 500
//        - 스폰: 템플릿 존재, count >= 1, 좌표/반경 유한, 존당 총 마릿수 <= kMaxSpawnPerZone
//        존의 영역/AOI에 의존하는 검사는 CMonsterSystem::Validate()가 맡는다.
// @param error [out] 오류 설명
// @return 문제가 없으면 true
//***************************************************************************
bool CMonsterData::Validate(std::string& error) const
{
	for( const auto& pair : _monsters )
	{
		const SMonsterTemplate& m = pair.second;
		const unsigned id = m.id;

		if( m.id == 0 )						{ error = "monster id must not be 0"; return false; }
		if( m.stats.maxHp < 1 )				{ error = Format("monster %u: maxHp must be >= 1", id); return false; }
		if( m.stats.attack < 0 )			{ error = Format("monster %u: attack must be >= 0", id); return false; }
		if( m.stats.defense < 0 )			{ error = Format("monster %u: defense must be >= 0", id); return false; }
		if( !IsFinitePositive(m.stats.attackRange) )	{ error = Format("monster %u: attackRange must be > 0", id); return false; }
		if( m.stats.attackIntervalMs < 1 )	{ error = Format("monster %u: attackIntervalMs must be >= 1", id); return false; }
		if( !IsFinitePositive(m.speed) )	{ error = Format("monster %u: speed must be > 0", id); return false; }
		if( !IsFiniteNonNegative(m.aggroRange) )		{ error = Format("monster %u: aggroRange must be >= 0", id); return false; }
		if( !IsFiniteNonNegative(m.leashRange) || m.leashRange < m.aggroRange )
													{ error = Format("monster %u: leashRange must be >= aggroRange", id); return false; }
		if( m.respawnMs < 500 )				{ error = Format("monster %u: respawnMs must be >= 500", id); return false; }
	}

	std::map<uint32, uint64> perZone;
	for( size_t i = 0; i < _spawns.size(); ++i )
	{
		const SSpawnEntry& s = _spawns[i];
		const unsigned row = static_cast<unsigned>(i);

		if( FindMonster(s.monsterId) == nullptr )
		{
			error = Format("spawn row %u: unknown monster id", row);
			return false;
		}
		if( s.count < 1 )
		{
			error = Format("spawn row %u: count must be >= 1", row);
			return false;
		}
		if( !std::isfinite(s.x) || !std::isfinite(s.y) || !IsFiniteNonNegative(s.radius) )
		{
			error = Format("spawn row %u: x/y must be finite and radius >= 0", row);
			return false;
		}

		uint64& total = perZone[s.zoneId];
		total += s.count;
		if( total > kMaxSpawnPerZone )
		{
			error = Format("zone %u: total spawn count exceeds the limit", s.zoneId);
			return false;
		}
	}

	return true;
}
