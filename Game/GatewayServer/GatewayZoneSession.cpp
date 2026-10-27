
//***************************************************************************
// GatewayZoneSession.cpp : implementation of the CGatewayZoneSession class.
//
//***************************************************************************

#include "pch.h"
#include "GatewayZoneSession.h"

//***************************************************************************
// @brief CGatewayZoneSession 생성자
// @param server 이 세션이 속한 Gateway (세션 생성 팩토리가 항상 주입)
//***************************************************************************
CGatewayZoneSession::CGatewayZoneSession(CGatewayServerMain* server)
	: _server(server)
{
}

//***************************************************************************
// @brief 연결 종료 시 호출. 등록된 존이었다면 라우팅 테이블 제거와 클라이언트 정리를 서버에 위임합니다.
//***************************************************************************
void CGatewayZoneSession::OnDisconnected()
{
	if( _server != nullptr && GetZoneId() != 0 )
		_server->OnZoneSessionClosed(this);
}

//***************************************************************************
// @brief 수신 데이터 처리. 완전한 패킷 단위만 서버로 전달하고 처리한 총 바이트 수를 반환합니다.
//        프로토콜 위반이면 -1을 반환해 연결을 종료시킵니다.
//***************************************************************************
int32 CGatewayZoneSession::OnRecv(BYTE* buffer, int32 len)
{
	return ProcessPacketFrames(buffer, len, kMaxServerPacketBytes,
		[this](const PacketHeader* header) { return _server->OnZonePacket(*this, header); });
}
