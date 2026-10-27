
//***************************************************************************
// ZoneLinkSession.cpp: implementation of the CZoneLinkSession class.
//
//***************************************************************************

#include "pch.h"
#include "ZoneLinkSession.h"

//***************************************************************************
// @brief CZoneLinkSession 생성자
// @param server 이 세션이 속한 존 서버 (세션 생성 팩토리가 항상 주입)
//***************************************************************************
CZoneLinkSession::CZoneLinkSession(CZoneServerMain* server)
	: _server(server)
{
}

//***************************************************************************
// @brief Gateway 연결 완료 시 호출. 서버에 알려 존 등록을 시작하게 합니다.
//***************************************************************************
void CZoneLinkSession::OnConnected()
{
	if( _server != nullptr )
		_server->OnLinkConnected(std::static_pointer_cast<CZoneLinkSession>(shared_from_this()));
}

//***************************************************************************
// @brief 연결 종료(또는 연결 실패) 시 호출. 서버가 재접속을 예약합니다.
//***************************************************************************
void CZoneLinkSession::OnDisconnected()
{
	if( _server != nullptr )
		_server->OnLinkDisconnected(this);
}

//***************************************************************************
// @brief 수신 데이터 처리. 완전한 패킷 단위만 서버로 전달하고 처리한 총 바이트 수를 반환합니다.
//        프로토콜 위반이면 -1을 반환해 연결을 종료시킵니다.
//***************************************************************************
int32 CZoneLinkSession::OnRecv(BYTE* buffer, int32 len)
{
	return ProcessPacketFrames(buffer, len, kMaxServerPacketBytes,
		[this](const PacketHeader* header) { return _server->OnGatewayPacket(header); });
}
