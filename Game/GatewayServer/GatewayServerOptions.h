
//***************************************************************************
// GatewayServerOptions.h : CGatewayServerMain::Start()에 넘기는 구동 옵션
//
//***************************************************************************

#ifndef UC_GATEWAYSERVEROPTIONS_H
#define UC_GATEWAYSERVEROPTIONS_H

#include <Network/NetworkCommon.h>
#include "../GameCommon/ZoneCommon/ZonePacketTypes.h"

//***************************************************************************
// @struct SGatewayOptions
// @brief Gateway 서버 설정값.
// @detail 존 서버용 포트(zonePort)는 내부망 전용이다. 현재 존 서버 인증이 없으므로
//         기본 바인딩 주소를 로컬(127.0.0.1)로 두며, 다른 호스트의 존 서버를 받으려면
//         내부망 인터페이스로만 바인딩하고 방화벽으로 막아야 한다.
//         설정 파일 값은 CGatewayServerConfig::ToOptions()가 이 구조체로 옮겨 담는다.
//***************************************************************************
struct SGatewayOptions
{
	// ── 클라이언트 접속 ──────────────────────────────────
	_tstring	clientBindIp = _T("0.0.0.0");
	uint16		clientPort = 7000;
	int32		maxClientSessions = 5000;
	uint32		clientWorkerCount = 0;				// 0 = 자동

	// ── 존 서버 접속 (내부망 전용) ───────────────────────
	_tstring	zoneBindIp = _T("127.0.0.1");
	uint16		zonePort = 7100;
	int32		maxZoneSessions = 64;
	uint32		zoneWorkerCount = 2;

	// ── 존 서버 생존 감시 ────────────────────────────────
	uint32		heartbeatIntervalMs = kDefaultHeartbeatIntervalMs;	// 존 서버에 지시하는 하트비트 주기
	uint32		heartbeatTimeoutMs = kDefaultHeartbeatTimeoutMs;	// 이 시간 동안 하트비트가 없으면 연결을 끊음
};

#endif // ndef UC_GATEWAYSERVEROPTIONS_H