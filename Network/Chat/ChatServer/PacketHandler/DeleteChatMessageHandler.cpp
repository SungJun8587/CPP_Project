
//***************************************************************************
// DeleteChatMessageHandler.cpp : DeleteChatMessageReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

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
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시 — 다른 핸들러들과 동일한 정책

		const DeleteChatMessageReqPacket* packet = reinterpret_cast<const DeleteChatMessageReqPacket*>(header);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		int32 roomId = 0;
		const EDeleteMessageResult result = server->TryDeleteMessage(packet->messageId, session.GetPublicId(), roomId);

		DeleteChatMessageResPacket res{};
		res.size = sizeof(res);
		res.type = static_cast<uint16>(EChatPacketType::DeleteChatMessageRes);
		res.success = (result == EDeleteMessageResult::Ok) ? 1 : 0;
		res.reason = static_cast<uint8>(result);
		session.Send(&res, sizeof(res));

		if( result != EDeleteMessageResult::Ok )
			return;

		// [추가] 대화 기록(Redis)에서도 이 메시지를 제거한다 — 로비면
		// RemoveRoomChatMessage() 내부에서 즉시 반환하므로 안전하게 항상 호출.
		server->RemoveRoomChatMessage(roomId, packet->messageId);

		DeleteChatMessageNotifyPacket notify{};
		notify.size = sizeof(notify);
		notify.type = static_cast<uint16>(EChatPacketType::DeleteChatMessageNotify);
		notify.messageId = packet->messageId;
		server->BroadcastToRoom(roomId, &notify, notify.size);
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteChatMessageReq, DeleteChatMessageReqPacket, HandleDeleteChatMessageReq);