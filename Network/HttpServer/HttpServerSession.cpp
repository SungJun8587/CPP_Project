
//***************************************************************************
// HttpServerSession.cpp : implementation of the CHttpServerSession class.
//
//***************************************************************************

#include "pch.h"
#include "HttpServerSession.h"

//***************************************************************************
// @brief 클라이언트 연결 완료 시 호출되어 파서 상태 초기화
//***************************************************************************
void CHttpServerSession::OnConnected()
{
    // 1. 새로운 클라이언트 연결 시 이전 HTTP 파싱 상태 초기화
    _parser.Reset();
}

//***************************************************************************
// @brief 수신된 패킷 데이터를 HTTP 파서로 전달하고 처리
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
        // 4-1. 이번 Feed() 호출에서 실제로 소비된 바이트 수를 먼저 확보 (Reset() 전에 저장해야 함).
        //      파이프라이닝된 다음 요청의 선두 바이트가 같은 수신 버퍼에 섞여 들어온 경우
        //      consumed는 len보다 작을 수 있다 — 이 경우 len 전체를 처리한 것으로 리턴하면
        //      뒤에 섞여 있던 다음 요청의 바이트가 그대로 유실된다.
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
            // Close일 경우 응답 송신 후 연결 종료 처리 (ForcedClose 사유 전달)
            Disconnect(Iocp::CloseReason::ForcedClose);
        }

        // 4-4. 실제로 소비한 바이트 수만 리턴. 나머지(len - consumed)는 세션 수신 버퍼에 남아
        //      다음 OnRecv 호출 시 이어서 파싱된다(파이프라이닝 대응).
        return static_cast<int32>(consumed);
    }

    // 5. 미완성된 패킷일 경우 추가 수신 데이터 대기
    return 0; // 추가 데이터 수신 대기
}

