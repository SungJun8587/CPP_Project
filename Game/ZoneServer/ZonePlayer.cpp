
//***************************************************************************
// ZonePlayer.cpp: implementation of the CZonePlayer class.
//
//***************************************************************************

#include "pch.h"
#include "ZonePlayer.h"
#include "BattleZone.h"

//***************************************************************************
// @brief 플레이어 엔티티를 생성한다.
// @param charId          캐릭터 ID (엔티티 ID로 사용)
// @param clientSessionId Gateway 클라이언트 세션 ID
// @param zone            소속 존
// @param heroId          선택한 영웅 ID
//***************************************************************************
CZonePlayer::CZonePlayer(uint64 charId, uint64 clientSessionId, CBattleZone* zone, uint32 heroId)
	: CZoneEntity(charId, EntityType::Player)
	, _clientSessionId(clientSessionId)
	, _heroId(heroId)
	, _zone(zone)
	, _leaving(false)
{
}

//***************************************************************************
// @brief 다른 엔티티가 시야에 들어왔을 때 호출. 이 플레이어의 클라이언트에 스폰 패킷을 보낸다.
// @param other 시야에 들어온 엔티티
//***************************************************************************
void CZonePlayer::OnAoiEnter(CZoneEntity& other)
{
	if( _zone != nullptr )
		_zone->SendSpawn(*this, other);
}

//***************************************************************************
// @brief 다른 엔티티가 시야에서 벗어났을 때 호출. 이 플레이어의 클라이언트에 디스폰 패킷을 보낸다.
// @param other 시야에서 벗어난 엔티티
//***************************************************************************
void CZonePlayer::OnAoiLeave(CZoneEntity& other)
{
	if( _zone != nullptr && !_leaving )
		_zone->SendDespawn(*this, other);
}