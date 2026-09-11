
//***************************************************************************
// ChatClientSession.cpp: implementation of the CChatClientSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

#include <cstring>
#include <algorithm>

//***************************************************************************
// @brief CChatClientSession 생성자
// @param userId hasToken==false일 때만 의미: 신규 가입 시 원하는 닉네임
// @param hasToken true면 publicId+token으로 재접속 시도, false면 신규 가입 시도
// @param publicId hasToken==true일 때만 의미: 재접속 대상 계정의 안정 식별자
// @param token 재접속 토큰 원문(hasToken==false면 무시됨)
// @param client 이 세션을 소유한 클라이언트 파사드
//***************************************************************************
CChatClientSession::CChatClientSession(std::string userId, bool hasToken,
	std::array<BYTE, kPublicIdBytes> publicId,
	std::array<BYTE, kTokenBytes> token, CChatClientMain* client)
	: _userId(std::move(userId))
	, _hasToken(hasToken)
	, _publicId(publicId)
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

	req.hasToken = _hasToken ? 1 : 0;

	if( !_hasToken )
	{
		// 신규 가입 — userId(원하는 닉네임)만 의미 있음
		const size_t copyLen = (std::min)(_userId.size(), sizeof(req.userId) - 1);
		::memcpy(req.userId, _userId.data(), copyLen);
		// 나머지 바이트는 {} 초기화로 이미 0-채움 → NUL 종단 보장
	}
	else
	{
		// 재접속 — publicId+token만 의미 있음
		::memcpy(req.publicId, _publicId.data(), _publicId.size());
		::memcpy(req.token, _token.data(), _token.size());
	}

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
	// [참고] packet{}이 새로 추가된 nickname 필드까지 0으로 초기화해준다 —
	// 클라이언트가 보낼 땐 이 필드가 무시되므로(서버가 세션의 실제
	// 닉네임으로 채워 재브로드캐스트함) 별도로 채울 필요가 없다.
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

//***************************************************************************
// @brief 서버에 닉네임 변경을 요청합니다. newNickname이 kNicknameBytes-1을
//        넘으면 잘립니다(UTF-8 바이트 경계 확인은 호출부 책임 — 실제
//        검증은 서버 쪽 ChangeNicknameDBHandler.cpp가 다시 한번 함).
//***************************************************************************
void CChatClientSession::SendChangeNicknameReq(const std::string& newNickname)
{
	// 응답 패킷(ChangeNicknameResPacket)엔 success/reason만 실려 있고 새
	// 닉네임 문자열 자체는 없다 — 요청 시점의 값을 여기 잠깐 저장해뒀다가
	// 응답 처리(GetPendingNewNickname())에서 꺼내 쓴다.
	_pendingNewNickname = newNickname;

	ChangeNicknameReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::ChangeNicknameReq);
	req.size = sizeof(req);

	const size_t copyLen = (std::min)(newNickname.size(), sizeof(req.newNickname) - 1);
	::memcpy(req.newNickname, newNickname.data(), copyLen);

	Send(&req, sizeof(req));
}

//***************************************************************************
// @brief 서버에 방 입장을 요청합니다.
//***************************************************************************
void CChatClientSession::SendRoomEnterReq(int32 roomId)
{
	RoomEnterReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::RoomEnterReq);
	req.size = sizeof(req);
	req.roomId = roomId;

	Send(&req, sizeof(req));
}

//***************************************************************************
// @brief 서버에 방 퇴장(로비 복귀)을 요청합니다.
//***************************************************************************
void CChatClientSession::SendRoomLeaveReq()
{
	RoomLeaveReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::RoomLeaveReq);
	req.size = sizeof(req);

	Send(&req, sizeof(req));
}

//***************************************************************************
// @brief 서버에 전체 접속자 수(동접자수) 조회를 요청합니다(폴링용).
//***************************************************************************
void CChatClientSession::SendServerUserCountReq()
{
	ServerUserCountReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::ServerUserCountReq);
	req.size = sizeof(req);

	Send(&req, sizeof(req));
}