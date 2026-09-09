
//***************************************************************************
// ChatMessageHandler.cpp : Chat 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <cstring>
namespace
{
	//***************************************************************************
	// @brief 채팅 메시지 처리. 기본 뼈대는 로그인한 발신자의 메시지를 전체
	//        브로드캐스트합니다(발신자 제외, 채팅방 분리 등은 범위 밖).
	// @details [수정] 받은 패킷을 그대로 재브로드캐스트하지 않는다 — 클라이언트가
	//          보낸 nickname 필드는 신뢰하지 않고(자기 자신을 다른 사람인 척
	//          꾸며 보낼 수 있으므로), 세션이 실제로 로그인한 닉네임
	//          (session.GetNickname())으로 새로 채운 패킷을 만들어 보낸다.
	//***************************************************************************
	void HandleChat(CChatSession& session, const PacketHeader* header)
	{
		const ChatPacket* packet = reinterpret_cast<const ChatPacket*>(header);

		if( !session.IsLoggedIn() )
			return; // 로그인 전 채팅 무시

		ChatPacket outPacket{};
		outPacket.size = sizeof(outPacket);
		outPacket.type = static_cast<uint16>(EChatPacketType::Chat);

		const std::string& nickname = session.GetNickname();
		const size_t nicknameCopyLen = (std::min)(nickname.size(), sizeof(outPacket.nickname) - 1);
		::memcpy(outPacket.nickname, nickname.data(), nicknameCopyLen);

		::memcpy(outPacket.message, packet->message, sizeof(outPacket.message));

		if( CChatServerMain* server = session.GetServer() )
			server->Broadcast(&outPacket, outPacket.size);
	}
}

REGISTER_CHAT_PACKET_HANDLER(Chat, ChatPacket, HandleChat);