//***************************************************************************
// @brief 파싱된 HTTP 요청을 바탕으로 API 또는 정적 파일 응답을 처리
//***************************************************************************
void CHttpServerSession::ProcessHttpRequest()
{
    // char 기반의 URI/Method/Body를 TCHAR(_tstring) 형태로 변환
    std::string rawUri = _parser.GetUri();
    std::string rawMethod = _parser.GetMethod();

    // 1. 빌드 환경(Unicode/ANSI)에 상응하는 문자열로 URI 변환
#ifdef UNICODE
    int uriLen = MultiByteToWideChar(CP_UTF8, 0, rawUri.c_str(), static_cast<int>(rawUri.size()), NULL, 0);
    _tstring url(uriLen, _T('\0'));
    MultiByteToWideChar(CP_UTF8, 0, rawUri.c_str(), static_cast<int>(rawUri.size()), &url[0], uriLen);
#else
    _tstring url = rawUri;
#endif

    // 2. URL 쿼리 스트링('?') 제거하여 순수 파일 경로 추출
    size_t queryPos = url.find(_T('?'));
    if( queryPos != _tstring::npos )
    {
        url = url.substr(0, queryPos); // ? 이전의 순수 경로만 남김
    }

    // 3. Path Traversal 방어: 상위 디렉터리 이동('..') 시도 차단
    if( url.find(_T("..")) != _tstring::npos )
    {
        _tstring response = _T("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        SendResponseString(response);
        Disconnect(Iocp::CloseReason::ForcedClose);
        return;
    }

    // 5. 크롬 자동 파비콘 요청 예외 처리
    if( url == _T("/favicon.ico") )
    {
        // 5-1. 파비콘 미지원 시 404 빠른 응답 처리
        _tstring response = _T("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        SendResponseString(response);
        return;
    }

    // 6. 동적 API 테스트 경로 처리
    if( url == _T("/api/json") )
    {
        // 6-1. JSON API 요청 매핑 및 응답
        _tstring jsonBody = _T("{\"status\":\"success\", \"type\":\"JSON\", \"message\":\"C++ IOCP Server JSON Response\"}");
        SendTextResponse(_T("200 OK"), _T("application/json; charset=utf-8"), jsonBody);
        return;
    }
    else if( url == _T("/api/xml") )
    {
        // 6-2. XML API 요청 매핑 및 응답
        _tstring xmlBody = _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><status>success</status><type>XML</type><message>C++ IOCP Server XML Response</message></response>");
        SendTextResponse(_T("200 OK"), _T("text/xml; charset=utf-8"), xmlBody);
        return;
    }

    // 7. 정적 파일 로딩 처리 (HTML, 이미지, 기타 파일)
    // 7-1. 기본 정적 파일 디렉터리 경로 설정 및 루트 요청 처리
    _tstring filePath = _T("Statics") + url;
    if( url == _T("/") )
    {
        filePath = _T("Statics/index.html");
    }

    // 8. 확장자 기반 Content-Type 자동으로 구하기
    _tstring contentType = GetContentTypeByExtension(filePath);

    // 9. 바이너리 파일 읽기 시도 및 결과 분기
    std::vector<BYTE> fileBuffer;
    if( LoadBinaryFile(filePath, fileBuffer) )
    {
        // 9-1. 파일 로드 성공 시 200 OK 전송
        SendBinaryResponse(_T("200 OK"), contentType, fileBuffer);
    }
    else
    {
        // 9-2. 파일 미존재 시 404 Not Found 전송
        _tstring notFoundBody = _T("<html><body><h1>404 File Not Found</h1></body></html>");
        SendTextResponse(_T("404 Not Found"), _T("text/html; charset=utf-8"), notFoundBody);
    }
}

//***************************************************************************
// @brief 파일 경로에서 확장자를 추출하여 대응하는 MIME Content-Type을 반환
// @param filePath 대상 파일 경로
// @return Content-Type 문자열
//***************************************************************************
_tstring CHttpServerSession::GetContentTypeByExtension(const _tstring& filePath)
{
    // 1. 파일 경로에서 확장자 위치 검색
    size_t dotIdx = filePath.rfind(_T('.'));
    if( dotIdx != _tstring::npos )
    {
        // 1-1. 확장자 추출 및 소문자 정규화
        _tstring ext = filePath.substr(dotIdx);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](TCHAR c) {
            return static_cast<TCHAR>(_totlower(c));
            });

        // 1-2. static map 대신 단순 비교 및 조건문으로 완전 대체 (메모리 동적 할당 0개)
        if( ext == _T(".html") || ext == _T(".htm") ) return _T("text/html; charset=utf-8");
        if( ext == _T(".css") )                       return _T("text/css; charset=utf-8");
        if( ext == _T(".js") )                        return _T("application/javascript; charset=utf-8");
        if( ext == _T(".json") )                      return _T("application/json; charset=utf-8");
        if( ext == _T(".xml") )                       return _T("text/xml; charset=utf-8");
        if( ext == _T(".png") )                       return _T("image/png");
        if( ext == _T(".jpg") || ext == _T(".jpeg") ) return _T("image/jpeg");
        if( ext == _T(".gif") )                       return _T("image/gif");
        if( ext == _T(".ico") )                       return _T("image/x-icon");
        if( ext == _T(".svg") )                       return _T("image/svg+xml");
        if( ext == _T(".pdf") )                       return _T("application/pdf");
        if( ext == _T(".txt") )                       return _T("text/plain; charset=utf-8");
    }

    // 2. 일치하는 확장자가 없는 경우 기본 바이너리 타입 반환
    return _T("application/octet-stream");
}

//***************************************************************************
// @brief 텍스트 형태(HTML, JSON, XML 등)의 HTTP 응답을 생성 및 전송
// @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
// @param contentType 응답의 Content-Type 헤더 값
// @param body 전송할 텍스트 본문
//***************************************************************************
void CHttpServerSession::SendTextResponse(const _tstring& status, const _tstring& contentType, const _tstring& body)
{
    // 1. UTF-8 인코딩 시 변환될 실제 바이트 길이 계산
#ifdef UNICODE
    int bodyLen = WideCharToMultiByte(CP_UTF8, 0, body.c_str(), static_cast<int>(body.size()), NULL, 0, NULL, NULL);
#else
    int bodyLen = static_cast<int>(body.size());
#endif

    // 2. Connection 헤더 값 결정
    _tstring connHeader = _parser.IsKeepAlive() ? _T("keep-alive") : _T("close");

    // 3. 표준 HTTP 응답 패킷(헤더 + 바디) 조립
    _tstring header = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_wstring(bodyLen) + _T("\r\n") +
        _T("Connection: ") + connHeader + _T("\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n") + body;

    // 4. 문자열 응답 전송 실행
    SendResponseString(header);
}

