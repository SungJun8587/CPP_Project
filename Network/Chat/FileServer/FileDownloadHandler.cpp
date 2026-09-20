
//***************************************************************************
// FileDownloadHandler.cpp: implementation of the CFileDownloadHandler class.
//
// [설계 변경 — 진짜 스트리밍] 이전 버전은 IFileStorage::LoadFile()로 파일
// 전체를 std::vector<BYTE>에 올린 뒤 Range만큼 잘라서 응답했다 — Range
// 기능 자체는 정확했지만, 큰 파일의 일부만 요청해도 디스크 I/O와 메모리
// 사용량은 항상 파일 전체 크기였다. 이제는 OpenStream()으로 스트림을 열고,
// CFileSendBufferPool의 청크(64KB) 단위로 "읽은 만큼 바로 전송"을 반복한다
// — 피크 메모리 사용량이 파일 크기가 아니라 청크 크기 수준으로 줄었다.
//
// [설계 변경 — CHttpResponseBuilder로 교체] 헤더 블록 조립에 직접 문자열을
// 이어붙이던 방식 대신, 이미 있던 CHttpResponseBuilder(HttpPacketBuilder.h)를
// 쓴다. 이 빌더는 AddHeader()에 넘긴 string_view를 Build() 시점까지 그대로
// 참조만 한다(복사 없음) — 그래서 std::to_string() 같은 임시값을 그
// 자리에서 바로 넘기면 Build() 시점엔 이미 소멸된 문자열을 참조하는
// 댕글링 버그가 된다. 아래 코드가 Content-Length/Content-Range 값을
// 전부 지역 std::string 변수(contentLengthStr/contentRangeStr)에 먼저
// 담아두고 그 변수를 넘기는 이유가 이것 — Build() 호출이 끝날 때까지
// 그 변수들이 스코프 안에 살아있어야 한다.
//***************************************************************************

#include "pch.h"
#include "FileDownloadHandler.h"

CFileDownloadHandler::CFileDownloadHandler(IFileStorage* storage)
	: _storage(storage)
{
}

