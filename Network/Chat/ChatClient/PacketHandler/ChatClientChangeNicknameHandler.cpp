
//***************************************************************************
// ChatClientChangeNicknameHandler.cpp : ChangeNicknameRes 패킷 핸들러 (자체 등록)
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
	// @brief 서버로부터 닉네임 변경 응답을 받았을 때.
	//***************************************************************************
	void HandleChangeNicknameRes(CChatClientSession& session, const PacketHeader* header)
	{
		const ChangeNicknameResPacket* packet = reinterpret_cast<const ChangeNicknameResPacket*>(header);

		// ChangeNicknameResPacket 자체엔 success/reason만 있고 새 닉네임
		// 문자열은 안 실려 있다 — 요청 시점에 세션이 기억해둔 값
		// (GetPendingNewNickname())을 꺼내 쓴다. 요청 하나가 진행 중일
		// 때만 정확하다는 전제(동시에 여러 변경 요청을 보내는 시나리오는
		// 지원하지 않음).
		if( CChatClientMain* client = session.GetClient() )
		{
			client->OnNicknameChangeResult(packet->success != 0,
				static_cast<ELoginResult>(packet->reason), session.GetPendingNewNickname());
		}
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(ChangeNicknameRes, ChangeNicknameResPacket, HandleChangeNicknameRes);