
//***************************************************************************
// ChatSession.cpp: implementation of the CChatSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <cstring>

//***************************************************************************
// @brief CChatSession 생성자
// @param server 이 세션이 속한 채팅 서버 (세션 생성 팩토리가 항상 주입)
//***************************************************************************
CChatSession::CChatSession(CChatServerMain* server)
	: _server(server)
{
}

//***************************************************************************
// @brief 연결 완료 시 호출 (기본 뼈대 — 필요 시 접속 로그 등 추가)
//***************************************************************************
void CChatSession::OnConnected()
{
}

//***************************************************************************
// @brief 연결 종료 시 호출. 로그인 상태였다면 Redis에서 로그인 기록을 제거합니다.
//***************************************************************************
void CChatSession::OnDisconnected()
{
	if( _loggedIn )
	{
		_server->OnUserLogout(_userId);
		_loggedIn = false;
	}
}

//***************************************************************************
// @brief 수신 데이터 처리. PacketHeader 규칙으로 완전한 패킷 단위만 처리하고,
//        처리한 총 바이트 수를 반환합니다(미완성 패킷의 잔여 바이트는 상위
//        RingBuffer가 보존했다가 다음 Recv 완료 때 이어 붙여줍니다).
//***************************************************************************
int32 CChatSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processedLen = 0;

	while( len - processedLen >= static_cast<int32>(sizeof(PacketHeader)) )
	{
		const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer + processedLen);

		// 프로토콜 위반(헤더보다 작은 size) — 기본 뼈대에서는 즉시 연결 종료.
		// 실제 서비스에서는 별도 로깅/악성 클라이언트 카운팅 등을 추가 권장.
		if( header->size < sizeof(PacketHeader) )
		{
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}

		// 패킷 전체가 아직 다 도착하지 않음 — 다음 Recv 완료를 기다린다.
		if( len - processedLen < header->size )
			break;

		HandlePacket(header);

		processedLen += header->size;
	}

	return processedLen;
}

//***************************************************************************
// @brief 패킷 타입에 따라 등록된 핸들러로 분기합니다.
// @details [설계 변경] 이 세션 안에 테이블을 두는 대신 전역
//          CChatPacketDispatcher에 위임 — 실제 핸들러 구현들은 이제 기능별
//          파일(ChatLoginHandler.cpp, ChatMessageHandler.cpp 등)에 흩어져
//          각자 정적 초기화 시점에 스스로 등록한다(REGISTER_CHAT_PACKET_HANDLER).
//          이 함수와 이 파일은 새 패킷이 추가돼도 전혀 수정할 필요가 없다.
//***************************************************************************
void CChatSession::HandlePacket(const PacketHeader* header)
{
	switch( CChatPacketDispatcher::Dispatch(*this, header) )
	{
	case EChatDispatchResult::UnknownType:
		// 알 수 없는 타입 — 기본 뼈대에서는 조용히 무시.
		// TODO: 로깅 및/또는 반복 시 연결 종료 정책 추가 고려.
		break;

	case EChatDispatchResult::SizeViolation:
		// 등록된 최소 크기 미달 — 프로토콜 위반으로 간주해 연결 종료.
		Disconnect(Iocp::CloseReason::InternalError);
		break;

	case EChatDispatchResult::Handled:
	default:
		break;
	}
}