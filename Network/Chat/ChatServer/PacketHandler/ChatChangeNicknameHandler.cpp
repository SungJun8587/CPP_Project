
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

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestChangeNickname(sessionRef, publicId, newNickname,
			[sessionWeak](ELoginResult result, const std::array<BYTE, kPublicIdBytes>& /*publicId*/, const std::string& newNick)
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
					session->UpdateNickname(newNick);

				session->Send(&res, sizeof(res));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ChangeNicknameReq, ChangeNicknameReqPacket, HandleChangeNicknameReq);