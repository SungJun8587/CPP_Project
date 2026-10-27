
//***************************************************************************
// GatewayZoneSession.h : interface for the CGatewayZoneSession class.
//
//***************************************************************************

#ifndef GATEWAYZONESESSION_H
#define GATEWAYZONESESSION_H

#include <Network/IOCP/IocpSession.h>

#include "ZoneRouter.h"

#include <atomic>
#include <memory>

class CGatewayServerMain;

//***************************************************************************
// @class CGatewayZoneSession
// @brief Gateway가 존 서버로부터 받는 서버 간 연결 세션(서버 측 세션).
// @details 존 서버가 ZoneRegisterReq로 등록하기 전까지는 어떤 패킷도 처리하지 않는다.
//          등록 이후의 처리는 CGatewayServerMain::OnZonePacket()이 전담한다.
//          연결 종료 시 라우팅 테이블 제거와 해당 존의 클라이언트 정리를 서버 파사드에 위임한다.
//***************************************************************************
class CGatewayZoneSession : public CIocpSession
{
public:
	explicit CGatewayZoneSession(CGatewayServerMain* server);
	virtual ~CGatewayZoneSession() = default;

	//***************************************************************************
	// @brief 이 세션이 등록한 존 ID를 반환합니다.
	// @return 등록 전이면 0
	//***************************************************************************
	uint32 GetZoneId() const { return _zoneId.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 존 등록 결과를 세션에 반영합니다. 등록 핸들러(CGatewayServerMain)만 호출한다.
	// @param zoneId 등록된 존 ID
	// @param node   등록된 라우팅 노드
	//***************************************************************************
	void BindZone(uint32 zoneId, std::shared_ptr<SZoneNode> node)
	{
		_node = std::move(node);
		_zoneId.store(zoneId, std::memory_order_release);
	}

	//***************************************************************************
	// @brief 이 세션이 등록한 라우팅 노드를 반환합니다. 이 세션의 수신 처리 스레드에서만 호출한다.
	// @return 노드, 등록 전이면 nullptr
	//***************************************************************************
	const std::shared_ptr<SZoneNode>& GetNode() const { return _node; }

protected:
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

private:
	CGatewayServerMain*			_server;			// 이 세션이 속한 Gateway (서버가 세션보다 오래 살아있음이 보장됨)
	std::atomic<uint32>			_zoneId{ 0 };		// 등록한 존 ID (0 = 미등록)
	std::shared_ptr<SZoneNode>	_node;				// 등록한 라우팅 노드 (수신 처리 스레드 전용)
};

#endif // ndef GATEWAYZONESESSION_H