//***************************************************************************
// @brief 바이너리 형태(이미지 파일 등)의 HTTP 응답을 생성 및 분할 전송
// @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
// @param contentType 응답의 Content-Type 헤더 값
// @param body 전송할 바이너리 데이터 버퍼
//***************************************************************************
void CHttpServerSession::SendBinaryResponse(const _tstring& status, const _tstring& contentType, const std::vector<BYTE>& body)
{
    // 1. Connection 헤더 결정
    _tstring connHeader = _parser.IsKeepAlive() ? _T("keep-alive") : _T("close");

    // 1-1. 바이너리 데이터용 HTTP 헤더 패킷 구성
    _tstring headerStr = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_wstring(body.size()) + _T("\r\n") +
        _T("Connection: ") + connHeader + _T("\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n");

    // 1-2. 헤더선 전송 완료
    SendResponseString(headerStr);

    if( body.empty() )
        return;

    // 2. 바디 데이터 분할 전송 (SEND_BUFFER_CHUNK_SIZE 초과 방지)
    // 2-1. 소켓 버퍼 넘침 방지를 위한 청크 단위 전송 설정 (4KB)
    constexpr uint32 MAX_SEND_CHUNK_SIZE = 4096;
    uint32 offset = 0;
    uint32 remainSize = static_cast<uint32>(body.size());

    // 2-2. 청크 루프를 통한 바이너리 바디 분할 비동기 송신
    while( remainSize > 0 )
    {
        uint32 sendSize = (std::min)(remainSize, MAX_SEND_CHUNK_SIZE);
        CSendBufferRef sendBuffer = CSendBufferManager::Open(sendSize);
        if( sendBuffer == nullptr )
        {
            // 송신 버퍼 확보 실패: 이미 보낸 바이트만큼과 Content-Length가 어긋나므로
            // 남은 청크를 건너뛰지 않고 연결을 종료해 응답 손상을 막는다.
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
// @brief 문자열 패킷을 UTF-8 인코딩으로 변환하여 분할 전송
// @param str 전송할 문자열 데이터
//***************************************************************************
void CHttpServerSession::SendResponseString(const _tstring& str)
{
    // 1. 문자열 데이터를 네트워크 표준 인코딩인 UTF-8로 변환
    std::string utf8Str;
#ifdef UNICODE
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    utf8Str.resize(utf8Len);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &utf8Str[0], utf8Len, NULL, NULL);
#else
    utf8Str = str;
#endif

    if( utf8Str.empty() )
        return;

    // 2. 전송할 데이터를 안전한 크기(4KB) 청크 단위로 분할하여 세션 버퍼에 할당 및 송신
    constexpr uint32 MAX_SEND_CHUNK_SIZE = 4096;
    uint32 offset = 0;
    uint32 remainSize = static_cast<uint32>(utf8Str.size());

    while( remainSize > 0 )
    {
        uint32 sendSize = (std::min)(remainSize, MAX_SEND_CHUNK_SIZE);
        CSendBufferRef sendBuffer = CSendBufferManager::Open(sendSize);
        if( sendBuffer == nullptr )
        {
            // 송신 버퍼 확보 실패: 이미 보낸 바이트만큼과 Content-Length가 어긋나므로
            // 남은 청크를 건너뛰지 않고 연결을 종료해 응답 손상을 막는다.
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
// @brief 지정된 경로의 바이너리 파일을 읽어 버퍼에 저장
// @param filePath 읽어올 파일의 상대 또는 절대 경로
// @param outBuffer 읽어온 데이터를 저장할 BYTE 벡터
// @return 파일 로딩 성공 여부 (성공 시 true, 실패 시 false)
//***************************************************************************
bool CHttpServerSession::LoadBinaryFile(const _tstring& filePath, std::vector<BYTE>& outBuffer)
{
    // 0. 정적 파일 응답 최대 크기 상한 (워커 스레드 블로킹/메모리 스파이크 방지용)
    constexpr std::streamsize MAX_STATIC_FILE_SIZE = 64LL * 1024 * 1024; // 64MB

    // 1. 바이너리 읽기 및 파일 끝 지점 모드로 오픈
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if( !file.is_open() )
        return false;

    // 2. 전체 파일 크기 확인 (tellg() 실패 및 상한 초과 방어)
    std::streamsize size = file.tellg();
    if( size < 0 || size > MAX_STATIC_FILE_SIZE )
        return false;

    file.seekg(0, std::ios::beg);
    if( !file )
        return false;

    // 3. 빈 파일은 read() 없이 바로 성공 처리 (일부 구현에서 read(ptr,0)이 failbit 설정 가능)
    if( size == 0 )
    {
        outBuffer.clear();
        return true;
    }

    // 4. 버퍼 메모리 할당 후 파일 내용 전체 읽기
    outBuffer.resize(static_cast<size_t>(size));
    if( file.read(reinterpret_cast<char*>(outBuffer.data()), size) )
        return true;

    outBuffer.clear();
    return false;
}