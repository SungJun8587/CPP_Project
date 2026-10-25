
//***************************************************************************
// ChatSession.cpp: implementation of the CChatSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <cstring>

namespace
{
	// 프로토콜의 어떤 패킷도 넘지 않는 상한 — 이보다 큰 size는 프로토콜 위반으로 보고 연결을 끊는다.
	// (수신 버퍼보다 큰 size가 오면 패킷이 영원히 완성되지 않아 연결이 멈추는 것을 막는다)
	constexpr int32 kMaxPacketBytes = 8192;
}

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
	if( IsLoggedIn() )
	{
		if( _server != nullptr )
		{
			// 이 세션이 갖고 있던 부가 상태를 정리한다: 방 멤버십 → 세션 레지스트리(친구에게 종료 알림).
			// _publicId를 읽으므로 _loggedIn을 내리기 전에 호출한다.
			_server->LeaveCurrentRoom(this);
			const bool accountStillOnline = _server->OnFriendLogout(this);

			// 같은 계정의 다른 세션(중복 로그인)이 남아 있으면 Redis 로그인 기록을 지우지 않는다.
			if( !accountStillOnline )
				_server->OnUserLogout(_publicId);
		}
		_loggedIn.store(false, std::memory_order_release);
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
		if( header->size < sizeof(PacketHeader) || header->size > kMaxPacketBytes )
		{
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}

		// 패킷 전체가 아직 다 도착하지 않음 — 다음 Recv 완료를 기다린다.
		if( len - processedLen < header->size )
			break;

		// 종류와 무관한 전체 수신 속도 제한 — 초과하면 악성/오작동 클라이언트로 보고 끊는다.
		if( !AllowPacket() )
		{
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}

		HandlePacket(header, static_cast<size_t>(len - processedLen));

		processedLen += header->size;
	}

	return processedLen;
}

//***************************************************************************
// @brief 패킷 타입에 따라 등록된 핸들러로 분기합니다.
// @details [설계 변경] 이 세션 안에 테이블을 두는 대신 전역
//          CChatPacketDispatcher에 위임 — 실제 핸들러 구현들은 이제 기능별
//          파일(ChatAccountHandler.cpp, ChatMessageHandler.cpp 등)에 흩어져
//          각자 정적 초기화 시점에 스스로 등록한다(REGISTER_CHAT_PACKET_HANDLER).
//          이 함수와 이 파일은 새 패킷이 추가돼도 전혀 수정할 필요가 없다.
// @param bufferSize 이 header 시점부터 수신 버퍼에 남아있는 바이트 수(호출부인
//        OnRecv()가 이미 header->size <= bufferSize임을 검증했지만, 공용
//        디스패처(CPacketDispatcher)가 한 번 더 방어적으로 확인한다).
//***************************************************************************
void CChatSession::HandlePacket(const PacketHeader* header, size_t bufferSize)
{
	// 알 수 없는 타입이 연속으로 이 횟수에 이르면 프로토콜 위반 클라이언트로 보고 연결을 끊는다.
	constexpr int32 kMaxUnknownPacketStreak = 10;

	switch( CChatPacketDispatcher::Dispatch(*this, header, bufferSize) )
	{
	case EChatDispatchResult::UnknownType:
	{
		// 알 수 없는 타입은 무시하되 기록하고, 연속되면 연결을 종료한다.
		const int32 streak = _unknownPacketStreak.fetch_add(1) + 1;
		LOG_ERROR(_T("CChatSession::HandlePacket: unknown packet type(%u), streak(%d)"),
			static_cast<unsigned>(header->type), streak);
		if( streak >= kMaxUnknownPacketStreak )
			Disconnect(Iocp::CloseReason::InternalError);
		return;
	}

	case EChatDispatchResult::SizeViolation:
		// 등록된 최소 크기 미달 — 프로토콜 위반으로 간주해 연결 종료.
		Disconnect(Iocp::CloseReason::InternalError);
		break;

	case EChatDispatchResult::Handled:
		_unknownPacketStreak.store(0);
		break;

	default:
		break;
	}
}