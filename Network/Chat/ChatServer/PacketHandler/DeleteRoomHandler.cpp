
//***************************************************************************
// DeleteRoomHandler.cpp : DeleteRoomReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 방 삭제 요청 처리. 성공 시 그 방에 있던 멤버 전원에게
	//        DeleteRoomNotifyPacket을 브로드캐스트하고 로비로 이동시키는
	//        작업은 CChatServerMain::RequestDeleteRoom() 내부에서 전담한다
	//        — 여기서는 요청자에게 보낼 직접 응답만 처리한다.
	//***************************************************************************
	void HandleDeleteRoomReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const DeleteRoomReqPacket* packet = reinterpret_cast<const DeleteRoomReqPacket*>(header);
		const int32 roomId = packet->roomId;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestDeleteRoom(sessionRef, roomId, session.GetPublicId(),
			[sessionWeak](ERoomResult result)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				DeleteRoomResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::DeleteRoomRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteRoomReq, DeleteRoomReqPacket, HandleDeleteRoomReq);