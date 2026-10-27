
//***************************************************************************
// ZoneLinkSession.h : interface for the CZoneLinkSession class.
//
//***************************************************************************

#ifndef ZONELINKSESSION_H
#define ZONELINKSESSION_H

#include <Network/IOCP/IocpSession.h>

class CZoneServerMain;

//***************************************************************************
// @class CZoneLinkSession
// @brief 존 서버가 Gateway로 맺는 서버 간 연결 세션(클라이언트 측 세션).
// @details 연결/종료 통지는 CZoneServerMain에 위임하고(등록·재접속은 서버 파사드가 전담),
//          수신 패킷은 프레이밍만 한 뒤 CZoneServerMain::OnGatewayPacket()으로 넘깁니다.
//***************************************************************************
class CZoneLinkSession : public CIocpSession
{
public:
	explicit CZoneLinkSession(CZoneServerMain* server);
	virtual ~CZoneLinkSession() = default;

protected:
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

private:
	CZoneServerMain*	_server;	// 이 세션이 속한 존 서버 (서버가 세션보다 오래 살아있음이 보장됨)
};

#endif // ndef ZONELINKSESSION_H
