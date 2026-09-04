
//***************************************************************************
// ChatLoginHandler.cpp : LoginReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <cstring>
#include <array>

namespace
{
	//***************************************************************************
	// @brief 로그인(신규 가입 또는 토큰 기반 재접속) 요청 처리.
	// @details CChatServerMain::RequestSignup()으로 DB 비동기 워커에 위임한 뒤,
	//          그 결과(성공/중복/토큰불일치/계정없음/DB오류)를 받아서야
	//          LoginRes를 응답한다 — 그 사이 세션이 끊길 수 있으므로
	//          weak_ptr로 재확인한다.
	//***************************************************************************
	void HandleLoginReq(CChatSession& session, const PacketHeader* header)
	{
		const LoginReqPacket* packet = reinterpret_cast<const LoginReqPacket*>(header);

		if( session.IsLoggedIn() )
			return; // 중복 로그인 요청 무시 — 재로그인/강퇴 정책은 기본 뼈대 범위 밖

		// userId[32]가 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도
		// 버퍼에 복사 후 문자열화. 형식(charset/길이) 검증은 CAccountDBHandler가
		// DB 워커 스레드에서 다시 한번 하므로 여기선 "완전히 빈 문자열"만 걸러
		// DB 요청 자체를 만들 이유가 없는 케이스의 워커 부하를 아낀다.
		char safeBuf[sizeof(packet->userId) + 1] = {};
		::memcpy(safeBuf, packet->userId, sizeof(packet->userId));
		std::string nickname(safeBuf);

		if( nickname.empty() )
		{
			session.Disconnect(Iocp::CloseReason::InternalError);
			return;
		}

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		std::array<BYTE, kTokenBytes> token{};
		if( packet->hasToken != 0 )
			::memcpy(token.data(), packet->token, token.size());

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSignup(sessionRef, nickname, packet->hasToken != 0, token,
			[sessionWeak](ELoginResult result, const std::string& completedNickname,
				const std::array<BYTE, kTokenBytes>& newToken)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return; // 응답이 오기 전에 연결이 끊김 — 더 이상 할 일 없음

				LoginResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::LoginRes);
				res.success = (result == ELoginResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);

				if( result == ELoginResult::Ok )
				{
					session->MarkLoggedIn(completedNickname);
					::memcpy(res.token, newToken.data(), newToken.size());
				}

				session->Send(&res, sizeof(res));

				if( result == ELoginResult::Ok )
				{
					if( CChatServerMain* srv = session->GetServer() )
						srv->OnUserLogin(completedNickname);
				}
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(LoginReq, LoginReqPacket, HandleLoginReq);