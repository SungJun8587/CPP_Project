
//***************************************************************************
// ChatAccountHandler.cpp : 계정 패킷 핸들러 — 로그인/닉네임 변경
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

		if( !session.TryBeginLogin() )
			return; // 이미 로그인했거나 로그인 처리 중 — 중복 요청 무시

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
			publicId = ToPublicId(packet->publicId);
			token = FixedBuf::ToArray<kTokenBytes>(packet->token);
		}

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSignup(sessionRef, nickname, hasToken, publicId, token,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, const std::string& completedNickname,
				const std::array<BYTE, kPublicIdBytes>& completedPublicId,
				const std::array<BYTE, kTokenBytes>& newToken,
				const std::string& profileImageUrl)
				{
					// 로그인 처리 중에 연결이 이미 끊긴 세션은 등록하지 않는다(끊김 정리는 이미 끝났다).
					if( !session->IsConnected() )
						return;

					if( result != ELoginResult::Ok && session->OnLoginFailed() )
					{
						session->Disconnect(Iocp::CloseReason::InternalError);
						return;
					}

					LoginResPacket res{};
					InitPacket(res, EChatPacketType::LoginRes);
					res.success = (result == ELoginResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);

					// [수정] 성공/실패 상관없이 completedNickname을 그대로 실어 보낸다.
					// AccountDBHandler.cpp가 이미 상황에 맞게 채워서(성공/신규가입
					// 실패면 해당 닉네임, AccountNotFound처럼 애초에 못 찾은 경우면
					// 빈 문자열) 넘겨주므로 여기서 추가로 분기할 필요가 없다.
					CopyStrUtf8(res.nickname, completedNickname);

					if( result == ELoginResult::Ok )
					{
						session->MarkLoggedIn(completedPublicId, completedNickname, profileImageUrl);
						CopyPublicId(res.publicId, completedPublicId);
						CopyBytes(res.token, newToken);

						CopyStrUtf8(res.profileImageUrl, profileImageUrl);
					}

					SendPacket(session, res);

					if( result == ELoginResult::Ok )
					{
						if( CChatServerMain* srv = session->GetServer() )
						{
							// 친구 기능 — 친구 알림용 세션 레지스트리에 등록하고 접속 중인 친구들에게
							// 접속을 알리며, 이 계정이 차단한 사용자 목록을 세션에 올린다(채팅 수신 필터용).
							// 차단 목록 로드를 최대한 일찍 시작하도록 로비 배정보다 먼저 호출한다.
							// 같은 계정의 이전 세션 종료(OnFriendLogout)와 겹쳐도 로그인 기록이 지워지지 않도록
							// 레지스트리 등록을 Redis 로그인 기록(OnUserLogin)보다 먼저 한다.
							// 접속 알림은 로그인 키 기록이 끝난 뒤에 나가도록 첫 세션 여부를 OnUserLogin에 넘긴다.
							const bool firstSession = srv->OnFriendLogin(session);
							srv->OnUserLogin(completedPublicId, firstSession);

							// [추가] 로그인에 성공하면 곧바로 로비에 배정한다 —
							// "로그인은 됐는데 아직 어디에도 속하지 않은" 어중간한
							// 상태를 만들지 않기 위함. 로비도 CChatServerMain
							// 관점에서는 그냥 하나의 방(roomId=kLobbyRoomId)이라
							// MoveToRoom()을 그대로 재사용한다.
							int32 lobbyUserCount = 0;
							srv->MoveToRoom(session, kLobbyRoomId, lobbyUserCount);

							// 위 등록 도중 연결이 끊겼다면 OnDisconnected()가 로그인 상태를 못 보고 지나갔을 수 있다 — 직접 정리한다.
							if( !session->IsConnected() )
							{
								srv->LeaveCurrentRoom(session.get());
								if( !srv->OnFriendLogout(session.get()) )
									srv->OnUserLogout(completedPublicId);
							}
						}
					}
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(LoginReq, LoginReqPacket, HandleLoginReq);

namespace
{
	//***************************************************************************
	// @brief 로그인된 세션의 닉네임 변경 요청 처리.
	// @details [설계 변경] CChatServerMain::RequestChangeNickname()으로 DB
	//          비동기 워커에 위임한 뒤, 성공 시 세션의 표시용 닉네임만
	//          갱신한다(UpdateNickname()). publicId(GetPublicId())는 절대
	//          바뀌지 않으므로 Redis 키 갱신 자체가 필요 없다 — 예전엔
	//          닉네임이 곧 식별자라 Redis RENAME이 필요했지만, 이제는
	//          Redis 키가 애초에 publicId 기준이라 이 요청과 무관하다.
	//          HandleLoginReq()와 동일하게 응답이 오기 전 세션이 끊길 수
	//          있으므로 weak_ptr로 재확인한다.
	//***************************************************************************
	void HandleChangeNicknameReq(CChatSession& session, const PacketHeader* header)
	{
		const ChangeNicknameReqPacket* packet = reinterpret_cast<const ChangeNicknameReqPacket*>(header);

		// newNickname이 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도
		// 버퍼에 복사 후 문자열화. 형식 검증은 AccountDBHandler.cpp가
		// DB 워커 스레드에서 다시 한번 하므로 여기선 "완전히 빈 문자열"만
		// 걸러 DB 요청 자체를 만들 이유가 없는 케이스의 워커 부하를 아낀다.
		char safeBuf[sizeof(packet->newNickname) + 1] = {};
		::memcpy(safeBuf, packet->newNickname, sizeof(packet->newNickname));
		std::string newNickname(safeBuf);

		if( newNickname.empty() )
			return; // 로그인 실패와 달리 연결을 끊을 정도의 위반은 아님 — 그냥 무시

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return; // 로그인 전 요청은 조용히 무시 — HandleLoginReq()의 중복 로그인 무시와 대칭되는 정책

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestChangeNickname(sessionRef, publicId, newNickname,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, const std::array<BYTE, kPublicIdBytes>& /*publicId*/, const std::string& newNick)
				{
					ChangeNicknameResPacket res{};
					InitPacket(res, EChatPacketType::ChangeNicknameRes);
					res.success = (result == ELoginResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);

					if( result == ELoginResult::Ok )
					{
						session->UpdateNickname(newNick);

						// 접속 중인 친구들에게 바뀐 닉네임을 알린다(세션 값 갱신 직후).
						if( CChatServerMain* srv = session->GetServer() )
							srv->NotifyFriendProfileChanged(session);
					}

					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(ChangeNicknameReq, ChangeNicknameReqPacket, HandleChangeNicknameReq);