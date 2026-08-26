
//***************************************************************************
// HttpClientSession.h : interface for the CHttpClientSession class.
//
//***************************************************************************

#ifndef __HTTPCLIENTSESSION_H__
#define __HTTPCLIENTSESSION_H__

#ifndef __HTTPSESSIONIOCP_H__
#include <Network/HTTP/HttpSessionIocp.h>
#endif

class CHttpClientSession;
using CHttpClientSessionRef = std::shared_ptr<CHttpClientSession>;

//***************************************************************************
// @class CHttpClientSession
// @brief CHttpSessionIocp를 상속받아 사용자가 커스텀 로직 및 이벤트를 확장할 수 있는 세션 클래스
//***************************************************************************
class CHttpClientSession : public CHttpSessionIocp
{
public:
	CHttpClientSession() = default;
	virtual ~CHttpClientSession() = default;

protected:
	//***************************************************************************
	// @brief 소켓 연결이 완료되었을 때 호출되는 이벤트 핸들러입니다.
	//***************************************************************************
	void OnConnected() override;

	//***************************************************************************
	// @brief 소켓 연결이 종료되었을 때 호출되는 이벤트 핸들러입니다.
	//***************************************************************************
	void OnDisconnected() override;

	//***************************************************************************
	// @brief 데이터 수신 시 호출되는 이벤트 핸들러입니다.
	// @param buffer 수신된 데이터 버퍼
	// @param len 수신된 데이터 길이
	// @return int32 처리한 바이트 수
	//***************************************************************************
	int32 OnRecv(BYTE* buffer, int32 len) override;

private:
	uint64_t _sessionCustomId = 0; // 사용자 정의 세션 ID 예시
};

#endif // ndef __HTTPCLIENTSESSION_H__