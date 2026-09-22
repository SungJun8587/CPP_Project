
//***************************************************************************
// SetRoomImageHandler.cpp : SetRoomImageReq 패킷 핸들러 (자체 등록)
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
	// @brief 방 프로필 이미지 설정/교체/해제 요청 처리. 성공 시 그 방
	//        멤버 전원에게 RoomImageChangedNotifyPacket을 브로드캐스트하고,
	//        이전 이미지가 우리 파일 서버 소유였으면 실제 파일 삭제를
	//        예약하는 것은 CChatServerMain::RequestSetRoomImage() 내부에서
	//        전담한다 — 여기서는 요청자에게 보낼 직접 응답만 처리한다.
	//***************************************************************************
	void HandleSetRoomImageReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const SetRoomImageReqPacket* packet = reinterpret_cast<const SetRoomImageReqPacket*>(header);
		const int32 roomId = packet->roomId;

		// 고정폭 char 배열이 NUL로 안 끝났을 가능성(비정상 패킷)에 대비해
		// 최대 길이 안에서만 읽어 std::string으로 안전하게 변환한다.
		const size_t urlLen = ::strnlen(packet->imageUrl, sizeof(packet->imageUrl));
		const std::string imageUrl(packet->imageUrl, urlLen);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSetRoomImage(sessionRef, roomId, session.GetPublicId(), imageUrl,
			[sessionWeak](ERoomResult result)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				SetRoomImageResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::SetRoomImageRes);
				res.success = (result == ERoomResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(SetRoomImageReq, SetRoomImageReqPacket, HandleSetRoomImageReq);