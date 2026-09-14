
//***************************************************************************
// ChatClientRoomHandler.cpp : RoomEnterRes/RoomLeaveRes/RoomUserCountNotify
//                             패킷 핸들러 (자체 등록, 3개 한 파일에 모음)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

namespace
{
	void HandleRoomEnterRes(CChatClientSession& session, const PacketHeader* header)
	{
		const RoomEnterResPacket* packet = reinterpret_cast<const RoomEnterResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
		{
			client->OnRoomEnterResult(packet->success != 0,
				static_cast<ERoomResult>(packet->reason), packet->roomId, packet->roomUserCount);
		}
	}

	void HandleRoomLeaveRes(CChatClientSession& session, const PacketHeader* header)
	{
		const RoomLeaveResPacket* packet = reinterpret_cast<const RoomLeaveResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnRoomLeaveResult(packet->success != 0, packet->roomId, packet->roomUserCount);
	}

	//***************************************************************************
	// @brief 지금 있는 방(또는 로비)의 인원수가 바뀌었다는 서버의 자발적 알림.
	// @details 응답(Res)이 아니라 알림(Notify)이다 — 내가 방을 옮길 때뿐만
	//          아니라, 같은 방에 있는 "다른" 누군가가 들어오거나 나갈 때도
	//          도착한다. UI는 이 값으로 "방 유저수" 표시를 실시간 갱신하면 된다.
	//***************************************************************************
	void HandleRoomUserCountNotify(CChatClientSession& session, const PacketHeader* header)
	{
		const RoomUserCountNotifyPacket* packet = reinterpret_cast<const RoomUserCountNotifyPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnRoomUserCountChanged(packet->roomId, packet->userCount);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(RoomEnterRes, RoomEnterResPacket, HandleRoomEnterRes);
REGISTER_CHAT_CLIENT_PACKET_HANDLER(RoomLeaveRes, RoomLeaveResPacket, HandleRoomLeaveRes);
REGISTER_CHAT_CLIENT_PACKET_HANDLER(RoomUserCountNotify, RoomUserCountNotifyPacket, HandleRoomUserCountNotify);