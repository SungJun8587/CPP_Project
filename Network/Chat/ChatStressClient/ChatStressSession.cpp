
//***************************************************************************
// ChatStressSession.cpp : implementation of the CChatStressSession class.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressSession.h"

//***************************************************************************
// @brief 가상 클라이언트 세션을 생성하고 연결 시도 중 카운터를 올린다.
// @param index 가상 클라이언트 번호
// @param hasToken true이면 publicId/token으로 재접속, false이면 신규 가입
// @param publicId 재접속에 쓸 계정 식별자(hasToken=true일 때만 의미)
// @param token 재접속에 쓸 토큰(hasToken=true일 때만 의미)
//***************************************************************************
CChatStressSession::CChatStressSession(int index, bool hasToken, const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::array<BYTE, kTokenBytes>& token)
	: _index(index), _hasToken(hasToken), _publicId(publicId), _token(token)
{
	_tCreatedUs = ptime::MonotonicNowMicros();
	S.connecting++;
}

//***************************************************************************
// @brief ConnectEx 게시 후 연결이 완료되기 전에 세션이 사라지는 경로(IOCP 등록
//        실패 등)에서도 connecting 카운터가 어긋나지 않게 정리한다.
//***************************************************************************
CChatStressSession::~CChatStressSession()
{
	if( _state.exchange(State::Closed) == State::Connecting )
	{
		S.connecting--;
		S.connectFail++;
		S.dead++;
	}
}

//***************************************************************************
// @brief 이 클라이언트가 로그인 후 입장할 방 번호를 계산한다.
// @details roomSpread가 1보다 크면 roomId..roomId+spread-1에 번호 순으로 분산한다.
// @return 입장할 방 번호, 0이면 로비 유지
//***************************************************************************
int CChatStressSession::RoomFor() const
{
	return g_cfg.roomId <= 0 ? 0 : g_cfg.roomId + (_index % (std::max)(1, g_cfg.roomSpread));
}

//***************************************************************************
// @brief TCP 연결 완료 — CChatClientSession과 동일하게 곧바로 LoginReq 전송.
//***************************************************************************
void CChatStressSession::OnConnected()
{
	_state.store(State::LoggingIn, std::memory_order_release);
	S.connecting--;
	S.connected++;
	S.connectOk++;
	S.connectLat.Add(ptime::MonotonicNowMicros() - _tCreatedUs);

	LoginReqPacket req{};
	req.type = static_cast<uint16>(EChatPacketType::LoginReq);
	req.size = sizeof(req);
	req.hasToken = _hasToken ? 1 : 0;

	if( !_hasToken )
	{
		// 신규 가입: 닉네임 = prefix + 실행 태그 + 5자리 번호
		std::snprintf(req.userId, sizeof(req.userId), "%s%s%05d", g_cfg.prefix.c_str(), g_runTag.c_str(), _index);
	}
	else
	{
		::memcpy(req.publicId, _publicId.data(), _publicId.size());
		::memcpy(req.token, _token.data(), _token.size());
	}

	_tLoginSentUs = ptime::MonotonicNowMicros();
	Send(&req, sizeof(req));
}

//***************************************************************************
// @brief 연결 종료 처리. 직전 상태에 맞춰 통계 카운터를 정리한다.
// @details 종료 절차(g_stop)에서 우리가 끊은 연결은 disconnects로 집계하지 않는다.
//***************************************************************************
void CChatStressSession::OnDisconnected()
{
	const State prev = _state.exchange(State::Closed);
	switch( prev )
	{
	case State::Connecting:	// 연결 자체가 실패
		S.connecting--; S.connectFail++; S.dead++;
		break;
	case State::LoggingIn:
		S.connected--; S.dead++;
		if( !g_stop ) S.disconnects++;
		break;
	case State::Active:
		S.connected--; S.loggedIn--; S.dead++;
		if( !g_stop ) S.disconnects++;	// 종료 절차에서 우리가 끊은 것은 집계하지 않는다
		break;
	case State::Closed:
		break;
	}
}

