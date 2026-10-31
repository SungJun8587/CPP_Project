
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
// @details 엔티티 ID는 charId를 그대로 쓴다. 최상위 비트(kMonsterIdBase)는 몬스터 전용 대역이므로
//          charId는 이 비트를 쓰지 않아야 하며, 존 입장 시 거부한다.
//          AOI 진입/이탈 통지를 받아 해당 클라이언트에게 스폰/디스폰 패킷을 보낸다.
//          퇴장 중(MarkLeaving 이후)에는 클라이언트로 아무것도 보내지 않는다. 엔티티를 존에서 뺄 때 이웃 수만큼 나가는
//          디스폰 패킷은 이미 끊겼거나 끊기는 클라이언트에게는 낭비이기 때문이다.
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

	//***************************************************************************
	// @brief 퇴장 중으로 표시합니다. 존에서 제거하기 직전에 호출하며, 이후 이 플레이어의 클라이언트로 패킷을 보내지 않습니다.
	//***************************************************************************
	void MarkLeaving() { _leaving = true; }

	//***************************************************************************
	// @brief 퇴장 중인지 반환합니다.
	// @return 퇴장 중이면 true
	//***************************************************************************
	bool IsLeaving() const { return _leaving; }

protected:
	virtual void	OnAoiEnter(CZoneEntity& other) override;
	virtual void	OnAoiLeave(CZoneEntity& other) override;

private:
	uint64			_clientSessionId;	// Gateway 클라이언트 세션 ID (패킷 전송 대상 식별)
	CBattleZone* _zone;				// 소속 존 (존이 엔티티보다 오래 살아있음이 보장됨)
	bool			_leaving;			// 퇴장 중 여부 (true면 클라이언트로 패킷을 보내지 않음)
};

#endif // ndef ZONEPLAYER_H