
//***************************************************************************
// ZoneEntity.cpp : implementation of the CZoneEntity class.
//
//***************************************************************************

#include "pch.h"
#include "ZoneEntity.h"

#include <cmath>

//***************************************************************************
// @brief 엔티티를 생성합니다.
// @param id   존 내에서 유일한 엔티티 ID
// @param type 엔티티 종류
//***************************************************************************
CZoneEntity::CZoneEntity(uint64 id, EntityType type)
	: _id(id)
	, _type(type)
	, _speed(0.0f)
	, _moving(false)
	, _inZone(false)
	, _hp(_stats.maxHp)
	, _dead(false)
	, _nextAttackMs(0)
	, _invulnerableUntilMs(0)
	, _respawnTimerId(0)
	, _movingIdx(kInvalidIndex)
{
}

//***************************************************************************
// @brief 소멸자.
//***************************************************************************
CZoneEntity::~CZoneEntity()
{
}

//***************************************************************************
// @brief 스폰 위치를 설정합니다. 존에 등록(AddEntity)되기 전에만 유효하며,
//        등록 이후의 위치 변경은 CFieldZoneBase(이동/순간이동)을 통해서만 가능합니다.
// @param pos 스폰 좌표
//***************************************************************************
void CZoneEntity::SetSpawnPos(const Vec2& pos)
{
	if( _inZone )
		return;

	_pos = pos;
	_dest = pos;
	_spawnPos = pos;
}

//***************************************************************************
// @brief 전투 능력치를 설정하고 체력을 가득 채웁니다. 존에 등록(AddEntity)되기 전에만 유효합니다.
// @details 잘못된 값은 보정한다: maxHp/attackIntervalMs는 1 이상, attack/defense는 0 이상,
//          attackRange는 0 이상의 유한한 값(NaN/Inf는 0).
// @param stats 전투 능력치
//***************************************************************************
void CZoneEntity::SetCombatStats(const SCombatStats& stats)
{
	if( _inZone )
		return;

	_stats = stats;
	if( _stats.maxHp < 1 )
		_stats.maxHp = 1;
	if( _stats.attack < 0 )
		_stats.attack = 0;
	if( _stats.defense < 0 )
		_stats.defense = 0;
	if( !std::isfinite(_stats.attackRange) || _stats.attackRange < 0.0f )
		_stats.attackRange = 0.0f;
	if( _stats.attackIntervalMs < 1 )
		_stats.attackIntervalMs = 1;

	_hp = _stats.maxHp;
}

//***************************************************************************
// @brief 다른 엔티티가 시야(AOI)에 들어왔을 때 호출됩니다.
//        플레이어는 재정의하여 상대의 스폰 패킷을 전송합니다.
// @param other 시야에 들어온 엔티티
//***************************************************************************
void CZoneEntity::OnAoiEnter(CZoneEntity& other)
{
	(void)other;
}

//***************************************************************************
// @brief 다른 엔티티가 시야(AOI)에서 벗어났을 때 호출됩니다.
//        플레이어는 재정의하여 상대의 디스폰 패킷을 전송합니다.
// @param other 시야에서 벗어난 엔티티
//***************************************************************************
void CZoneEntity::OnAoiLeave(CZoneEntity& other)
{
	(void)other;
}