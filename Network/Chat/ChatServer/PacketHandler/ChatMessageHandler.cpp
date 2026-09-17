
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
	// @brief 채팅 메시지 처리. 발신자가 지금 있는 위치(로비 또는 특정 룸)에
	//        있는 사람들에게만 브로드캐스트합니다.
	// @details [수정] 받은 패킷을 그대로 재브로드캐스트하지 않는다 — 클라이언트가
	//          보낸 nickname 필드는 신뢰하지 않고(자기 자신을 다른 사람인 척
	//          꾸며 보낼 수 있으므로), 세션이 실제로 로그인한 닉네임
	//          (session.GetNickname())으로 새로 채운 패킷을 만들어 보낸다.
	//          [수정 — 로비/룸] 예전엔 server->Broadcast()로 접속자 전원에게
	//          보냈는데, 이제 방 개념이 생기면서 "발신자와 같은 방에 있는
	//          사람"으로만 좁힌다 — session.GetRoomId()가 로비(0)든 특정
	//          룸이든 동일한 방식으로 처리된다(로비도 그냥 방 하나).
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

		const std::string& profileImageUrl = session.GetProfileImageUrl();
		const size_t urlCopyLen = (std::min)(profileImageUrl.size(), sizeof(outPacket.profileImageUrl) - 1);
		::memcpy(outPacket.profileImageUrl, profileImageUrl.data(), urlCopyLen);

		::memcpy(outPacket.message, packet->message, sizeof(outPacket.message));

		if( CChatServerMain* server = session.GetServer() )
		{
			// [추가] 나중에 DeleteChatMessageReq가 "이 메시지를 지워도 되는지"
			// 검증할 수 있도록, 브로드캐스트 직전에 고유 ID를 부여하고
			// 발신자/방 정보를 서버가 짧게 기억해둔다(영구 저장 아님 —
			// ChatServerMain.h::kMaxTrackedMessages 참고).
			outPacket.messageId = server->RegisterOutgoingMessage(session.GetPublicId(), session.GetRoomId());
			server->BroadcastToRoom(session.GetRoomId(), &outPacket, outPacket.size);
		}
	}
}

REGISTER_CHAT_PACKET_HANDLER(Chat, ChatPacket, HandleChat);