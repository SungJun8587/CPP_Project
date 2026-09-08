
//***************************************************************************
// ChatChangeNicknameHandler.cpp : ChangeNicknameReq 패킷 핸들러 (자체 등록)
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
	// @brief 로그인된 세션의 닉네임 변경 요청 처리.
	// @details CChatServerMain::RequestChangeNickname()으로 DB 비동기 워커에
	//          위임한 뒤, 성공 시에만 세션 아이덴티티(UpdateNickname())와
	//          Redis 온라인 상태 키(OnUserNicknameChanged())를 갱신한다 —
	//          HandleLoginReq()와 동일하게 그 사이 세션이 끊길 수 있으므로
	//          weak_ptr로 재확인한다.
	//***************************************************************************
	void HandleChangeNicknameReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시 — HandleLoginReq()의 중복 로그인 무시와 대칭되는 정책

		const ChangeNicknameReqPacket* packet = reinterpret_cast<const ChangeNicknameReqPacket*>(header);

		// newNickname이 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도
		// 버퍼에 복사 후 문자열화. 형식 검증은 ChangeNicknameDBHandler.cpp가
		// DB 워커 스레드에서 다시 한번 하므로 여기선 "완전히 빈 문자열"만
		// 걸러 DB 요청 자체를 만들 이유가 없는 케이스의 워커 부하를 아낀다.
		char safeBuf[sizeof(packet->newNickname) + 1] = {};
		::memcpy(safeBuf, packet->newNickname, sizeof(packet->newNickname));
		std::string newNickname(safeBuf);

		if( newNickname.empty() )
			return; // 로그인 실패와 달리 연결을 끊을 정도의 위반은 아님 — 그냥 무시

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::string oldNickname = session.GetUserId();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestChangeNickname(sessionRef, oldNickname, newNickname,
			[sessionWeak](ELoginResult result, const std::string& oldNick, const std::string& newNick)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return; // 응답이 오기 전에 연결이 끊김 — 더 이상 할 일 없음

				ChangeNicknameResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::ChangeNicknameRes);
				res.success = (result == ELoginResult::Ok) ? 1 : 0;
				res.reason = static_cast<uint8>(result);

				if( result == ELoginResult::Ok )
				{
					// [순서 중요] 세션 아이덴티티를 먼저 갱신한 뒤 Redis를
					// 갱신한다 — 반대로 하면 그 찰나에 GetUserId()를 참조하는
					// 다른 코드(예: 동시에 들어온 다른 패킷 핸들러)가 옛
					// 닉네임과 새 Redis 키 사이의 불일치를 볼 수 있다.
					session->UpdateNickname(newNick);

					if( CChatServerMain* srv = session->GetServer() )
						srv->OnUserNicknameChanged(oldNick, newNick);
				}

				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ChangeNicknameReq, ChangeNicknameReqPacket, HandleChangeNicknameReq);