//***************************************************************************
// @brief 수신 데이터 처리 (CChatClientSession::OnRecv()와 동일한 프레이밍 규칙).
// @param buffer 수신 버퍼
// @param len 버퍼에 쌓인 바이트 수
// @return 처리(소비)한 바이트 수. 완성되지 않은 패킷은 남겨 두어 다음 수신에서 이어 처리한다.
//***************************************************************************
int32 CChatStressSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processed = 0;
	while( len - processed >= static_cast<int32>(sizeof(PacketHeader)) )
	{
		const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer + processed);

		if( header->size < sizeof(PacketHeader) )
		{
			S.protoErrors++;
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}
		if( len - processed < header->size )
			break;	// 패킷 전체 미도착 — 다음 Recv에서 이어 처리

		S.rxPkts++;
		HandlePacket(header);
		processed += header->size;
	}

	S.rxBytes += processed;
	return processed;
}

namespace
{
	//***************************************************************************
	// @brief RTT 마커 "ST|<runTag>|<idx>|<sendUs>|..."를 파싱한다.
	// @details runTag는 1~15자(구분자 '|' 제외). 형식이 맞지 않으면 false.
	//          sscanf 계열(보안 경고/버퍼 크기 인자) 대신 포인터 이동으로 직접 읽는다.
	// @param msg NUL 종료된 채팅 본문
	// @param tag 파싱한 runTag가 채워질 버퍼
	// @param idx 파싱한 클라이언트 번호
	// @param sendUs 파싱한 송신 시각(마이크로초)
	// @return 마커 형식이 맞으면 true
	//***************************************************************************
	bool ParseRttMarker(const char* msg, char(&tag)[16], int& idx, long long& sendUs)
	{
		if( msg[0] != 'S' || msg[1] != 'T' || msg[2] != '|' ) return false;

		const char* p = msg + 3;
		const char* end = ::strchr(p, '|');
		if( end == nullptr || end == p || (end - p) > 15 ) return false;
		::memcpy(tag, p, static_cast<size_t>(end - p));
		tag[end - p] = 0;

		p = end + 1;
		char* next = nullptr;
		const long idxValue = ::strtol(p, &next, 10);
		if( next == p || *next != '|' ) return false;
		idx = static_cast<int>(idxValue);

		p = next + 1;
		const long long value = ::strtoll(p, &next, 10);
		if( next == p ) return false;
		sendUs = value;
		return true;
	}
}

//***************************************************************************
// @brief 패킷 타입별 처리. 부하 도구라 핸들러 등록 테이블 대신 switch로 직접 분기
//        (패킷당 함수 포인터 간접 호출/컨텍스트 캐스팅 비용 제거). 크기 검증은
//        CPacketDispatcher의 방어적 검증과 같은 규칙(최소 sizeof 확인)을 따른다.
// @param header 완전히 수신된 패킷의 헤더
//***************************************************************************
void CChatStressSession::HandlePacket(const PacketHeader* header)
{
	const int64_t now = ptime::MonotonicNowMicros();
	switch( static_cast<EChatPacketType>(header->type) )
	{
	case EChatPacketType::LoginRes:
		if( header->size < sizeof(LoginResPacket) ) { S.protoErrors++; return; }
		OnLoginRes(reinterpret_cast<const LoginResPacket*>(header));
		break;

	case EChatPacketType::RoomEnterRes:
		if( header->size < sizeof(RoomEnterResPacket) ) { S.protoErrors++; return; }
		if( reinterpret_cast<const RoomEnterResPacket*>(header)->success ) S.enterOk++; else S.enterFail++;
		break;

	case EChatPacketType::Chat:
	{
		if( header->size < sizeof(ChatPacket) ) { S.protoErrors++; return; }
		S.chatRecv++;
		char msg[257];
		::memcpy(msg, reinterpret_cast<const ChatPacket*>(header)->message, 256);
		msg[256] = 0;
		// RTT 마커: "ST|<runTag>|<idx>|<sendUs>|<seq>|"
		char tag[16] = {};
		int idx = -1;
		long long t = 0;
		if( ParseRttMarker(msg, tag, idx, t) && idx == _index && g_runTag == tag )
		{
			S.chatOwnRecv++;
			S.chatRtt.Add(now - t);
		}
		break;
	}

	case EChatPacketType::ListRoomsEndRes:
		if( header->size < sizeof(ListRoomsEndResPacket) ) { S.protoErrors++; return; }
		if( reinterpret_cast<const ListRoomsEndResPacket*>(header)->requestId == _pendingListId.load() )
		{
			S.listLat.Add(now - _pendingListT.load());
			S.listDone++;
			_pendingListId = -1;
		}
		break;

	case EChatPacketType::RoomMemberListEndRes:
		if( header->size < sizeof(RoomMemberListEndResPacket) ) { S.protoErrors++; return; }
		if( reinterpret_cast<const RoomMemberListEndResPacket*>(header)->requestId == _pendingMemberId.load() )
		{
			S.memberLat.Add(now - _pendingMemberT.load());
			S.memberDone++;
			_pendingMemberId = -1;
		}
		break;

	default:
		break;	// Item/알림/기록 등은 수신량(rxPkts/rxBytes)만 집계
	}
}