//***************************************************************************
// @brief GET /images/{path} 처리. Range 헤더가 있으면 206 Partial Content로,
//        없거나 처리 못 하는 형태(멀티파트 range 등)면 200 OK 전체 응답으로
//        돌려준다. 헤더를 먼저 보낸 뒤, 본문은 파일을 스트림으로 열어
//        청크 단위로 읽으면서 그때그때 전송한다(파일 전체를 한 번에
//        메모리에 올리지 않음).
//***************************************************************************
void CFileDownloadHandler::Handle(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, const std::string& relativePath, bool keepAlive)
{
	if( _storage == nullptr )
	{
		SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Storage not initialized", keepAlive);
		return;
	}

	std::unique_ptr<CFileStream> stream = _storage->OpenStream(relativePath);
	if( stream == nullptr || !stream->IsOpen() )
	{
		SendSimpleResponse(session, 404, "Not Found", "text/plain", "File not found", keepAlive);
		return;
	}

	const int64_t totalSize = stream->GetFileSize();
	const std::string contentType = GuessContentType(relativePath);
	const std::string_view rangeHeaderValue = request.FindHeader("Range");

	int64_t bodyStart = 0;
	int64_t bodyLength = totalSize;
	int statusCode = 200;
	std::string statusText = "OK";
	bool isPartial = false;
	HTTP::SByteRange range{};

	if( !rangeHeaderValue.empty() )
	{
		std::optional<HTTP::SByteRange> parsedRange = HTTP::ParseRange(rangeHeaderValue, totalSize);
		if( !parsedRange.has_value() )
		{
			SendRangeNotSatisfiable(session, totalSize, keepAlive);
			return;
		}

		range = *parsedRange;
		bodyStart = range.start;
		bodyLength = range.Length();
		statusCode = 206;
		statusText = "Partial Content";
		isPartial = true;
	}

	// [주의] CHttpResponseBuilder::AddHeader()는 string_view를 그대로
	// 참조만 하므로, 아래 두 값은 Build() 호출이 끝날 때까지 이 스코프
	// 안에 살아있어야 한다(파일 상단 설계 노트 참고).
	const std::string contentLengthStr = std::to_string(bodyLength);
	std::string contentRangeStr;
	if( isPartial )
		contentRangeStr = "bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/" + std::to_string(totalSize);

	CHttpResponseBuilder builder;
	builder.SetStatus(statusCode, statusText)
		.AddHeader("Content-Type", contentType)
		.AddHeader("Accept-Ranges", "bytes")
		.AddHeader("Content-Length", contentLengthStr)
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close");
	if( isPartial )
		builder.AddHeader("Content-Range", contentRangeStr);
	builder.SetBody(""); // 바디는 비워서 헤더 블록만 조립 — 실제 바디는 아래서 청크로 직접 전송

	auto [headerData, headerLen] = builder.Build();
	session->SendRaw(std::string(headerData, headerLen));

	// 본문 스트리밍 — bodyStart부터 bodyLength바이트를 청크 단위로 읽어서
	// 그때그때 전송한다.
	if( !stream->Seek(bodyStart) )
	{
		// 헤더는 이미 나갔는데 여기서 실패하면 온전한 HTTP 응답을 더 이상
		// 만들 수 없다 — 데모 범위에서는 로그만 남기고 연결을 그대로 둔다
		// (클라이언트는 Content-Length보다 짧게 받아 타임아웃/오류로
		// 인지하게 된다). 실무라면 청크 전송 인코딩으로 바꿔 이런 경우도
		// 깔끔하게 종료 신호를 보낼 수 있지만, 이 서버의 응답은 전부
		// Content-Length 고정 방식이라 범위 밖 — 별도 개선 과제로 남겨둔다.
		LOG_ERROR(_T("CFileDownloadHandler::Handle: Seek 실패 (bodyStart=%lld)"), static_cast<long long>(bodyStart));
		return;
	}

	int64_t remaining = bodyLength;
	while( remaining > 0 )
	{
		std::vector<BYTE> chunk = CFileSendBufferPool::Acquire();

		const size_t toRead = static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(chunk.size()), remaining));
		const size_t actuallyRead = stream->ReadChunk(chunk.data(), toRead);

		if( actuallyRead == 0 )
		{
			// 예상보다 파일이 짧았던 경우(디스크에서 파일이 그 사이 변경된
			// 등 드문 상황) — 더 보낼 게 없으니 루프를 접는다. Content-Length를
			// 이미 약속한 값보다 적게 보내는 셈이지만, 감지/복구 불가능한
			// 상황이라(응답 헤더를 이미 보낸 뒤) 로그만 남긴다.
			LOG_ERROR(_T("CFileDownloadHandler::Handle: 예상보다 일찍 EOF (남은 %lld바이트)"), static_cast<long long>(remaining));
			CFileSendBufferPool::Release(std::move(chunk));
			break;
		}

		session->SendRaw(std::string(reinterpret_cast<const char*>(chunk.data()), actuallyRead));
		remaining -= static_cast<int64_t>(actuallyRead);

		CFileSendBufferPool::Release(std::move(chunk));
	}
}

//***************************************************************************
// @brief 요청 Range가 파일 크기를 벗어나는 등 처리 불가능할 때 416을 보낸다.
//***************************************************************************
void CFileDownloadHandler::SendRangeNotSatisfiable(std::shared_ptr<CFileServerSession> session, int64_t totalSize, bool keepAlive)
{
	if( session == nullptr )
		return;

	// [주의] 위 Handle()과 동일한 이유 — Build() 호출 전까지 살아있어야
	// 하는 지역 변수.
	const std::string contentRangeStr = "bytes */" + std::to_string(totalSize);

	CHttpResponseBuilder builder;
	builder.SetStatus(416, "Range Not Satisfiable")
		.AddHeader("Content-Range", contentRangeStr)
		.AddHeader("Content-Length", "0")
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close")
		.SetBody("");

	auto [data, len] = builder.Build();
	session->SendRaw(std::string(data, len));
}

//***************************************************************************
// @brief 확장자로부터 Content-Type을 추정한다.
//***************************************************************************
std::string CFileDownloadHandler::GuessContentType(const std::string& path)
{
	const size_t dotPos = path.find_last_of('.');
	if( dotPos == std::string::npos )
		return "application/octet-stream";

	std::string ext = path.substr(dotPos + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

	if( ext == "png" ) return "image/png";
	if( ext == "jpg" || ext == "jpeg" ) return "image/jpeg";
	if( ext == "gif" ) return "image/gif";
	if( ext == "bmp" ) return "image/bmp";
	if( ext == "webp" ) return "image/webp";

	return "application/octet-stream";
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(에러 응답 전용).
//***************************************************************************
void CFileDownloadHandler::SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
	const std::string& contentType, const std::string& body, bool keepAlive)
{
	if( session == nullptr )
		return;

	CHttpResponseBuilder builder;
	builder.SetStatus(statusCode, statusText)
		.AddHeader("Content-Type", contentType)
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close")
		.SetBody(body);

	auto [data, len] = builder.Build();
	session->SendRaw(std::string(data, len));
}