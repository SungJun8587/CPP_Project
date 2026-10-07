
//***************************************************************************
// HttpServerSession.cpp : implementation of the CHttpServerSession class.
//
//***************************************************************************

#include "pch.h"
#include "HttpServerSession.h"

//***************************************************************************
// @brief CHttpServerSession 생성자
// @param rootDirectory 정적 파일을 서빙할 루트 디렉토리 (절대/상대 경로 모두 가능)
//***************************************************************************
CHttpServerSession::CHttpServerSession(std::string rootDirectory)
    : _rootDirectory(std::move(rootDirectory)), _mimeTypes(DefaultMimeTypes())
{
}

//***************************************************************************
// @brief 확장자별 Content-Type을 추가하거나 기본값을 덮어씁니다.
// @param extension 확장자 (점 포함, 예: ".webp") — 대소문자 무시하고 소문자로 저장
// @param contentType 이 확장자에 매핑할 Content-Type
//***************************************************************************
void CHttpServerSession::SetMimeType(std::string extension, std::string contentType)
{
    for( char& c : extension )
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    _mimeTypes[extension] = std::move(contentType);
}

//***************************************************************************
// @brief 클라이언트 연결 완료 시 호출되어 파서 상태를 초기화합니다.
//***************************************************************************
void CHttpServerSession::OnConnected()
{
    // 1. 새로운 클라이언트 연결 시 이전 HTTP 파싱 상태 초기화
    _parser.Reset();
}

//***************************************************************************
// @brief 수신된 패킷 데이터를 HTTP 파서로 전달하고 처리합니다.
// @param buffer 수신된 바이트 데이터 버퍼
// @param len 수신된 데이터 길이
// @return 처리된 바이트 수 또는 에러(-1)
//***************************************************************************
int32 CHttpServerSession::OnRecv(BYTE* buffer, int32 len)
{
    // 1. 유효한 수신 데이터 크기 검증
    if( len <= 0 )
        return 0;

    // 2. 수신 버퍼 데이터를 CHttpRequestParser에 전달 (Feed)
    HTTP::EParseState state = _parser.Feed(reinterpret_cast<const char*>(buffer), static_cast<size_t>(len));

    // 3. 프로토콜 파싱 에러 처리 (InternalError 사유 전달)
    if( state == HTTP::EParseState::Error )
    {
        // 3-1. 파싱 실패 시 서버 내부 오류 사유로 세션 종료
        Disconnect(Iocp::CloseReason::InternalError);
        return -1;
    }

    // 4. HTTP 요청 1건 완전 완료 시 응답 및 세션 상태 제어
    if( state == HTTP::EParseState::Complete )
    {
        // 4-1. 소비된 바이트 수 먼저 확보
        size_t consumed = _parser.GetLastFeedConsumed();

        // 4-2. 헤더 분석을 통한 Keep-Alive 옵션 여부 확인
        bool keepAlive = _parser.IsKeepAlive();

        // 4-3. 요청받은 URL에 대한 HTTP 응답 생성 및 전송
        ProcessHttpRequest();

        if( keepAlive )
        {
            // Keep-Alive일 경우 파서 상태 초기화 후 수신 지속
            _parser.Reset();
        }
        else
        {
            // Close일 경우 응답 송신 후 연결 종료 처리
            Disconnect(Iocp::CloseReason::ForcedClose);
        }

        // 4-4. 실제로 소비한 바이트 수만 리턴 (파이프라이닝 대응)
        return static_cast<int32>(consumed);
    }

    // 5. 미완성된 패킷일 경우 추가 수신 데이터 대기
    return 0;
}

//***************************************************************************
// @brief 파싱된 HTTP 요청을 처리하고 적절한 응답 전송 함수를 호출합니다.
//***************************************************************************
void CHttpServerSession::ProcessHttpRequest()
{
    // 1. 요청된 URI 경로 가져오기
    std::string requestUri = _parser.GetUri();

    // 2. 쿼리 스트링('?') 제거
    size_t queryPos = requestUri.find('?');
    if( queryPos != std::string::npos )
    {
        requestUri = requestUri.substr(0, queryPos);
    }

    // 3. 루트 경로 요청일 경우 index.html 기본 서빙
    if( requestUri == "/" )
    {
        requestUri = "/index.html";
    }

    // 4. 정적 파일 서빙 처리
    StaticFileResult fileResult = ServeStaticFile(requestUri);

    // 5. 결과에 따른 전송 함수 분기
    if( fileResult.found )
    {
        std::vector<BYTE> binaryBody(fileResult.body.begin(), fileResult.body.end());
        _tstring status = _T("200 OK");
        _tstring contentType = Utf8ToTString(fileResult.contentType);

        SendBinaryResponse(status, contentType, binaryBody);
    }
    else
    {
        _tstring status = (fileResult.statusCode == 403) ? _T("403 Forbidden") : _T("404 Not Found");
        _tstring contentType = _T("text/html; charset=utf-8");
        _tstring errBody = _T("<html><body><h1>") + status + _T("</h1></body></html>");

        SendTextResponse(status, contentType, errBody);
    }
}

