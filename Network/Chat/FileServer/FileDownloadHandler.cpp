
//***************************************************************************
// FileDownloadHandler.cpp: implementation of the CFileDownloadHandler class.
//
// [설계 — 스트리밍] OpenStream()으로 스트림을 열어 헤더를 보낸 뒤, 본문은
// 세션(CFileServerSession::BeginDownload)이 송신 완료에 맞춰 청크 단위로
// 읽어 보낸다 — 메모리에 쌓이는 양이 파일 크기가 아니라 상한으로 묶인다.
//
// [주의 — CHttpResponseBuilder] 헤더 조립에 CHttpResponseBuilder
// (HttpPacketBuilder.h)를 쓴다. 이 빌더는 AddHeader()에 넘긴 string_view를 Build() 시점까지 그대로
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
		session->SendSimple(500, "Internal Server Error", "text/plain", "Storage not initialized", keepAlive);
		return;
	}

	std::unique_ptr<CFileStream> stream = _storage->OpenStream(relativePath);
	if( stream == nullptr || !stream->IsOpen() )
	{
		session->SendSimple(404, "Not Found", "text/plain", "File not found", keepAlive);
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

	// 헤더를 보내기 전에 위치를 잡는다 — 실패하면 아직 온전한 오류 응답을 보낼 수 있다.
	if( !stream->Seek(bodyStart) )
	{
		LOG_ERROR(_T("CFileDownloadHandler::Handle: Seek 실패 (bodyStart=%lld)"), static_cast<long long>(bodyStart));
		session->SendSimple(500, "Internal Server Error", "text/plain", "Failed to read file", keepAlive);
		return;
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
		// 저장 파일명은 무작위이고 내용이 바뀌지 않으므로 오래 캐시해도 안전하다.
		.AddHeader("Cache-Control", "public, max-age=31536000, immutable")
		.AddHeader("X-Content-Type-Options", "nosniff")
		.AddHeader("Content-Length", contentLengthStr)
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close");
	if( isPartial )
		builder.AddHeader("Content-Range", contentRangeStr);
	builder.SetBody(""); // 바디는 비워서 헤더 블록만 조립 — 실제 바디는 아래서 청크로 직접 전송

	auto [headerData, headerLen] = builder.Build();
	session->SendRaw(headerData, headerLen);

	// 본문은 송신 완료에 맞춰 세션이 청크 단위로 읽어 보낸다(BeginDownload 참고).
	session->BeginDownload(std::move(stream), bodyLength, keepAlive);
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
	session->SendRaw(data, len);
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