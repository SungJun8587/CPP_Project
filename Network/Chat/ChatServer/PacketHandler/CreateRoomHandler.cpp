
//***************************************************************************
// CreateRoomHandler.cpp : CreateRoomReq 패킷 핸들러 (자체 등록)
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
	// @brief 방 생성 요청 처리. 로그인 상태여야 한다. 이름 형식이 잘못됐으면
	//        DB까지 안 가고 즉시 InvalidName으로 응답한다.
	//***************************************************************************
	void HandleCreateRoomReq(CChatSession& session, const PacketHeader* header)
	{
		const CreateRoomReqPacket* packet = reinterpret_cast<const CreateRoomReqPacket*>(header);

		// 고정폭 char 배열이 NUL로 안 끝났을 가능성(비정상 패킷)에 대비해
		// 최대 길이 안에서만 읽어 std::string으로 안전하게 변환한다.
		const std::string roomName = FixedBuf::ReadStr(packet->roomName);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		if( !RoomValidation::IsValidRoomName(roomName) )
		{
			CreateRoomResPacket res{};
			InitPacket(res, EChatPacketType::CreateRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			res.roomId = 0;
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestCreateRoom(sessionRef, session.GetPublicId(), roomName,
			[sessionWeak](ERoomResult result, int32 newRoomId)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				CreateRoomResPacket res{};
				InitPacket(res, EChatPacketType::CreateRoomRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				res.roomId = newRoomId;
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(CreateRoomReq, CreateRoomReqPacket, HandleCreateRoomReq);