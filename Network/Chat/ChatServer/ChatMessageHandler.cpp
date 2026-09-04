
//***************************************************************************
// ChatMessageHandler.cpp : Chat 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 채팅 메시지 처리. 기본 뼈대는 로그인한 발신자의 메시지를 전체
	//        브로드캐스트합니다(발신자 제외, 채팅방 분리 등은 범위 밖).
	//***************************************************************************
	void HandleChat(CChatSession& session, const PacketHeader* header)
	{
		const ChatPacket* packet = reinterpret_cast<const ChatPacket*>(header);

		if( !session.IsLoggedIn() )
			return; // 로그인 전 채팅 무시

		if( CChatServerMain* server = session.GetServer() )
			server->Broadcast(packet, packet->size);
	}
}

REGISTER_CHAT_PACKET_HANDLER(Chat, ChatPacket, HandleChat);
