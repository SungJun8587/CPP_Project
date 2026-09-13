
//***************************************************************************
// SetProfileImageUrlHandler.cpp : SetProfileImageUrlReq 패킷 핸들러 (자체 등록)
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
	// @brief 로그인된 세션의 프로필 이미지 URL 변경 요청 처리.
	// @details ChatChangeNicknameHandler.cpp와 동일한 구조 — RequestSetProfileImageUrl()로
	//          DB 비동기 워커에 위임한 뒤, 성공 시 세션의 값만 갱신한다.
	//***************************************************************************
	void HandleSetProfileImageUrlReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시

		const SetProfileImageUrlReqPacket* packet = reinterpret_cast<const SetProfileImageUrlReqPacket*>(header);

		// url이 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도 버퍼에
		// 복사 후 문자열화. 빈 문자열이면 "프로필 이미지 해제" 의미이므로
		// (닉네임과 달리) 빈 값이어도 그냥 요청을 진행한다.
		char safeBuf[sizeof(packet->url) + 1] = {};
		::memcpy(safeBuf, packet->url, sizeof(packet->url));
		std::string newUrl(safeBuf);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSetProfileImageUrl(sessionRef, publicId, newUrl,
			[sessionWeak](ELoginResult result, const std::array<BYTE, kPublicIdBytes>& /*publicId*/,
				const std::string& newUrl, int64 newImageId)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return; // 응답이 오기 전에 연결이 끊김 — 더 이상 할 일 없음

				SetProfileImageUrlResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::SetProfileImageUrlRes);
				res.success = (result == ELoginResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);
				res.imageId = newImageId;

				if( result == ELoginResult::Ok )
					session->UpdateProfileImageUrl(newUrl);

				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(SetProfileImageUrlReq, SetProfileImageUrlReqPacket, HandleSetProfileImageUrlReq);