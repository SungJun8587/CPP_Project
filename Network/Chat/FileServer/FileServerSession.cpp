
//***************************************************************************
// FileServerSession.cpp: implementation of the CFileServerSession class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerSession.h"

//***************************************************************************
// @brief CFileServerSession 클래스의 생성자
// @details [추가] 헤더 완료 콜백을 여기서 한 번만 건다 — CHttpRequestParser::
//          Reset()이 이 콜백을 지우지 않으므로, 이 세션이 살아있는 동안
//          (keep-alive로 여러 요청을 처리해도) 매 요청마다 다시 걸 필요가
//          없다. 콜백 자체는 "이번 요청이 스트리밍 대상인지" 매번 새로
//          판단한다(SetupUploadStreamingIfNeeded() 참고).
//***************************************************************************
CFileServerSession::CFileServerSession(CFileServerMain* server)
	: _server(server)
{
	_parser.SetHeadersCompleteCallback([this](CHttpRequestParser& parser)
		{
			SetupUploadStreamingIfNeeded(parser);
		});
}

//***************************************************************************
// @brief 연결 수립 시점 훅 — 파일 서버는 로그인 개념이 없어 특별히 할 일이 없다.
//***************************************************************************
void CFileServerSession::OnConnected()
{
}

//***************************************************************************
// @brief 연결 종료 시점 훅.
// @details [추가] 연결이 끊긴 시점에 업로드가 한창 스트리밍 중이었을 수
//          있다(클라이언트가 대용량 파일 전송 도중 끊음 등) — 그 경우
//          _uploadFileStream이 열려있는 채로 남으므로 정리하고, 반쯤 쓰인
//          임시 파일도 지운다(디스크에 고아 파일이 쌓이는 것 방지).
//***************************************************************************
void CFileServerSession::OnDisconnected()
{
	if( _uploadFileStream.is_open() )
		_uploadFileStream.close();

	if( _pendingUpload.hasFile && !_pendingUpload.tempFilePath.empty() )
	{
		std::error_code ec;
		std::filesystem::remove(_pendingUpload.tempFilePath, ec);
	}
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
// @details [수정 — 대용량 업로드] Complete 시점에 _multipartParser가
//          있었다면(=이번 요청이 스트리밍 업로드였다면) IsDone() 결과를
//          _pendingUpload.multipartOk에 반영한 뒤 HandleRequest()를
//          호출한다 — FileUploadHandler가 그 값을 보고 성공/실패를 판단한다.
//          그 다음 _multipartParser를 리셋해서 다음 요청에 이전 상태가
//          새어 들어가지 않게 한다(_pendingUpload 자체는
//          SetupUploadStreamingIfNeeded()가 매 요청 시작 시 새로 초기화함).
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
			if( _multipartParser != nullptr )
				_pendingUpload.multipartOk = _multipartParser->IsDone();

			const bool keepAlive = _parser.IsKeepAlive();

			auto sessionRef = std::static_pointer_cast<CFileServerSession>(shared_from_this());
			if( _server != nullptr )
				_server->HandleRequest(sessionRef, _parser);

			// 다음 요청을 위해 이번 요청 한정 상태를 정리한다. _pendingUpload
			// 자체는 다음 요청의 SetupUploadStreamingIfNeeded()가 시작하자마자
			// 새로 초기화하므로 여기서 굳이 지울 필요는 없지만, _multipartParser/
			// _uploadFileStream은 리소스(파일 핸들 등)를 쥐고 있으므로 명시적으로
			// 정리한다.
			_multipartParser.reset();
			if( _uploadFileStream.is_open() )
				_uploadFileStream.close();

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
			// [추가] 스트리밍 중이었다면 임시 파일도 같이 정리한다 —
			// OnDisconnected()가 곧 호출되겠지만(Disconnect()가 트리거),
			// 순서를 확신할 수 없으므로 여기서도 방어적으로 정리한다
			// (파일 삭제는 두 번 호출돼도 안전 — 이미 없으면 조용히 무시됨).
			if( _uploadFileStream.is_open() )
				_uploadFileStream.close();
			if( _pendingUpload.hasFile && !_pendingUpload.tempFilePath.empty() )
			{
				std::error_code ec;
				std::filesystem::remove(_pendingUpload.tempFilePath, ec);
			}

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
	constexpr size_t kSendChunkBytes = 8192;

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

//***************************************************************************
// @brief 이번 요청이 POST /upload + 유효한 boundary인지 확인하고, 맞으면
//        본문 처리를 스트리밍 모드로 전환한다.
// @details [설계] 여기서 스트리밍 여부를 파일 크기가 아니라 "경로가
//          /upload인지"만으로 결정한다 — 크기는 아직(Content-Length는
//          이미 헤더에 있지만) 파일이 진짜 몇 바이트인지와 무관하게,
//          POST /upload는 항상 같은 방식(스트리밍)으로 처리하는 게
//          FileUploadHandler 쪽 코드를 "스트리밍 경로/비스트리밍 경로"
//          둘로 안 쪼개도 되게 해준다 — 작은 파일이어도 스트리밍 오버헤드는
//          미미하다(임시 파일 하나 여닫는 정도).
//***************************************************************************
void CFileServerSession::SetupUploadStreamingIfNeeded(CHttpRequestParser& parser)
{
	// 매 요청마다 이전 요청의 잔재가 안 남게 새로 초기화.
	_pendingUpload = SPendingUploadResult{};
	_multipartParser.reset();
	_currentPartIsFile = false;
	_currentSmallFieldName.clear();
	_currentSmallFieldValue.clear();

	if( parser.GetMethod() != "POST" || parser.GetPath() != "/upload" )
		return; // 업로드 요청이 아님 — 기존 방식(비스트리밍) 그대로 둔다

	std::string boundary;
	if( !HTTP::ExtractBoundary(parser.FindHeader("Content-Type"), boundary) )
		return; // boundary가 없음 — FileUploadHandler가 이 경우도 처리(400 응답)

	_pendingUpload.attempted = true;

	_multipartParser = std::make_unique<HTTP::CMultipartStreamParser>(boundary);
	_multipartParser->SetCallbacks(
		[this](const HTTP::SMultipartPartInfo& info) { OnMultipartPartBegin(info); },
		[this](const char* data, size_t len) { OnMultipartPartData(data, len); },
		[this]() { OnMultipartPartEnd(); });

	const size_t maxUploadBytes = (_server != nullptr) ? _server->GetMaxUploadBytes() : (64ULL * 1024 * 1024);

	parser.SetBodyStreamCallback(
		[this](const char* data, size_t len) { OnUploadBodyChunk(data, len); },
		maxUploadBytes);
}

//***************************************************************************
// @brief 본문 바이트를 그대로 _multipartParser에 흘려보낸다.
//***************************************************************************
void CFileServerSession::OnUploadBodyChunk(const char* data, size_t len)
{
	if( _multipartParser == nullptr )
		return;

	if( !_multipartParser->Feed(data, len) )
	{
		// 형식이 깨짐 — 이후 파트 콜백은 더 안 온다. _pendingUpload.multipartOk는
		// OnRecv()가 Complete 시점에 IsDone()==false로 확인해 자연스럽게
		// false로 남으므로, 여기서는 열려있던 파일 스트림만 정리한다.
		if( _uploadFileStream.is_open() )
			_uploadFileStream.close();
	}
}

//***************************************************************************
// @brief 새 멀티파트 파트의 헤더가 파싱된 직후 호출됨 — filename 유무로
//        "file" 파트인지 판단해 이후 데이터를 어디로 보낼지 결정한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartBegin(const HTTP::SMultipartPartInfo& info)
{
	if( !info.filename.empty() )
	{
		_currentPartIsFile = true;

		// 임시 파일명은 무작위로 — 동시 업로드(여러 세션) 간 충돌 방지.
		BYTE randomBytes[8] = {};
		Crypto::CCryptoUtil::GenerateRandomBytes(randomBytes, sizeof(randomBytes));
		const std::string randomHex = Crypto::CCryptoUtil::ToHex(randomBytes, sizeof(randomBytes));

		std::error_code ec;
		std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
		if( ec )
			tempDir = std::filesystem::path("."); // 임시 디렉터리 조회 실패 시 최후의 수단

		const std::string tempPath = (tempDir / ("upload_" + randomHex + ".tmp")).string();

		_uploadFileStream.open(tempPath, std::ios::binary | std::ios::trunc);
		if( _uploadFileStream.is_open() )
		{
			_pendingUpload.hasFile = true;
			_pendingUpload.tempFilePath = tempPath;
			_pendingUpload.originalFileName = info.filename;
			_pendingUpload.fileContentType = info.contentType;
			_pendingUpload.fileSizeBytes = 0;
		}
		else
		{
			LOG_ERROR(_T("CFileServerSession::OnMultipartPartBegin: 임시 파일 열기 실패 (%hs)"), tempPath.c_str());
		}
	}
	else
	{
		_currentPartIsFile = false;
		_currentSmallFieldName = info.name;
		_currentSmallFieldValue.clear();
	}
}

//***************************************************************************
// @brief 현재 파트의 데이터 바이트 — "file" 파트면 임시 파일에 바로 쓰고,
//        아니면(작은 필드) 메모리 버퍼에 누적한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartData(const char* data, size_t len)
{
	if( _currentPartIsFile )
	{
		if( _uploadFileStream.is_open() )
		{
			_uploadFileStream.write(data, static_cast<std::streamsize>(len));
			_pendingUpload.fileSizeBytes += static_cast<int64_t>(len);
		}
	}
	else
	{
		_currentSmallFieldValue.append(data, len);
	}
}

//***************************************************************************
// @brief 현재 파트의 데이터가 끝남 — "file" 파트면 임시 파일을 닫고,
//        작은 필드면("token") 결과에 반영한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartEnd()
{
	if( _currentPartIsFile )
	{
		if( _uploadFileStream.is_open() )
			_uploadFileStream.close();
	}
	else if( _currentSmallFieldName == "token" )
	{
		_pendingUpload.tokenValue = _currentSmallFieldValue;
		_pendingUpload.hasToken = true;
	}
	// 다른 작은 필드가 추가되면 여기 else-if 분기만 추가하면 된다.
}