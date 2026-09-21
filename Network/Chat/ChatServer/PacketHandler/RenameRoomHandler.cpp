
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
	// @brief CreateRoomHandler.cpp의 IsValidRoomName()과 동일한 규칙 —
	//        빈 문자열/필드 크기 초과만 걸러낸다.
	//***************************************************************************
	bool IsValidRoomName(const std::string& name)
	{
		return !name.empty() && name.size() < sizeof(RenameRoomReqPacket::newName);
	}

	//***************************************************************************
	// @brief 방 이름 변경 요청 처리. 성공 시 그 방 멤버 전원에게
	//        RenameRoomNotifyPacket을 브로드캐스트하는 것은
	//        CChatServerMain::RequestRenameRoom() 내부에서 전담한다.
	//***************************************************************************
	void HandleRenameRoomReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const RenameRoomReqPacket* packet = reinterpret_cast<const RenameRoomReqPacket*>(header);
		const int32 roomId = packet->roomId;

		const size_t nameLen = ::strnlen(packet->newName, sizeof(packet->newName));
		const std::string newName(packet->newName, nameLen);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		if( !IsValidRoomName(newName) )
		{
			RenameRoomResPacket res{};
			res.size = sizeof(res);
			res.type = static_cast<uint16>(EChatPacketType::RenameRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestRenameRoom(sessionRef, roomId, session.GetPublicId(), newName,
			[sessionWeak](ERoomResult result)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				RenameRoomResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::RenameRoomRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(RenameRoomReq, RenameRoomReqPacket, HandleRenameRoomReq);