
//***************************************************************************
// ChatClientLoginHandler.cpp : LoginRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"
#include <Crypto/CryptoUtil.h>

#include <cstring>
#include <array>

namespace
{
	//***************************************************************************
	// @brief 서버로부터 로그인 응답을 받았을 때.
	//***************************************************************************
	void HandleLoginRes(CChatClientSession& session, const PacketHeader* header)
	{
		const LoginResPacket* packet = reinterpret_cast<const LoginResPacket*>(header);

		// nickname[kNicknameBytes]이 NUL로 안 끝났을 가능성 방어(경계값)
		char safeNicknameBuf[kNicknameBytes + 1] = {};
		::memcpy(safeNicknameBuf, packet->nickname, sizeof(packet->nickname));

		std::array<BYTE, kPublicIdBytes> publicId{};
		std::array<BYTE, kTokenBytes> token{};
		if( packet->success != 0 )
		{
			::memcpy(publicId.data(), packet->publicId, publicId.size());
			::memcpy(token.data(), packet->token, token.size());
		}

		if( CChatClientMain* client = session.GetClient() )
			client->OnLoginResult(packet->success != 0, static_cast<ELoginResult>(packet->reason), safeNicknameBuf, publicId, token);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(LoginRes, LoginResPacket, HandleLoginRes);