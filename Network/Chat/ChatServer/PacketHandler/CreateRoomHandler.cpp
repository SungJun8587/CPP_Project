
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
	// @brief 방 이름 형식 검증 — 빈 문자열이거나 필드 크기를 넘는 값만
	//        걸러낸다. 닉네임처럼 문자 종류를 세밀하게 제한하지는 않는다
	//        — 방 이름은 자유도가 더 커도 무방하다고 판단했다.
	//***************************************************************************
	bool IsValidRoomName(const std::string& name)
	{
		return !name.empty() && name.size() < sizeof(CreateRoomReqPacket::roomName);
	}

	//***************************************************************************
	// @brief 방 생성 요청 처리. 로그인 상태여야 한다. 이름 형식이 잘못됐으면
	//        DB까지 안 가고 즉시 InvalidName으로 응답한다.
	//***************************************************************************
	void HandleCreateRoomReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const CreateRoomReqPacket* packet = reinterpret_cast<const CreateRoomReqPacket*>(header);

		// 고정폭 char 배열이 NUL로 안 끝났을 가능성(비정상 패킷)에 대비해
		// 최대 길이 안에서만 읽어 std::string으로 안전하게 변환한다.
		const size_t nameLen = ::strnlen(packet->roomName, sizeof(packet->roomName));
		const std::string roomName(packet->roomName, nameLen);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		if( !IsValidRoomName(roomName) )
		{
			CreateRoomResPacket res{};
			res.size = sizeof(res);
			res.type = static_cast<uint16>(EChatPacketType::CreateRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			res.roomId = 0;
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestCreateRoom(sessionRef, session.GetPublicId(), roomName,
			[sessionWeak](ERoomResult result, int32 newRoomId)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				CreateRoomResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::CreateRoomRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				res.roomId = newRoomId;
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(CreateRoomReq, CreateRoomReqPacket, HandleCreateRoomReq);