//***************************************************************************
// @brief 텍스트 형태(HTML, JSON, XML 등)의 HTTP 응답을 생성 및 전송합니다.
// @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
// @param contentType 응답의 Content-Type 헤더 값
// @param body 전송할 텍스트 본문
//***************************************************************************
void CHttpServerSession::SendTextResponse(const _tstring& status, const _tstring& contentType, const _tstring& body)
{
    // 1. TStringToUtf8 함수를 활용해 UTF-8 변환 후 정확한 바이트 길이를 계산
    std::string utf8Body = TStringToUtf8(body);
    size_t bodyLen = utf8Body.size();

    // 2. Connection 헤더 값 결정
    _tstring connHeader = _parser.IsKeepAlive() ? _T("keep-alive") : _T("close");

    // 3. 표준 HTTP 응답 헤더 조립
    _tstring header = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_wstring(bodyLen) + _T("\r\n") +
        _T("Connection: ") + connHeader + _T("\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n");

    // 4. 헤더 전송
    SendResponseString(header);

    if( utf8Body.empty() )
        return;

    // 5. 4KB 청크 단위 바디 송신
    constexpr uint32 MAX_SEND_CHUNK_SIZE = 4096;
    uint32 offset = 0;
    uint32 remainSize = static_cast<uint32>(utf8Body.size());

    while( remainSize > 0 )
    {
        uint32 sendSize = (std::min)(remainSize, MAX_SEND_CHUNK_SIZE);
        CSendBufferRef sendBuffer = CSendBufferManager::Open(sendSize);
        if( sendBuffer == nullptr )
        {
            Disconnect(Iocp::CloseReason::InternalError);
            return;
        }

        ::memcpy(sendBuffer->Buffer(), utf8Body.data() + offset, sendSize);
        sendBuffer->Close(sendSize);
        Send(sendBuffer->Buffer(), sendBuffer->WriteSize());

        offset += sendSize;
        remainSize -= sendSize;
    }
}

//***************************************************************************
// @brief 바이너리 형태(이미지 파일 등)의 HTTP 응답을 생성 및 분할 전송합니다.
// @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
// @param contentType 응답의 Content-Type 헤더 값
// @param body 전송할 바이너리 데이터 버퍼
//***************************************************************************
void CHttpServerSession::SendBinaryResponse(const _tstring& status, const _tstring& contentType, const std::vector<BYTE>& body)
{
    // 1. Connection 헤더 결정
    _tstring connHeader = _parser.IsKeepAlive() ? _T("keep-alive") : _T("close");

    // 2. 바이너리 데이터용 HTTP 헤더 구성
    _tstring headerStr = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_wstring(body.size()) + _T("\r\n") +
        _T("Connection: ") + connHeader + _T("\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n");

    // 3. 헤더 전송
    SendResponseString(headerStr);

    if( body.empty() )
        return;

    // 4. 청크 루프를 통한 바이너리 바디 송신
    constexpr uint32 MAX_SEND_CHUNK_SIZE = 4096;
    uint32 offset = 0;
    uint32 remainSize = static_cast<uint32>(body.size());

    while( remainSize > 0 )
    {
        uint32 sendSize = (std::min)(remainSize, MAX_SEND_CHUNK_SIZE);
        CSendBufferRef sendBuffer = CSendBufferManager::Open(sendSize);
        if( sendBuffer == nullptr )
        {
            Disconnect(Iocp::CloseReason::InternalError);
            return;
        }

        ::memcpy(sendBuffer->Buffer(), body.data() + offset, sendSize);
        sendBuffer->Close(sendSize);
        Send(sendBuffer->Buffer(), sendBuffer->WriteSize());

        offset += sendSize;
        remainSize -= sendSize;
    }
}

//***************************************************************************
// @brief 문자열 패킷을 UTF-8 인코딩으로 변환하여 분할 전송합니다.
// @param str 전송할 문자열 데이터
//***************************************************************************
void CHttpServerSession::SendResponseString(const _tstring& str)
{
    // 1. EncodingConvert.h의 TStringToUtf8 함수를 호출하여 통합 처리
    std::string utf8Str = TStringToUtf8(str);

    if( utf8Str.empty() )
        return;

    // 2. 4KB 청크 단위 전송
    constexpr uint32 MAX_SEND_CHUNK_SIZE = 4096;
    uint32 offset = 0;
    uint32 remainSize = static_cast<uint32>(utf8Str.size());

    while( remainSize > 0 )
    {
        uint32 sendSize = (std::min)(remainSize, MAX_SEND_CHUNK_SIZE);
        CSendBufferRef sendBuffer = CSendBufferManager::Open(sendSize);
        if( sendBuffer == nullptr )
        {
            Disconnect(Iocp::CloseReason::InternalError);
            return;
        }

        ::memcpy(sendBuffer->Buffer(), utf8Str.data() + offset, sendSize);
        sendBuffer->Close(sendSize);
        Send(sendBuffer->Buffer(), sendBuffer->WriteSize());

        offset += sendSize;
        remainSize -= sendSize;
    }
}