//***************************************************************************
// @brief 로그인 응답 처리. 성공하면 새 토큰을 저장하고 로그인 후 단계로 넘어간다.
// @param packet 수신한 LoginRes 패킷
//***************************************************************************
void CChatStressSession::OnLoginRes(const LoginResPacket* packet)
{
	if( _state.load() != State::LoggingIn )
	{
		S.protoErrors++;
		return;
	}

	S.loginLat.Add(ptime::MonotonicNowMicros() - _tLoginSentUs);

	if( packet->success == 0 )
	{
		S.loginFail++;
		Disconnect(Iocp::CloseReason::InternalError);
		return;
	}

	S.loginOk++;

	// 서버가 회전 발급한 새 토큰을 저장해 둔다(--accounts 사용 시 다음 실행에서 재접속)
	ChatStressAccount acc;
	::memcpy(acc.publicId.data(), packet->publicId, acc.publicId.size());
	::memcpy(acc.token.data(), packet->token, acc.token.size());
	CChatStressAccounts::Instance().Update(_index, acc);

	OnLoggedIn();
}

//***************************************************************************
// @brief 로그인 완료 처리. 주기 송신 시각을 흩어 설정하고 Active로 전환하며,
//        방 입장이 설정돼 있으면 RoomEnterReq를 보낸다.
//***************************************************************************
void CChatStressSession::OnLoggedIn()
{
	const int64_t now = ptime::MonotonicNowMicros();
	_myRoom = RoomFor();

	// 시작 시점을 흩어 모든 클라이언트가 같은 순간에 보내는 동기화 폭주를 피한다
	auto jitter = [&](int ms) -> int64_t
		{
			return now + static_cast<int64_t>(ms > 0 ? (std::rand() % ms) : 0) * 1000 + 200 * 1000;
		};
	_nextChat = g_cfg.chatIntervalMs ? jitter(g_cfg.chatIntervalMs) : INT64_MAX;
	_nextList = g_cfg.listIntervalMs ? jitter(g_cfg.listIntervalMs) : INT64_MAX;
	_nextMember = g_cfg.memberIntervalMs ? jitter(g_cfg.memberIntervalMs) : INT64_MAX;
	_nextCount = g_cfg.countIntervalMs ? jitter(g_cfg.countIntervalMs) : INT64_MAX;

	_state.store(State::Active, std::memory_order_release);
	S.loggedIn++;

	if( _myRoom > 0 )
	{
		RoomEnterReqPacket req{};
		req.type = static_cast<uint16>(EChatPacketType::RoomEnterReq);
		req.size = sizeof(req);
		req.roomId = _myRoom;
		Send(&req, sizeof(req));
	}
}

