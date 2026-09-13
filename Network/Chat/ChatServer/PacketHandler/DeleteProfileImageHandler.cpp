
//***************************************************************************
// DeleteProfileImageHandler.cpp : DeleteProfileImageReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 갤러리에서 이미지 하나를 삭제. 지운 이미지가 대표였다면 세션의
	//        표시용 프로필 이미지 값을 비운다(다른 이미지로 자동 승격하지
	//        않음 — 사용자가 남은 이미지 중 새로 골라야 한다).
	//***************************************************************************
	void HandleDeleteProfileImageReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const DeleteProfileImageReqPacket* packet = reinterpret_cast<const DeleteProfileImageReqPacket*>(header);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();
		const int64 imageId = packet->imageId;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestDeleteProfileImage(sessionRef, publicId, imageId,
			[sessionWeak](ELoginResult result, int64 /*imageId*/, bool wasActive)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				if( result == ELoginResult::Ok && wasActive )
					session->UpdateProfileImageUrl(std::string());

				DeleteProfileImageResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::DeleteProfileImageRes);
				res.success = (result == ELoginResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteProfileImageReq, DeleteProfileImageReqPacket, HandleDeleteProfileImageReq);