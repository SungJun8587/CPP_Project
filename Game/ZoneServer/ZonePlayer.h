
//***************************************************************************
// ZonePlayer.h : interface for the CZonePlayer class.
//
//***************************************************************************

#ifndef ZONEPLAYER_H
#define ZONEPLAYER_H

#include "../GameCommon/ZoneCore/ZoneEntity.h"

class CBattleZone;

//***************************************************************************
// @class CZonePlayer
// @brief 존에 입장한 플레이어 엔티티.
// @details 엔티티 ID는 charId를 그대로 쓴다. 이후 몬스터/NPC 엔티티가 추가되면 ID 공간이 겹치지 않도록
//          상위 비트로 종류를 구분하는 규약이 필요하다.
//          AOI 진입/이탈 통지를 받아 해당 클라이언트에게 스폰/디스폰 패킷을 보낸다.
//          모든 메서드는 존 스레드에서만 호출된다.
//***************************************************************************
class CZonePlayer : public CZoneEntity
{
public:
	CZonePlayer(uint64 charId, uint64 clientSessionId, CBattleZone* zone);
	virtual ~CZonePlayer() = default;

	//***************************************************************************
	// @brief 이 플레이어의 Gateway 클라이언트 세션 ID를 반환합니다.
	// @return Gateway가 부여한 클라이언트 세션 ID
	//***************************************************************************
	uint64 GetClientSessionId() const { return _clientSessionId; }

protected:
	virtual void	OnAoiEnter(CZoneEntity& other) override;
	virtual void	OnAoiLeave(CZoneEntity& other) override;

private:
	uint64			_clientSessionId;	// Gateway 클라이언트 세션 ID (패킷 전송 대상 식별)
	CBattleZone*	_zone;				// 소속 존 (존이 엔티티보다 오래 살아있음이 보장됨)
};

#endif // ndef ZONEPLAYER_H
