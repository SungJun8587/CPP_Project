
//***************************************************************************
// SelectProfileImageHandler.cpp : SelectProfileImageReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 갤러리에 이미 있는 이미지 중 하나를 대표로 지정. 성공하면
	//        세션의 표시용 프로필 이미지 값도 곧바로 갱신한다.
	//***************************************************************************
	void HandleSelectProfileImageReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const SelectProfileImageReqPacket* packet = reinterpret_cast<const SelectProfileImageReqPacket*>(header);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();
		const int64 imageId = packet->imageId;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSelectProfileImage(sessionRef, publicId, imageId,
			[sessionWeak](ELoginResult result, int64 /*imageId*/, const std::string& selectedImageRef)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				if( result == ELoginResult::Ok )
				{
					// DB의 image_ref는 상대 경로로 저장돼 있을 수 있으므로, 세션에는 로그인/URL 설정
					// 경로와 같은 "표시용 URL"을 넣는다(이미 완전한 URL이면 그대로 반환됨).
					CChatServerMain* srv = session->GetServer();
					session->UpdateProfileImageUrl(srv != nullptr ? srv->ToDisplayImageUrl(selectedImageRef) : selectedImageRef);

					// 접속 중인 친구들에게 바뀐 대표 이미지를 알린다(세션 값 갱신 직후).
					if( srv != nullptr )
						srv->NotifyFriendProfileChanged(session);
				}

				SelectProfileImageResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::SelectProfileImageRes);
				res.success = (result == ELoginResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(SelectProfileImageReq, SelectProfileImageReqPacket, HandleSelectProfileImageReq);