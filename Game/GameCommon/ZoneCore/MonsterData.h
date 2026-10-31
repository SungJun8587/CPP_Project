
//***************************************************************************
// MonsterData.h : 몬스터 템플릿/스폰 테이블 데이터 (서버 구동 시 로드되는 불변 데이터)
//
//***************************************************************************

#ifndef MONSTERDATA_H
#define MONSTERDATA_H

#include <BaseRedefineDataType.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "ZoneEntity.h"

//***************************************************************************
// @struct SMonsterTemplate
// @brief 몬스터 종류별 데이터. 기획 데이터(엑셀)의 Monster 시트 한 행에 대응한다.
// @details
// - aggroRange : 이 거리 안에 들어온 플레이어를 먼저 공격 대상으로 삼는다. 0이면 선공하지 않고 맞았을 때만 반응한다.
//                AOI 최소 보장 시야 이하여야 한다(시야 밖 플레이어는 AI가 깨어나지 않으므로).
// - leashRange : 스폰 지점에서 이 거리 이상 멀어지면 추적을 포기하고 돌아가 체력을 회복한다.
// - respawnMs  : 사망 후 부활까지의 시간
//***************************************************************************
struct SMonsterTemplate
{
	uint32			id = 0;						// 템플릿 ID (유일)
	std::string		name;						// 이름 (UTF-8, 로그/디버깅용)
	SCombatStats	stats;						// 전투 능력치
	float			speed = 3.0f;				// 이동 속도 (units/sec)
	float			aggroRange = 8.0f;			// 선공 감지 거리 (units)
	float			leashRange = 30.0f;			// 추적 한계 거리 (units)
	uint32			respawnMs = 10000;			// 부활 시간 (ms)
};

//***************************************************************************
// @struct SSpawnEntry
// @brief 몬스터 스폰 지점. 기획 데이터의 Spawn 시트 한 행에 대응한다.
//***************************************************************************
struct SSpawnEntry
{
	uint32	zoneId = 0;			// 스폰될 존 ID
	uint32	monsterId = 0;		// 몬스터 템플릿 ID
	uint32	count = 1;			// 마릿수
	float	x = 0.0f;			// 스폰 중심 X
	float	y = 0.0f;			// 스폰 중심 Y
	float	radius = 0.0f;		// 스폰 반경. 최초 스폰과 부활 위치를 이 원 안에서 무작위로 정한다
};

//***************************************************************************
// @class CMonsterData
// @brief 몬스터 템플릿과 스폰 테이블 모음. 로드가 끝나면 변경하지 않으며, shared_ptr<const>로 존들이 공유한다.
// @details 템플릿 포인터(FindMonster 결과)는 이 객체가 살아 있는 동안 유효하다(노드 기반 맵이라 추가로 무효화되지 않음).
//          Validate()는 값 범위와 테이블 간 참조(스폰 -> 템플릿)를 검사하며, 통과해야 서버가 구동된다.
//***************************************************************************
class CMonsterData
{
public:
	static constexpr size_t kMaxSpawnPerZone = 20000;	// 한 존의 최대 몬스터 수 (설정 실수로 메모리가 폭주하는 것을 막는다)

	bool AddMonster(const SMonsterTemplate& monster);
	void AddSpawn(const SSpawnEntry& spawn);
	bool Validate(std::string& error) const;

	//***************************************************************************
	// @brief 템플릿을 ID로 찾습니다.
	// @param id 템플릿 ID
	// @return 템플릿, 없으면 nullptr
	//***************************************************************************
	const SMonsterTemplate* FindMonster(uint32 id) const
	{
		auto it = _monsters.find(id);
		return it != _monsters.end() ? &it->second : nullptr;
	}

	//***************************************************************************
	// @brief 전체 스폰 테이블을 반환합니다.
	//***************************************************************************
	const std::vector<SSpawnEntry>& GetSpawns() const { return _spawns; }

	//***************************************************************************
	// @brief 등록된 템플릿 수를 반환합니다.
	//***************************************************************************
	size_t GetMonsterCount() const { return _monsters.size(); }

private:
	std::unordered_map<uint32, SMonsterTemplate>	_monsters;	// 템플릿 ID -> 템플릿
	std::vector<SSpawnEntry>						_spawns;	// 스폰 테이블
};

#endif // ndef MONSTERDATA_H