//***************************************************************************
// @brief 요청 경로에 해당하는 파일을 읽어 응답 결과를 만듭니다.
// @param requestPath HTTP 요청 경로 (예: "/data/config.json")
// @return StaticFileResult 성공 시 found=true/statusCode=200과 파일 내용, 실패 시 found=false와 원인에 맞는 statusCode(403/404)
//***************************************************************************
StaticFileResult CHttpServerSession::ServeStaticFile(std::string_view requestPath) const
{
    StaticFileResult result;

    std::filesystem::path resolvedPath;
    if( !ResolveSafePath(requestPath, resolvedPath) )
    {
        result.statusCode = 403;
        return result;
    }

    std::error_code ec;
    if( !std::filesystem::is_regular_file(resolvedPath, ec) || ec )
    {
        result.statusCode = 404;
        return result;
    }

    std::ifstream file(resolvedPath, std::ios::binary);
    if( !file )
    {
        result.statusCode = 404;
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    result.found = true;
    result.statusCode = 200;
    result.body = buffer.str();
    result.contentType = LookupContentType(resolvedPath);
    return result;
}

//***************************************************************************
// @brief 요청 경로를 루트 디렉토리 기준으로 안전하게 정규화합니다 (디렉토리 트래버설 방어).
// @param requestPath HTTP 요청 경로
// @param outPath [OUT] 정규화된 절대 경로
// @return bool 안전하면 true, 루트 디렉토리를 벗어나면 false
//***************************************************************************
bool CHttpServerSession::ResolveSafePath(std::string_view requestPath, std::filesystem::path& outPath) const
{
    std::string decodedPath = HTTP::UrlDecode(requestPath);

    // 1. 선행 '/'를 제거하여 상대 경로 변환 (루트 결합 무시 현상 방지)
    while( !decodedPath.empty() && decodedPath.front() == '/' )
        decodedPath.erase(decodedPath.begin());

    std::error_code ec;
    std::filesystem::path root = std::filesystem::weakly_canonical(_rootDirectory, ec);
    if( ec )
        return false;

    std::filesystem::path combined = std::filesystem::weakly_canonical(root / decodedPath, ec);
    if( ec )
        return false;

    // 2. combined 경로가 root 디렉토리 하위에 위치하는지 검증
    auto rootIt = root.begin();
    auto combinedIt = combined.begin();
    for( ; rootIt != root.end(); ++rootIt, ++combinedIt )
    {
        if( combinedIt == combined.end() || *combinedIt != *rootIt )
            return false;
    }

    outPath = combined;
    return true;
}

//***************************************************************************
// @brief 파일 확장자로 Content-Type을 조회합니다. 매핑에 없으면 범용값을 반환합니다.
// @param path 검증 대상 파일 경로
// @return std::string 결정된 Content-Type 문자열
//***************************************************************************
std::string CHttpServerSession::LookupContentType(const std::filesystem::path& path) const
{
    std::string ext = path.extension().string();
    for( char& c : ext )
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto it = _mimeTypes.find(ext);
    if( it != _mimeTypes.end() )
        return it->second;

    return "application/octet-stream";
}

//***************************************************************************
// @brief 기본 확장자->Content-Type 매핑 테이블을 생성합니다.
// @return std::unordered_map<std::string, std::string> 확장자 및 Content-Type 맵
//***************************************************************************
std::unordered_map<std::string, std::string> CHttpServerSession::DefaultMimeTypes()
{
    return
    {
        { ".html",  "text/html; charset=utf-8" },
        { ".htm",   "text/html; charset=utf-8" },
        { ".css",   "text/css; charset=utf-8" },
        { ".js",    "application/javascript; charset=utf-8" },
        { ".json",  "application/json; charset=utf-8" },
        { ".xml",   "application/xml; charset=utf-8" },
        { ".txt",   "text/plain; charset=utf-8" },
        { ".png",   "image/png" },
        { ".jpg",   "image/jpeg" },
        { ".jpeg",  "image/jpeg" },
        { ".gif",   "image/gif" },
        { ".svg",   "image/svg+xml" },
        { ".ico",   "image/x-icon" },
        { ".pdf",   "application/pdf" },
        { ".woff",  "font/woff" },
        { ".woff2", "font/woff2" },
    };
}