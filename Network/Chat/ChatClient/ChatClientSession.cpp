
//***************************************************************************
// ChatClientSession.cpp: implementation of the CChatClientSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"

//***************************************************************************
// @brief CChatClientSession 생성자
// @param userId 로그인에 사용할 닉네임
// @param hasToken true면 token으로 재접속 시도, false면 신규 가입 시도
// @param token 재접속 토큰 원문(hasToken==false면 무시됨)
// @param client 이 세션을 소유한 클라이언트 파사드
//***************************************************************************
CChatClientSession::CChatClientSession(std::string userId, bool hasToken,
	std::array<BYTE, kTokenBytes> token, CChatClientMain* client)
	: _userId(std::move(userId))
	, _hasToken(hasToken)
	, _token(token)
	, _client(client)
{
}

//***************************************************************************
// @brief TCP 연결이 실제로 완료된 시점 — 곧바로 LoginReq를 전송합니다.
//***************************************************************************
void CChatClientSession::OnConnected()
{
	LoginReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::LoginReq);
	req.size = sizeof(req);

	const size_t copyLen = (std::min)(_userId.size(), sizeof(req.userId) - 1);
	::memcpy(req.userId, _userId.data(), copyLen);
	// 나머지 바이트는 {} 초기화로 이미 0-채움 → NUL 종단 보장

	req.hasToken = _hasToken ? 1 : 0;
	if( _hasToken )
		::memcpy(req.token, _token.data(), _token.size());

	Send(&req, sizeof(req));
}

//***************************************************************************
// @brief 연결 종료 시 호출 — 클라이언트 파사드에 통지합니다.
//***************************************************************************
void CChatClientSession::OnDisconnected()
{
	if( _client != nullptr )
		_client->OnSessionClosed();
}

//***************************************************************************
// @brief 수신 데이터 처리 (CChatSession::OnRecv()와 동일한 프레이밍 규칙).
//***************************************************************************
int32 CChatClientSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processedLen = 0;

	while( len - processedLen >= static_cast<int32>(sizeof(PacketHeader)) )
	{
		const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer + processedLen);

		if( header->size < sizeof(PacketHeader) )
		{
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}

		if( len - processedLen < header->size )
			break; // 패킷 전체 미도착 — 다음 Recv에서 이어 처리

		HandlePacket(header);
		processedLen += header->size;
	}

	return processedLen;
}

//***************************************************************************
// @brief 패킷 타입에 따라 등록된 핸들러로 분기합니다.
// @details [설계 변경] 이 세션 안에 테이블을 두는 대신 전역
//          CChatClientPacketDispatcher에 위임 — 실제 핸들러 구현들은
//          기능별 파일(ChatClientLoginHandler.cpp, ChatClientMessageHandler.cpp)에
//          흩어져 각자 정적 초기화 시점에 스스로 등록한다
//          (REGISTER_CHAT_CLIENT_PACKET_HANDLER). 이 함수와 이 파일은 새
//          패킷이 추가돼도 전혀 수정할 필요가 없다.
//***************************************************************************
void CChatClientSession::HandlePacket(const PacketHeader* header)
{
	switch( CChatClientPacketDispatcher::Dispatch(*this, header) )
	{
	case EChatDispatchResult::UnknownType:
		// 알 수 없는 타입 — 무시. TODO: 로깅.
		break;

	case EChatDispatchResult::SizeViolation:
		Disconnect(Iocp::CloseReason::InternalError);
		break;

	case EChatDispatchResult::Handled:
	default:
		break;
	}
}

//***************************************************************************
// @brief 채팅 메시지를 서버로 전송합니다. message가 254바이트를 넘으면 잘립니다.
//***************************************************************************
void CChatClientSession::SendChat(const std::string& message)
{
	ChatPacket packet{};
	packet.type = static_cast<uint16>(EChatPacketType::Chat);
	packet.size = sizeof(packet);

	const size_t copyLen = (std::min)(message.size(), sizeof(packet.message) - 1);
	::memcpy(packet.message, message.data(), copyLen);

	Send(&packet, sizeof(packet));
}

//***************************************************************************
// @brief 서버에 랜덤 닉네임 생성을 요청합니다.
//***************************************************************************
void CChatClientSession::SendNicknameGenerateReq()
{
	NicknameGenerateReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::NicknameGenerateReq);
	req.size = sizeof(req);

	Send(&req, sizeof(req));
}