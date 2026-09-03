
//***************************************************************************
// ChatSession.cpp: implementation of the CChatSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"

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
// @brief 패킷 타입 → 핸들러 매핑 테이블의 원소.
// @details minSize는 이 타입 패킷이 가져야 할 최소 크기(보통 고정 크기
//          패킷이면 그 구조체의 sizeof()) — 핸들러 각자가 크기를 검증하는
//          대신 여기서 한 번에 강제되므로, 새 핸들러 추가 시 크기 체크를
//          깜빡할 여지가 없다.
//***************************************************************************
struct ChatPacketHandlerEntry
{
	uint16	minSize;
	void	(*handler)(CChatSession& session, const PacketHeader* header);
};

//***************************************************************************
// @brief 패킷 타입에 따라 등록된 핸들러로 분기합니다.
// @details [설계 변경] switch문 대신 타입→핸들러 테이블 조회로 변경 —
//          새 패킷을 추가할 때 이 함수를 안 건드리고 테이블에 한 줄만
//          추가하면 된다. 함수-로컬 static이라 최초 호출 시 1회만
//          생성되고(C++11부터 함수 로컬 static 초기화는 스레드 세이프),
//          이후 모든 세션·모든 스레드가 같은 읽기 전용 테이블을 공유해
//          조회한다(수정 없음 → 락 불필요).
//***************************************************************************
void CChatSession::HandlePacket(const PacketHeader* header)
{
	static const std::unordered_map<uint16, ChatPacketHandlerEntry> kHandlerTable =
	{
		{ static_cast<uint16>(EChatPacketType::LoginReq),	{ sizeof(LoginReqPacket),	&CChatSession::HandleLoginReq } },
		{ static_cast<uint16>(EChatPacketType::Chat),		{ sizeof(ChatPacket),		&CChatSession::HandleChat } },
		// 새 패킷 추가 예시:
		// { static_cast<uint16>(EChatPacketType::WhisperReq), { sizeof(WhisperReqPacket), &CChatSession::HandleWhisperReq } },
	};

	auto it = kHandlerTable.find(header->type);
	if( it == kHandlerTable.end() )
	{
		// 알 수 없는 타입 — 기본 뼈대에서는 조용히 무시.
		// TODO: 로깅 및/또는 반복 시 연결 종료 정책 추가 고려.
		return;
	}

	if( header->size < it->second.minSize )
	{
		// 등록된 최소 크기 미달 — 프로토콜 위반으로 간주해 연결 종료.
		Disconnect(Iocp::CloseReason::InternalError);
		return;
	}

	it->second.handler(*this, header);
}

//***************************************************************************
// @brief 로그인 요청 처리. Redis 등록은 CChatServer::OnUserLogin()에 위임합니다.
//***************************************************************************
void CChatSession::HandleLoginReq(CChatSession& session, const PacketHeader* header)
{
	const LoginReqPacket* packet = reinterpret_cast<const LoginReqPacket*>(header);

	if( session._loggedIn )
		return; // 중복 로그인 요청 무시 — 재로그인/강퇴 정책은 기본 뼈대 범위 밖

	// userId[32]가 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도
	// 버퍼에 복사 후 문자열화.
	char safeBuf[sizeof(packet->userId) + 1] = {};
	::memcpy(safeBuf, packet->userId, sizeof(packet->userId));
	session._userId.assign(safeBuf);

	if( session._userId.empty() )
	{
		session.Disconnect(Iocp::CloseReason::InternalError);
		return;
	}

	session._loggedIn = true;
	session._server->OnUserLogin(session._userId);

	LoginResPacket res{};
	res.size = sizeof(res);
	res.type = static_cast<uint16>(EChatPacketType::LoginRes);
	res.success = 1;
	session.Send(&res, sizeof(res));
}

//***************************************************************************
// @brief 채팅 메시지 처리. 기본 뼈대는 로그인한 발신자의 메시지를 전체
//        브로드캐스트합니다(발신자 제외, 채팅방 분리 등은 범위 밖).
//***************************************************************************
void CChatSession::HandleChat(CChatSession& session, const PacketHeader* header)
{
	const ChatPacket* packet = reinterpret_cast<const ChatPacket*>(header);

	if( !session._loggedIn )
		return; // 로그인 전 채팅 무시

	session._server->Broadcast(packet, packet->size);
}
