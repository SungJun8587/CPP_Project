
//***************************************************************************
// ChatClientMessageHandler.cpp : Chat 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 서버가 브로드캐스트한 채팅 메시지를 받았을 때.
	//***************************************************************************
	void HandleChat(CChatClientSession& session, const PacketHeader* header)
	{
		const ChatPacket* packet = reinterpret_cast<const ChatPacket*>(header);

		// message[256]이 NUL로 안 끝났을 가능성 방어
		char safeBuf[sizeof(packet->message) + 1] = {};
		::memcpy(safeBuf, packet->message, sizeof(packet->message));

		if( CChatClientMain* client = session.GetClient() )
			client->OnChatReceived(safeBuf);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(Chat, ChatPacket, HandleChat);
