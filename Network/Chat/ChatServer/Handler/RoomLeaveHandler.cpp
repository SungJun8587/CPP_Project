
//***************************************************************************
// RoomLeaveHandler.cpp : RoomLeaveReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 방 퇴장 요청 처리 — 지금 있는 방(로비 제외)에서 로비로 돌아간다.
	// @details 이미 로비에 있는 상태에서 이 요청이 와도 안전하다 —
	//          MoveToRoom(session, kLobbyRoomId, ...)이 "로비 -> 로비" 이동이
	//          되어 사실상 아무 일도 안 하는 것과 동일한 결과를 낸다(oldRoomId
	//          == newRoomId라 인원수 알림도 한 번만 나감). success는 항상 1로
	//          보낸다 — 로비에 있어도 "요청 자체는 처리됨"이라는 의미.
	//***************************************************************************
	void HandleRoomLeaveReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		const int32 oldRoomId = session.GetRoomId();

		int32 lobbyUserCount = 0;
		server->MoveToRoom(sessionRef, kLobbyRoomId, lobbyUserCount);

		RoomLeaveResPacket res{};
		res.size = sizeof(res);
		res.type = static_cast<uint16>(EChatPacketType::RoomLeaveRes);
		res.success = 1;
		res.roomId = oldRoomId; // 방금까지 있었던 곳(로비였으면 kLobbyRoomId 그대로)
		res.roomUserCount = (oldRoomId == kLobbyRoomId) ? lobbyUserCount : server->GetRoomUserCount(oldRoomId);
		session.Send(&res, sizeof(res));
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomLeaveReq, RoomLeaveReqPacket, HandleRoomLeaveReq);