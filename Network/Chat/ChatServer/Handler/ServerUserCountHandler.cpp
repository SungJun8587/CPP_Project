
//***************************************************************************
// ServerUserCountHandler.cpp : ServerUserCountReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 서버 전체 접속자 수(동접자수) 조회 요청 처리 — 클라이언트가
	//        폴링으로 주기적으로 보낸다. 로그인 여부와 무관하게 응답한다
	//        (서버 전체 통계라 특정 세션의 로그인 상태에 의존하지 않음).
	//***************************************************************************
	void HandleServerUserCountReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		ServerUserCountResPacket res{};
		res.size = sizeof(res);
		res.type = static_cast<uint16>(EChatPacketType::ServerUserCountRes);
		res.userCount = server->GetServerUserCount();
		res.lobbyUserCount = server->GetRoomUserCount(kLobbyRoomId);

		session.Send(&res, sizeof(res));
	}
}

REGISTER_CHAT_PACKET_HANDLER(ServerUserCountReq, ServerUserCountReqPacket, HandleServerUserCountReq);