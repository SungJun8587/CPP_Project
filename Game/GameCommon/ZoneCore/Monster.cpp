
//***************************************************************************
// Monster.cpp : implementation of the CMonster class.
//
//***************************************************************************

#include "pch.h"
#include "Monster.h"
#include "MonsterSystem.h"

//***************************************************************************
// @brief 몬스터를 생성합니다. 능력치/속도/스폰 위치는 템플릿에서 채운다.
// @param id          몬스터 엔티티 ID (kMonsterIdBase 대역)
// @param tmpl        몬스터 템플릿 (이 객체가 살아 있는 동안 유효해야 함)
// @param system      AI 시스템
// @param anchor      스폰 중심점
// @param spawnRadius 스폰 반경
//***************************************************************************
CMonster::CMonster(uint64 id, const SMonsterTemplate& tmpl, CMonsterSystem* system, const Vec2& anchor, float spawnRadius)
	: CZoneEntity(id, EntityType::Monster)
	, _tmpl(&tmpl)
	, _system(system)
	, _anchor(anchor)
	, _spawnRadius(spawnRadius)
	, _state(EMonsterState::Idle)
	, _nearPlayers(0)
	, _active(false)
	, _activeIdx(0)
	, _nextThinkMs(0)
	, _targetId(0)
	, _aggroCount(0)
{
	SetCombatStats(tmpl.stats);
	SetSpeed(tmpl.speed);
}

//***************************************************************************
// @brief 어그로 표에 위협을 더합니다. 표가 가득 차면 가장 낮은 위협 항목을 밀어내되, 새 위협이 그보다 클 때만 들어간다.
// @param entityId 위협을 준 엔티티 ID
// @param amount   더할 위협 수치 (0 이하는 무시)
//***************************************************************************
void CMonster::AddThreat(uint64 entityId, int64 amount)
{
	if( amount <= 0 )
		return;

	for( size_t i = 0; i < _aggroCount; ++i )
	{
		if( _aggro[i].entityId == entityId )
		{
			// 오버플로 방지: 한계를 넘으면 포화시킨다.
			_aggro[i].threat = amount > INT64_MAX - _aggro[i].threat ? INT64_MAX : _aggro[i].threat + amount;
			return;
		}
	}

	if( _aggroCount < kMaxAggro )
	{
		_aggro[_aggroCount].entityId = entityId;
		_aggro[_aggroCount].threat = amount;
		++_aggroCount;
		return;
	}

	size_t lowest = 0;
	for( size_t i = 1; i < _aggroCount; ++i )
	{
		if( _aggro[i].threat < _aggro[lowest].threat )
			lowest = i;
	}

	if( amount > _aggro[lowest].threat )
	{
		_aggro[lowest].entityId = entityId;
		_aggro[lowest].threat = amount;
	}
}

//***************************************************************************
// @brief 어그로 표에서 항목을 제거합니다. 대상이 사라졌거나 공격할 수 없을 때 호출한다.
// @param entityId 제거할 엔티티 ID
//***************************************************************************
void CMonster::RemoveThreat(uint64 entityId)
{
	for( size_t i = 0; i < _aggroCount; ++i )
	{
		if( _aggro[i].entityId == entityId )
		{
			_aggro[i] = _aggro[_aggroCount - 1];
			--_aggroCount;

			if( _targetId == entityId )
				_targetId = 0;
			return;
		}
	}
}

//***************************************************************************
// @brief 어그로 표와 현재 대상을 비웁니다.
//***************************************************************************
void CMonster::ClearAggro()
{
	_aggroCount = 0;
	_targetId = 0;
}

//***************************************************************************
// @brief 시야에 엔티티가 들어왔을 때 호출됩니다. 플레이어면 카운트를 올리고, 첫 플레이어가 보이면 AI를 깨운다.
// @param other 시야에 들어온 엔티티
//***************************************************************************
void CMonster::OnAoiEnter(CZoneEntity& other)
{
	if( other.GetType() != EntityType::Player )
		return;

	if( ++_nearPlayers == 1 && IsAlive() && _system != nullptr )
		_system->Activate(*this);
}

//***************************************************************************
// @brief 시야에서 엔티티가 벗어났을 때 호출됩니다. 비활성화는 다음 사고에서 대기 상태일 때 이루어진다.
// @param other 시야에서 벗어난 엔티티
//***************************************************************************
void CMonster::OnAoiLeave(CZoneEntity& other)
{
	if( other.GetType() == EntityType::Player && _nearPlayers > 0 )
		--_nearPlayers;
}
