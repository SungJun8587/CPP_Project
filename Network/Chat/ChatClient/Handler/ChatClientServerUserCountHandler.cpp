
//***************************************************************************
// ChatClientServerUserCountHandler.cpp : ServerUserCountRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief RequestServerUserCount()로 보낸 폴링 요청에 대한 서버 응답.
	//***************************************************************************
	void HandleServerUserCountRes(CChatClientSession& session, const PacketHeader* header)
	{
		const ServerUserCountResPacket* packet = reinterpret_cast<const ServerUserCountResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnServerUserCountResult(packet->userCount, packet->lobbyUserCount);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(ServerUserCountRes, ServerUserCountResPacket, HandleServerUserCountRes);