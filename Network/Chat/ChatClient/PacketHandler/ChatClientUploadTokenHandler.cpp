
//***************************************************************************
// ChatClientUploadTokenHandler.cpp : RequestUploadTokenRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 업로드 토큰 발급 요청에 대한 서버 응답. 발급된 토큰/파일서버
	//        주소를 그대로 앱 콜백에 전달한다 — 실제 HTTP 업로드는 이
	//        콘솔 클라이언트가 하지 않는다(ChatClient.cpp 참고).
	//***************************************************************************
	void HandleRequestUploadTokenRes(CChatClientSession& session, const PacketHeader* header)
	{
		const RequestUploadTokenResPacket* packet = reinterpret_cast<const RequestUploadTokenResPacket*>(header);

		std::string uploadToken;
		std::string fileServerUrl;

		if( packet->success )
		{
			char safeTokenBuf[sizeof(packet->uploadToken) + 1] = {};
			::memcpy(safeTokenBuf, packet->uploadToken, sizeof(packet->uploadToken));
			uploadToken.assign(safeTokenBuf);

			char safeUrlBuf[sizeof(packet->fileServerUrl) + 1] = {};
			::memcpy(safeUrlBuf, packet->fileServerUrl, sizeof(packet->fileServerUrl));
			fileServerUrl.assign(safeUrlBuf);
		}

		if( CChatClientMain* client = session.GetClient() )
			client->OnUploadTokenResult(packet->success != 0, static_cast<ELoginResult>(packet->reason), uploadToken, fileServerUrl);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(RequestUploadTokenRes, RequestUploadTokenResPacket, HandleRequestUploadTokenRes);