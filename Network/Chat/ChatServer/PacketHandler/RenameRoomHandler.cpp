
//***************************************************************************
// RenameRoomHandler.cpp : RenameRoomReq 패킷 핸들러 (자체 등록)
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
	// @brief 방 이름 변경 요청 처리. 성공 시 그 방 멤버 전원에게
	//        RenameRoomNotifyPacket을 브로드캐스트하는 것은
	//        CChatServerMain::RequestRenameRoom() 내부에서 전담한다.
	//***************************************************************************
	void HandleRenameRoomReq(CChatSession& session, const PacketHeader* header)
	{
		const RenameRoomReqPacket* packet = reinterpret_cast<const RenameRoomReqPacket*>(header);
		const int32 roomId = packet->roomId;

		const std::string newName = FixedBuf::ReadStr(packet->newName);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		if( !RoomValidation::IsValidRoomName(newName) )
		{
			RenameRoomResPacket res{};
			InitPacket(res, EChatPacketType::RenameRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestRenameRoom(sessionRef, roomId, session.GetPublicId(), newName,
			[sessionWeak](ERoomResult result)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				RenameRoomResPacket res{};
				InitPacket(res, EChatPacketType::RenameRoomRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(RenameRoomReq, RenameRoomReqPacket, HandleRenameRoomReq);