//***************************************************************************
// @brief 스케줄러 스레드가 주기적으로 호출. 때가 된 요청(채팅/방목록/멤버목록/동접자)을
//        Send()로 보낸다. Send()는 CChatClientMain::SendChat()이 콘솔 스레드에서
//        부르는 것과 같은 경로라 워커 스레드 밖에서 호출해도 안전하다.
// @param now 현재 시각(마이크로초, ptime::MonotonicNowMicros() 기준)
//***************************************************************************
void CChatStressSession::Tick(int64_t now)
{
	if( _state.load(std::memory_order_acquire) != State::Active )
		return;

	if( now >= _nextChat.load() )
	{
		ChatPacket packet{};
		packet.type = static_cast<uint16>(EChatPacketType::Chat);
		packet.size = sizeof(packet);
		std::snprintf(packet.message, sizeof(packet.message), "ST|%s|%d|%lld|%u|",
			g_runTag.c_str(), _index, static_cast<long long>(now), ++_chatSeq);
		const size_t len = std::strlen(packet.message);
		for( size_t i = len; i < static_cast<size_t>(g_cfg.msgBytes) && i < sizeof(packet.message) - 1; ++i )
			packet.message[i] = 'x';

		if( Send(&packet, sizeof(packet)) )
		{
			S.chatSent++;
			S.txPkts++; S.txBytes += sizeof(packet);
		}
		else
			S.sendDropped++;
		_nextChat = now + static_cast<int64_t>(g_cfg.chatIntervalMs) * 1000;
	}

	if( now >= _nextList.load() )
	{
		if( _pendingListId.load() >= 0 && now - _pendingListT.load() > 10 * 1000 * 1000 )
			_pendingListId = -1;	// 10초 무응답이면 폐기하고 새로 요청

		if( _pendingListId.load() < 0 )
		{
			ListRoomsReqPacket req{};
			req.type = static_cast<uint16>(EChatPacketType::ListRoomsReq);
			req.size = sizeof(req);
			req.requestId = ++_reqId;
			req.scope = static_cast<uint8_t>(std::rand() & 1);
			req.page = std::rand() % 5;
			req.pageSize = g_cfg.pageSize;
			_pendingListT = now;
			_pendingListId = req.requestId;
			if( Send(&req, sizeof(req)) ) { S.listSent++; S.txPkts++; S.txBytes += sizeof(req); }
			else { S.sendDropped++; _pendingListId = -1; }
		}
		_nextList = now + static_cast<int64_t>(g_cfg.listIntervalMs) * 1000;
	}

	if( now >= _nextMember.load() && _myRoom > 0 )
	{
		if( _pendingMemberId.load() >= 0 && now - _pendingMemberT.load() > 10 * 1000 * 1000 )
			_pendingMemberId = -1;

		if( _pendingMemberId.load() < 0 )
		{
			RoomMemberListReqPacket req{};
			req.type = static_cast<uint16>(EChatPacketType::RoomMemberListReq);
			req.size = sizeof(req);
			req.requestId = ++_reqId;
			req.roomId = _myRoom;
			req.scope = static_cast<uint8_t>(std::rand() & 1);
			req.page = 0;
			req.pageSize = g_cfg.pageSize;
			_pendingMemberT = now;
			_pendingMemberId = req.requestId;
			if( Send(&req, sizeof(req)) ) { S.memberSent++; S.txPkts++; S.txBytes += sizeof(req); }
			else { S.sendDropped++; _pendingMemberId = -1; }
		}
		_nextMember = now + static_cast<int64_t>(g_cfg.memberIntervalMs) * 1000;
	}

	if( now >= _nextCount.load() )
	{
		ServerUserCountReqPacket req{};
		req.type = static_cast<uint16>(EChatPacketType::ServerUserCountReq);
		req.size = sizeof(req);
		if( Send(&req, sizeof(req)) ) { S.txPkts++; S.txBytes += sizeof(req); }
		_nextCount = now + static_cast<int64_t>(g_cfg.countIntervalMs) * 1000;
	}
}