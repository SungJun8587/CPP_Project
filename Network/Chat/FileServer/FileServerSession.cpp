
//***************************************************************************
// FileServerSession.cpp: implementation of the CFileServerSession class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerSession.h"
#include "FileServerMain.h"

//***************************************************************************
// @brief CFileServerSession 클래스의 생성자
//***************************************************************************
CFileServerSession::CFileServerSession(CFileServerMain* server)
	: _server(server)
{
}

//***************************************************************************
// @brief 연결 수립 시점 훅 — 파일 서버는 로그인 개념이 없어 특별히 할 일이 없다.
//***************************************************************************
void CFileServerSession::OnConnected()
{
}

//***************************************************************************
// @brief 연결 종료 시점 훅 — 마찬가지로 별도 정리할 세션 상태가 없다.
//***************************************************************************
void CFileServerSession::OnDisconnected()
{
}

//***************************************************************************
// @brief 수신 바이트를 CHttpRequestParser에 흘려보내고, 완성된 요청이
//        생기면 CFileServerMain에 라우팅을 위임합니다.
// @details [설계] 채팅 서버(ChatSession::OnRecv)는 고정 헤더+size 기반으로
// "패킷 하나가 왔는지"를 판단하지만, HTTP는 그런 고정 프레이밍이 없다 —
// 그래서 상태를 세션 안에 들고 있는 CHttpRequestParser에게 매번 있는
// 그대로의 바이트를 넘기고, 파서가 알아서 "요청 하나가 완성됐는지"를
// 판단하게 한다. 반환값(processedLen)은 파서가 내부적으로 이미 흡수한
// 바이트 수와 같다 — 파싱 중간 상태는 전부 _parser 멤버 안에 남아있으므로,
// 프레임워크가 별도로 바이트를 보관해줄 필요가 없다.
//***************************************************************************
int32 CFileServerSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processedLen = 0;

	while( processedLen < len )
	{
		const char* data = reinterpret_cast<const char*>(buffer + processedLen);
		const size_t remaining = static_cast<size_t>(len - processedLen);

		const HTTP::EParseState state = _parser.Feed(data, remaining);
		const size_t consumed = _parser.GetLastFeedConsumed();
		processedLen += static_cast<int32>(consumed);

		if( state == HTTP::EParseState::Complete )
		{
			const bool keepAlive = _parser.IsKeepAlive();

			auto sessionRef = std::static_pointer_cast<CFileServerSession>(shared_from_this());
			if( _server != nullptr )
				_server->HandleRequest(sessionRef, _parser);

			_parser.Reset();

			if( !keepAlive )
			{
				// [알려진 한계] Send()가 큐잉 즉시 동기적으로 나간다는 전제
				// 하에 바로 끊는다 — 만약 Send()가 비동기 완료 통지 기반이라면
				// "응답의 마지막 바이트까지 실제로 나간 뒤 닫기"를 보장하는
				// 별도 콜백/카운터가 필요하다.
				// [수정] Iocp::CloseReason에 Normal은 없다 — 실제 열거값은
				// None/RingBufferOverflow/SocketError/RemoteClosed/
				// ForcedClose/InternalError뿐이다(IocpSession.cpp 실사용
				// 확인). ForcedClose가 "명시적/의도적 종료"의 범용 사유로
				// 쓰이므로(CIocpSession::Disconnect(const TCHAR*) 오버로드가
				// 바로 이 값을 쓴다) 여기(Connection: close 응답 후 정상
				// 종료)에 가장 잘 맞는다.
				Disconnect(Iocp::CloseReason::ForcedClose);
				break;
			}
			// keep-alive면 루프를 계속 돌려 같은 recv 버퍼에 파이프라이닝된
			// 다음 요청의 선두 바이트가 있는지 마저 처리한다.
		}
		else if( state == HTTP::EParseState::Error )
		{
			// 형식이 깨진 요청 — 격식 갖춘 응답 없이 바로 연결을 끊는다.
			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}
		else
		{
			// 아직 요청이 덜 도착함 — 이번에 받은 바이트는 파서가 전부
			// 내부 버퍼로 흡수했을 것이므로(consumed == remaining이 보통),
			// 더 받을 때까지 대기.
			break;
		}
	}

	return processedLen;
}

//***************************************************************************
// @brief 완성된 HTTP 응답 문자열을 그대로 전송합니다.
// @details [수정] 송신 버퍼가 청크 기반(CSendBufferChunk, 고정 크기
//          Iocp::SEND_BUFFER_CHUNK_SIZE)이다 — 그 크기보다 큰 데이터를
//          한 번에 Send()하면 CSendBufferChunk::Open()의
//          ASSERT_CRASH(allocSize <= SEND_BUFFER_CHUNK_SIZE)에 걸려
//          프로세스가 죽는다(실제로 GET /images/* 이미지 응답에서 재현됨
//          — 이 프로젝트가 지금까지 다뤄온 채팅 패킷은 전부 청크 크기보다
//          훨씬 작아서 이 제약이 안 드러났을 뿐이다). 그래서 여기서
//          안전한 크기 이하로 쪼개 여러 번 Send()한다 — 호출부(HandleGetImage()
//          등)가 매번 이 제약을 기억할 필요 없이 SendRaw()만 쓰면 항상
//          안전하게 처리되도록 책임을 이 계층으로 옮겼다. TCP는 스트림이라
//          여러 번 나눠 보내도 순서만 지키면 수신측(HttpClient)엔 원래
//          하나로 보낸 것과 완전히 동일하게 읽힌다.
// @attention kSendChunkBytes는 Iocp::SEND_BUFFER_CHUNK_SIZE(8192바이트,
//          IocpCommon.h)와 정확히 동일한 값으로 맞춰뒀다 — 이 상수가
//          바뀌면 여기도 같이 갱신해야 한다.
//***************************************************************************
void CFileServerSession::SendRaw(const std::string& data)
{
	// Iocp::SEND_BUFFER_CHUNK_SIZE(IocpCommon.h)와 정확히 동일한 값 — 실제
	// 헤더로 확인됨(8192바이트). CSendBufferChunk::Open()의 ASSERT_CRASH가
	// "<="(이하)로 검사하므로 이 값과 정확히 같아도 안전하다.
	constexpr size_t kSendChunkBytes = Iocp::SEND_BUFFER_CHUNK_SIZE;

	if( data.empty() )
	{
		Send(data.data(), 0);
		return;
	}

	for( size_t offset = 0; offset < data.size(); offset += kSendChunkBytes )
	{
		const size_t take = (std::min)(kSendChunkBytes, data.size() - offset);
		Send(data.data() + offset, static_cast<int32>(take));
	}
}