
//***************************************************************************
// ChatLoginHandler.cpp : LoginReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"
#include <Crypto/CryptoUtil.h>

#include <cstring>
#include <array>

namespace
{
	//***************************************************************************
	// @brief 로그인(신규 가입 또는 publicId+token 기반 재접속) 요청 처리.
	// @details [설계 변경] hasToken==0(신규 가입)이면 userId(원하는 닉네임)만
	//          의미 있고, hasToken==1(재접속)이면 publicId+token만 의미
	//          있다(userId는 무시 — 재접속 시점의 진짜 닉네임은 DB 값이
	//          기준). CChatServerMain::RequestSignup()으로 DB 비동기
	//          워커에 위임한 뒤, 그 결과(성공/중복/토큰불일치/계정없음/
	//          DB오류)를 받아서야 LoginRes를 응답한다 — 그 사이 세션이
	//          끊길 수 있으므로 weak_ptr로 재확인한다.
	//***************************************************************************
	void HandleLoginReq(CChatSession& session, const PacketHeader* header)
	{
		const LoginReqPacket* packet = reinterpret_cast<const LoginReqPacket*>(header);

		if( session.IsLoggedIn() )
			return; // 중복 로그인 요청 무시 — 재로그인/강퇴 정책은 기본 뼈대 범위 밖

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const bool hasToken = (packet->hasToken != 0);

		std::string nickname;
		std::array<BYTE, kPublicIdBytes> publicId{};
		std::array<BYTE, kTokenBytes> token{};

		if( !hasToken )
		{
			// ── 신규 가입 — userId(원하는 닉네임)만 의미 있음 ──────────
			// userId(kNicknameBytes바이트, UTF-8)가 NUL로 안 끝났을 가능성을
			// 방어(경계값)하기 위해 별도 버퍼에 복사 후 문자열화. 형식(charset/
			// 길이) 검증은 AccountDBHandler.cpp가 DB 워커 스레드에서 다시 한번
			// 하므로 여기선 "완전히 빈 문자열"만 걸러 DB 요청 자체를 만들
			// 이유가 없는 케이스의 워커 부하를 아낀다.
			char safeBuf[sizeof(packet->userId) + 1] = {};
			::memcpy(safeBuf, packet->userId, sizeof(packet->userId));
			nickname.assign(safeBuf);

			if( nickname.empty() )
			{
				session.Disconnect(Iocp::CloseReason::InternalError);
				return;
			}
		}
		else
		{
			// ── 재접속 — publicId+token만 의미 있음 ────────────────────
			::memcpy(publicId.data(), packet->publicId, publicId.size());
			::memcpy(token.data(), packet->token, token.size());
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSignup(sessionRef, nickname, hasToken, publicId, token,
			[sessionWeak](ELoginResult result, const std::string& completedNickname,
				const std::array<BYTE, kPublicIdBytes>& completedPublicId,
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

				// [수정] 성공/실패 상관없이 completedNickname을 그대로 실어 보낸다.
				// AccountDBHandler.cpp가 이미 상황에 맞게 채워서(성공/신규가입
				// 실패면 해당 닉네임, AccountNotFound처럼 애초에 못 찾은 경우면
				// 빈 문자열) 넘겨주므로 여기서 추가로 분기할 필요가 없다.
				const size_t nicknameCopyLen = (std::min)(completedNickname.size(), sizeof(res.nickname) - 1);
				::memcpy(res.nickname, completedNickname.data(), nicknameCopyLen);

				if( result == ELoginResult::Ok )
				{
					session->MarkLoggedIn(completedPublicId, completedNickname);
					::memcpy(res.publicId, completedPublicId.data(), completedPublicId.size());
					::memcpy(res.token, newToken.data(), newToken.size());
				}

				session->Send(&res, sizeof(res));

				if( result == ELoginResult::Ok )
				{
					if( CChatServerMain* srv = session->GetServer() )
					{
						srv->OnUserLogin(completedPublicId);

						// [추가] 로그인에 성공하면 곧바로 로비에 배정한다 —
						// "로그인은 됐는데 아직 어디에도 속하지 않은" 어중간한
						// 상태를 만들지 않기 위함. 로비도 CChatServerMain
						// 관점에서는 그냥 하나의 방(roomId=kLobbyRoomId)이라
						// MoveToRoom()을 그대로 재사용한다.
						int32 lobbyUserCount = 0;
						srv->MoveToRoom(session, kLobbyRoomId, lobbyUserCount);
					}
				}
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(LoginReq, LoginReqPacket, HandleLoginReq);