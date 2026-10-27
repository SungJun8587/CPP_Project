
//***************************************************************************
// GatewayClientSession.cpp: implementation of the CGatewayClientSession class.
//
//***************************************************************************

#include "pch.h"
#include "GatewayClientSession.h"

//***************************************************************************
// @brief CGatewayClientSession 생성자
// @param server 이 세션이 속한 Gateway (세션 생성 팩토리가 항상 주입)
//***************************************************************************
CGatewayClientSession::CGatewayClientSession(CGatewayServerMain* server)
	: _server(server)
{
}

//***************************************************************************
// @brief 연결 완료 시 호출. 응답 라우팅을 위해 서버의 클라이언트 레지스트리에 등록합니다.
//***************************************************************************
void CGatewayClientSession::OnConnected()
{
	if( _server != nullptr )
		_server->OnClientConnected(std::static_pointer_cast<CGatewayClientSession>(shared_from_this()));
}

//***************************************************************************
// @brief 연결 종료 시 호출. 존에 있었다면 퇴장을 통지하고 레지스트리에서 제거합니다.
//***************************************************************************
void CGatewayClientSession::OnDisconnected()
{
	if( _server != nullptr )
		_server->OnClientDisconnected(*this);
}

//***************************************************************************
// @brief 수신 데이터 처리. 완전한 패킷 단위만 서버로 전달하고 처리한 총 바이트 수를 반환합니다.
//        프로토콜 위반(크기 오류, 속도 제한 초과, 알 수 없는 패킷)이면 -1을 반환해 연결을 종료시킵니다.
//***************************************************************************
int32 CGatewayClientSession::OnRecv(BYTE* buffer, int32 len)
{
	return ProcessPacketFrames(buffer, len, kMaxClientPacketBytes,
		[this](const PacketHeader* header) { return _server->OnClientPacket(*this, header); });
}
