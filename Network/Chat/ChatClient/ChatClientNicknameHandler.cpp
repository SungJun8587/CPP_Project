
//***************************************************************************
// ChatClientNicknameHandler.cpp : NicknameGenerateRes 패킷 핸들러 (자체 등록)
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
	// @brief 서버가 생성해 보낸 닉네임을 받았을 때.
	//***************************************************************************
	void HandleNicknameGenerateRes(CChatClientSession& session, const PacketHeader* header)
	{
		const NicknameGenerateResPacket* packet = reinterpret_cast<const NicknameGenerateResPacket*>(header);

		// nickname[32]이 NUL로 안 끝났을 가능성 방어
		char safeBuf[sizeof(packet->nickname) + 1] = {};
		::memcpy(safeBuf, packet->nickname, sizeof(packet->nickname));

		if( CChatClientMain* client = session.GetClient() )
			client->OnNicknameGenerated(safeBuf);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(NicknameGenerateRes, NicknameGenerateResPacket, HandleNicknameGenerateRes);
