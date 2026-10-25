
//***************************************************************************
// ChatMessageHandler.cpp : 채팅 메시지 패킷 핸들러 — 메시지 전송/삭제
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
	//          [추가 — 차단] 발신자를 차단한 수신자에게는 전달하지 않는다
	//          (CChatServerMain::BroadcastChatToRoom() 참고).
	//***************************************************************************
	void HandleChat(CChatSession& session, const PacketHeader* header)
	{
		const ChatPacket* packet = reinterpret_cast<const ChatPacket*>(header);

		if( !session.IsLoggedIn() )
			return; // 로그인 전 채팅 무시

		if( !session.AllowMessage() )
			return; // 초당 전송 한도 초과 — 조용히 버린다

		ChatPacket outPacket{};
		InitPacket(outPacket, EChatPacketType::Chat);

		const std::string nickname = session.GetNickname();
		CopyStrUtf8(outPacket.nickname, nickname);

		const std::string profileImageUrl = session.GetProfileImageUrl();
		CopyStrUtf8(outPacket.profileImageUrl, profileImageUrl);

		::memcpy(outPacket.message, packet->message, sizeof(outPacket.message));
		outPacket.message[sizeof(outPacket.message) - 1] = '\0';	// 클라이언트가 종료 문자를 보내지 않아도 수신자가 문자열 끝을 넘어 읽지 않게 한다

		if( CChatServerMain* server = session.GetServer() )
		{
			// [추가] 나중에 DeleteChatMessageReq가 "이 메시지를 지워도 되는지"
			// 검증할 수 있도록, 브로드캐스트 직전에 고유 ID를 부여하고
			// 발신자/방 정보를 서버가 짧게 기억해둔다(영구 저장 아님 —
			// ChatServerMain.h::kMaxTrackedMessages 참고).
			outPacket.messageId = server->RegisterOutgoingMessage(session.GetPublicId(), session.GetRoomId());
			server->BroadcastChatToRoom(session.GetRoomId(), session.GetPublicId(), &outPacket, outPacket.size);

			// [추가] 방(로비 제외) 대화 기록을 Redis에 남긴다 — 나중에
			// ChatRoomHandler.cpp가 입장 시 자동으로 이 기록을 스트리밍해준다.
			// 로비는 RecordRoomChatMessage() 내부에서 kLobbyRoomId를 보고
			// 즉시 반환하므로 여기서 따로 분기할 필요는 없지만, 문자열 변환
			// (strnlen)을 로비 메시지에서까지 매번 할 필요는 없어서 먼저 확인한다.
			if( session.GetRoomId() != kLobbyRoomId )
			{
				const size_t messageLen = ::strnlen(outPacket.message, sizeof(outPacket.message));
				const std::string messageText(outPacket.message, messageLen);
				server->RecordRoomChatMessage(session.GetRoomId(), session.GetPublicId(), nickname, profileImageUrl, messageText, outPacket.messageId);
			}
		}
	}
}

REGISTER_CHAT_PACKET_HANDLER(Chat, ChatPacket, HandleChat);

namespace
{
	//***************************************************************************
	// @brief 채팅 메시지 삭제 요청 처리.
	// @details 요청자에게는 항상 DeleteChatMessageResPacket(성공/실패)을
	//          보낸다. 성공했을 때만 그 메시지가 원래 브로드캐스트됐던
	//          방(로비 포함) 전체에 DeleteChatMessageNotifyPacket을 추가로
	//          브로드캐스트한다 — 요청자 자신도 이 알림을 받아서, 클라이언트
	//          쪽 "내가 지운 메시지 제거" 처리를 요청자/타인 구분 없이 이
	//          알림 한 경로로 통일할 수 있다.
	//***************************************************************************
	void HandleDeleteChatMessageReq(CChatSession& session, const PacketHeader* header)
	{
		const DeleteChatMessageReqPacket* packet = reinterpret_cast<const DeleteChatMessageReqPacket*>(header);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return; // 로그인 전 요청은 조용히 무시 — 다른 핸들러들과 동일한 정책

		int32 roomId = 0;
		const EDeleteMessageResult result = server->TryDeleteMessage(packet->messageId, session.GetPublicId(), roomId);

		DeleteChatMessageResPacket res{};
		InitPacket(res, EChatPacketType::DeleteChatMessageRes);
		res.success = (result == EDeleteMessageResult::Ok) ? 1 : 0;
		res.reason = static_cast<uint8>(result);
		SendPacket(session, res);

		if( result != EDeleteMessageResult::Ok )
			return;

		// [추가] 대화 기록(Redis)에서도 이 메시지를 제거한다 — 로비면
		// RemoveRoomChatMessage() 내부에서 즉시 반환하므로 안전하게 항상 호출.
		server->RemoveRoomChatMessage(roomId, packet->messageId);

		DeleteChatMessageNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::DeleteChatMessageNotify);
		notify.messageId = packet->messageId;
		server->BroadcastToRoom(roomId, &notify, notify.size);
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteChatMessageReq, DeleteChatMessageReqPacket, HandleDeleteChatMessageReq);