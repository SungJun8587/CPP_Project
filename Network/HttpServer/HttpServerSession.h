
//***************************************************************************
// HttpServerSession.h : interface for the CHttpServerSession class.
//
//***************************************************************************

#ifndef __HTTPSERVERSESSION_H__
#define __HTTPSERVERSESSION_H__

#ifndef	__IOCPSESSION_H__
#include <Network/IOCP/IocpSession.h>
#endif

#ifndef	__HTTPREQUESTPARSER_H__
#include <Network/HTTP/HttpRequestParser.h>
#endif

//***************************************************************************
// @brief HTTP 서버 세션을 관리하는 클래스
// @details IOCP 세션을 상속받아 HTTP 요청을 파싱하고, 정적 파일(HTML, 이미지 등) 및 API 응답을 클라이언트에게 비동기로 전송합니다.
//***************************************************************************
class CHttpServerSession : public CIocpSession
{
public:
    CHttpServerSession() = default;
    virtual ~CHttpServerSession() = default;

protected:
    //***************************************************************************
    // @brief 클라이언트 연결 완료 시 호출되어 파서 상태 초기화
    //***************************************************************************
    virtual void OnConnected() override;

    //***************************************************************************
    // @brief 수신된 패킷 데이터를 HTTP 파서로 전달하고 처리
    // @param buffer 수신된 바이트 데이터 버퍼
    // @param len 수신된 데이터 길이
    // @return 처리된 바이트 수 또는 에러(-1)
    //***************************************************************************
    virtual int32 OnRecv(BYTE* buffer, int32 len) override;

private:
    //***************************************************************************
    // @brief 파싱된 HTTP 요청을 바탕으로 API 또는 정적 파일 응답을 처리
    //***************************************************************************
    void ProcessHttpRequest();

    //***************************************************************************
    // @brief 파일 경로에서 확장자를 추출하여 대응하는 MIME Content-Type을 반환
    // @param filePath 대상 파일 경로
    // @return Content-Type 문자열
    //***************************************************************************
    _tstring GetContentTypeByExtension(const _tstring& filePath);

    //***************************************************************************
    // @brief 텍스트 형태(HTML, JSON, XML 등)의 HTTP 응답을 생성 및 전송
    // @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
    // @param contentType 응답의 Content-Type 헤더 값
    // @param body 전송할 텍스트 본문
    //***************************************************************************
    void SendTextResponse(const _tstring& status, const _tstring& contentType, const _tstring& body);

    //***************************************************************************
    // @brief 바이너리 형태(이미지 파일 등)의 HTTP 응답을 생성 및 전송
    // @param status HTTP 상태 코드 및 메시지 (예: "200 OK")
    // @param contentType 응답의 Content-Type 헤더 값
    // @param body 전송할 바이너리 데이터 버퍼
    //***************************************************************************
    void SendBinaryResponse(const _tstring& status, const _tstring& contentType, const std::vector<BYTE>& body);

    //***************************************************************************
    // @brief 문자열 패킷을 UTF-8 인코딩으로 변환하여 비동기 전송
    // @param str 전송할 문자열 데이터
    //***************************************************************************
    void SendResponseString(const _tstring& str);

    //***************************************************************************
    // @brief 지정된 경로의 바이너리 파일을 읽어 버퍼에 저장
    // @param filePath 읽어올 파일의 상대 또는 절대 경로
    // @param outBuffer 읽어온 데이터를 저장할 BYTE 벡터
    // @return 파일 로딩 성공 여부 (성공 시 true, 실패 시 false)
    //***************************************************************************
    bool LoadBinaryFile(const _tstring& filePath, std::vector<BYTE>& outBuffer);

private:
    CHttpRequestParser _parser; // HTTP 요청 파싱을 담당하는 파서 객체
};

#endif // ndef __HTTPSERVERSESSION_H__