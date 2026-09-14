
//***************************************************************************
// FileServerSession.h : interface for the CFileServerSession class.
//
//***************************************************************************

#ifndef UC_FILESERVERSESSION_H
#define UC_FILESERVERSESSION_H

#include <Network/IOCP/IocpSession.h>
#include <Network/HTTP/HttpRequestParser.h>

#include <string>

class CFileServerMain;

//***************************************************************************
// @class CFileServerSession
// @brief CIocpSession을 상속받는 파일 서버 전용 HTTP 세션.
// @details 채팅 서버(CChatSession)와 달리 고정 크기 패킷 프레이밍이 아니라
// CHttpRequestParser로 증분 파싱한다 — OnRecv()가 들어온 바이트를 그대로
// Feed()에 흘려보내고, Complete가 되면 CFileServerMain에 라우팅을 위임한
// 뒤 Reset()해서 같은 연결(keep-alive)에서 다음 요청을 받을 준비를 한다.
//***************************************************************************
class CFileServerSession : public CIocpSession
{
public:
	explicit CFileServerSession(CFileServerMain* server);
	virtual ~CFileServerSession() = default;

protected:
	// CIocpSession의 상위 콘텐츠 레이어 훅 오버라이드
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

public:
	//***************************************************************************
	// @brief 완성된 HTTP 응답 문자열(상태줄+헤더+바디)을 그대로 전송합니다.
	//***************************************************************************
	void SendRaw(const std::string& data);

private:
	CFileServerMain* _server = nullptr;
	CHttpRequestParser	_parser;
};

#endif // ndef UC_FILESERVERSESSION_H