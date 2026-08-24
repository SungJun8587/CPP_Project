
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
    if( len <= 0 )
        return 0;

    // 1. 수신 버퍼 파싱
    int32 consumedBytes = _parser.Parse(buffer, static_cast<size_t>(len));

    // 2. 프로토콜 파싱 에러 처리
    if( _parser.GetState() == SimpleHttpParser::State::Error )
    {
        Disconnect(Iocp::CloseReason::ProtocolError);
        return -1;
    }

    // 3. HTTP 요청 1건 완전 완료 시 응답 및 세션 재설정
    if( _parser.GetState() == SimpleHttpParser::State::Complete )
    {
        ProcessHttpRequest();
        _parser.Reset();
        return consumedBytes;
    }

    return 0; // 추가 데이터 수신 대기
}

//***************************************************************************
// @brief 파싱된 HTTP 요청을 바탕으로 API 또는 정적 파일 응답을 처리
//***************************************************************************
void CHttpServerSession::ProcessHttpRequest()
{
    const _tstring& method = _parser.GetMethod();
    const _tstring& url = _parser.GetUrl();
    const _tstring& body = _parser.GetBody();

    // 1. 크롬 자동 파비콘 요청 예외 처리
    if( url == _T("/favicon.ico") )
    {
        _tstring response = _T("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        SendResponseString(response);
        return;
    }

    // 2. 동적 API 테스트 경로 처리
    if( url == _T("/api/json") )
    {
        _tstring jsonBody = _T("{\"status\":\"success\", \"type\":\"JSON\", \"message\":\"C++ IOCP Server JSON Response\"}");
        SendTextResponse(_T("200 OK"), _T("application/json; charset=utf-8"), jsonBody);
        return;
    }
    else if( url == _T("/api/xml") )
    {
        _tstring xmlBody = _T("<?xml version=\"1.0\" encoding=\"UTF-8\"?><response><status>success</status><type>XML</type><message>C++ IOCP Server XML Response</message></response>");
        SendTextResponse(_T("200 OK"), _T("text/xml; charset=utf-8"), xmlBody);
        return;
    }

    // 3. 정적 파일 로딩 처리 (HTML, 이미지, 기타 파일)
    _tstring filePath = _T("wwwroot") + url;
    if( url == _T("/") )
    {
        filePath = _T("wwwroot/index.html");
    }

    // 확장자 기반 Content-Type 자동으로 구하기
    _tstring contentType = GetContentTypeByExtension(filePath);

    // 바이너리 파일 읽기 시도
    std::vector<BYTE> fileBuffer;
    if( LoadBinaryFile(filePath, fileBuffer) )
    {
        SendBinaryResponse(_T("200 OK"), contentType, fileBuffer);
    }
    else
    {
        // 파일이 없을 경우 404 응답
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
    static const std::map<_tstring, _tstring> mimeMap = {
        { _T(".html"), _T("text/html; charset=utf-8") },
        { _T(".htm"),  _T("text/html; charset=utf-8") },
        { _T(".css"),  _T("text/css; charset=utf-8") },
        { _T(".js"),   _T("application/javascript; charset=utf-8") },
        { _T(".json"), _T("application/json; charset=utf-8") },
        { _T(".xml"),  _T("text/xml; charset=utf-8") },
        { _T(".png"),  _T("image/png") },
        { _T(".jpg"),  _T("image/jpeg") },
        { _T(".jpeg"), _T("image/jpeg") },
        { _T(".gif"),  _T("image/gif") },
        { _T(".ico"),  _T("image/x-icon") },
        { _T(".svg"),  _T("image/svg+xml") },
        { _T(".pdf"),  _T("application/pdf") },
        { _T(".txt"),  _T("text/plain; charset=utf-8") }
    };

    size_t dotIdx = filePath.rfind(_T('.'));
    if( dotIdx != _tstring::npos )
    {
        _tstring ext = filePath.substr(dotIdx);
        // 소문자 변환
        std::transform(ext.begin(), ext.end(), ext.begin(), [](TCHAR c) {
            return static_cast<TCHAR>(_totlower(c));
            });

        auto it = mimeMap.find(ext);
        if( it != mimeMap.end() )
            return it->second;
    }

    // 알 수 없는 파일은 일반 바이너리 스트림 처리
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
#ifdef UNICODE
    int bodyLen = WideCharToMultiByte(CP_UTF8, 0, body.c_str(), static_cast<int>(body.size()), NULL, 0, NULL, NULL);
#else
    int bodyLen = static_cast<int>(body.size());
#endif

    _tstring header = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_string(bodyLen) + _T("\r\n") +
        _T("Connection: close\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n") + body;

    SendResponseString(header);
}

//***************************************************************************
// @brief 바이너리 형태(이미지 파일 등)의 HTTP 응답을 생성 및 전송
// @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
// @param contentType 응답의 Content-Type 헤더 값
// @param body 전송할 바이너리 데이터 버퍼
//***************************************************************************
void CHttpServerSession::SendBinaryResponse(const _tstring& status, const _tstring& contentType, const std::vector<BYTE>& body)
{
    _tstring headerStr = _T("HTTP/1.1 ") + status + _T("\r\n") +
        _T("Content-Type: ") + contentType + _T("\r\n") +
        _T("Content-Length: ") + std::to_string(body.size()) + _T("\r\n") +
        _T("Connection: close\r\n") +
        _T("Access-Control-Allow-Origin: *\r\n\r\n");

    std::string headerAnsi;
#ifdef UNICODE
    int hLen = WideCharToMultiByte(CP_UTF8, 0, headerStr.c_str(), static_cast<int>(headerStr.size()), NULL, 0, NULL, NULL);
    headerAnsi.resize(hLen);
    WideCharToMultiByte(CP_UTF8, 0, headerStr.c_str(), static_cast<int>(headerStr.size()), &headerAnsi[0], hLen, NULL, NULL);
#else
    headerAnsi = headerStr;
#endif

    // 헤더 + 바이너리 바디 결합 후 송신
    std::vector<BYTE> packet(headerAnsi.begin(), headerAnsi.end());
    packet.insert(packet.end(), body.begin(), body.end());

    Send(packet.data(), static_cast<uint16_t>(packet.size()));
}

//***************************************************************************
// @brief 문자열 패킷을 UTF-8 인코딩으로 변환하여 비동기 전송
// @param str 전송할 문자열 데이터
//***************************************************************************
void CHttpServerSession::SendResponseString(const _tstring& str)
{
#ifdef UNICODE
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
    std::string utf8Str(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &utf8Str[0], utf8Len, NULL, NULL);
    Send(reinterpret_cast<const BYTE*>(utf8Str.data()), static_cast<uint16_t>(utf8Str.size()));
#else
    Send(reinterpret_cast<const BYTE*>(str.data()), static_cast<uint16_t>(str.size()));
#endif
}

//***************************************************************************
// @brief 지정된 경로의 바이너리 파일을 읽어 버퍼에 저장
// @param filePath 읽어올 파일의 상대 또는 절대 경로
// @param outBuffer 읽어온 데이터를 저장할 BYTE 벡터
// @return 파일 로딩 성공 여부 (성공 시 true, 실패 시 false)
//***************************************************************************
bool CHttpServerSession::LoadBinaryFile(const _tstring& filePath, std::vector<BYTE>& outBuffer)
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if( !file.is_open() )
        return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    outBuffer.resize(static_cast<size_t>(size));
    if( file.read(reinterpret_cast<char*>(outBuffer.data()), size) )
        return true;

    return false;
}