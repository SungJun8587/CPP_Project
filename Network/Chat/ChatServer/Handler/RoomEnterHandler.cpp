
//***************************************************************************
// RoomEnterHandler.cpp : RoomEnterReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 방 입장 요청 처리. 로그인 상태여야 하고, roomId가 1~kMaxRoomId
	//        범위여야 한다(로비로는 이 요청으로 못 들어간다 — 그건 RoomLeaveReq).
	//***************************************************************************
	void HandleRoomEnterReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시 — 다른 핸들러들과 동일한 정책

		const RoomEnterReqPacket* packet = reinterpret_cast<const RoomEnterReqPacket*>(header);
		const int32 roomId = packet->roomId;

		RoomEnterResPacket res{};
		res.size = sizeof(res);
		res.type = static_cast<uint16>(EChatPacketType::RoomEnterRes);
		res.roomId = roomId;

		if( roomId < 1 || roomId > kMaxRoomId )
		{
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidRoomId);
			res.roomUserCount = 0;
			session.Send(&res, sizeof(res));
			return;
		}

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		int32 newUserCount = 0;
		server->MoveToRoom(sessionRef, roomId, newUserCount);

		res.success = 1;
		res.reason = static_cast<uint8>(ERoomResult::Ok);
		res.roomUserCount = newUserCount;
		session.Send(&res, sizeof(res));
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomEnterReq, RoomEnterReqPacket, HandleRoomEnterReq);