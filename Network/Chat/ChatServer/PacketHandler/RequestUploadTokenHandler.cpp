
//***************************************************************************
// RequestUploadTokenHandler.cpp : RequestUploadTokenReq 패킷 핸들러 (자체 등록)
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
	// @brief 파일 서버 업로드용 임시 토큰 발급 요청 처리.
	// @details 이미지 바이트 자체는 이 핸들러도, 채팅 서버 전체도 다루지
	//          않는다 — Redis에 토큰을 등록해서 발급하고, 클라이언트는
	//          이 토큰과 파일 서버 주소를 갖고 파일 서버에 직접(HTTP로)
	//          접속해 업로드한다. 업로드가 끝나면 클라이언트는 파일
	//          서버가 돌려준 URL을 SetProfileImageUrlReq로 다시 채팅
	//          서버에 등록한다.
	//***************************************************************************
	void HandleRequestUploadTokenReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		if( !session.IsLoggedIn() )
			return;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestUploadToken(sessionRef, publicId,
			[sessionWeak](bool success, const std::string& uploadToken, const std::string& fileServerUrl)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return; // 응답이 오기 전에 연결이 끊김

				RequestUploadTokenResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::RequestUploadTokenRes);
				res.success = success ? 1 : 0;
				res.reason = static_cast<uint8>(success ? ELoginResult::Ok : ELoginResult::DbError);

				if( success )
				{
					// [수정] uploadToken은 항상 정확히 64자(32바이트 난수의
					// 16진 인코딩)라 필드 크기(64바이트)를 꽉 채운다 — 다른
					// 가변 길이 문자열 필드처럼 NUL 종단 여유(-1)를 두면
					// 마지막 한 글자가 잘려서 Redis에 SET한 실제 키와
					// 안 맞게 된다(그래서 GET이 항상 실패했음). fileServerUrl은
					// 가변 길이라 그 필드는 여유를 그대로 둔다.
					const size_t tokenCopyLen = (std::min)(uploadToken.size(), sizeof(res.uploadToken));
					::memcpy(res.uploadToken, uploadToken.data(), tokenCopyLen);

					const size_t urlCopyLen = (std::min)(fileServerUrl.size(), sizeof(res.fileServerUrl) - 1);
					::memcpy(res.fileServerUrl, fileServerUrl.data(), urlCopyLen);
				}

				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(RequestUploadTokenReq, RequestUploadTokenReqPacket, HandleRequestUploadTokenReq);