
//***************************************************************************
// HttpClientSession.cpp : implementation for the CHttpClientSession class.
//
//***************************************************************************

#include "pch.h"
#include "HttpClientSession.h"

//***************************************************************************
// @brief 소켓 연결이 완료되었을 때 호출되는 이벤트 핸들러입니다.
//***************************************************************************
void CHttpClientSession::OnConnected()
{
	// 기반 클래스(CHttpSessionIocp)의 기본 연결 처리 로직 실행
	CHttpSessionIocp::OnConnected();

	// TODO: 사용자 정의 연결 시점 추가 로직 작성
}

//***************************************************************************
// @brief 소켓 연결이 종료되었을 때 호출되는 이벤트 핸들러입니다.
//***************************************************************************
void CHttpClientSession::OnDisconnected()
{
	// TODO: 사용자 정의 연결 종료 시점 추가 로직 작성

	// 기반 클래스(CHttpSessionIocp)의 기본 종료 처리 로직 실행
	CHttpSessionIocp::OnDisconnected();
}

//***************************************************************************
// @brief 데이터 수신 시 호출되는 이벤트 핸들러입니다.
// @param buffer 수신된 데이터 버퍼
// @param len 수신된 데이터 길이
// @return int32 처리한 바이트 수
//***************************************************************************
int32 CHttpClientSession::OnRecv(BYTE* buffer, int32 len)
{
	// 기반 클래스(CHttpSessionIocp)의 HTTP 데이터 파싱 및 가공 로직 실행
	return CHttpSessionIocp::OnRecv(buffer, len);
}