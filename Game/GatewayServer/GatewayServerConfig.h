
//***************************************************************************
// GatewayServerConfig.h : interface for the CGatewayServerConfig class.
//
//***************************************************************************

#ifndef GATEWAYSERVERCONFIG_H
#define GATEWAYSERVERCONFIG_H

#include <ServerConfig.h>
#include "GatewayServerOptions.h"

//***************************************************************************
// @class CGatewayServerConfig
// @brief 게이트웨이 서버 설정 클래스. JSON 설정 파일을 읽어 SGatewayOptions로 변환한다.
// @details
// 베이스(CServerConfig)의 필드는 클라이언트 접속 리스너에 대응한다.
//     Name            -> 서버 이름
//     IP / Port       -> 클라이언트 접속 바인딩 주소/포트
//     MaxSessionCount -> 최대 클라이언트 세션 수
//     WorkerThreadCnt -> 클라이언트 서비스 워커 수 (0 = 자동)
// 존 서버 접속 리스너와 하트비트는 이 클래스 고유 필드다.
//     ZoneIP / ZonePort / ZoneMaxSessionCount / ZoneWorkerThreadCnt
//     HeartbeatIntervalMs / HeartbeatTimeoutMs
// ServiceName/DisplayName/GroupId/ChannelId/KeepAliveSec/ServerNode/DBNode/RedisNode는
// 게이트웨이에 해당 개념이 없으므로 읽지 않는다. 키가 없으면 SGatewayOptions의 기본값을 유지한다.
//***************************************************************************
class CGatewayServerConfig : public CServerConfig
{
public:
	CGatewayServerConfig();
	virtual ~CGatewayServerConfig() {}

	bool Init(const TCHAR* tszConfigPath);
	bool Save(const TCHAR* tszConfigPath) const;
	void PrintServerSettingInfo() const;

	//***************************************************************************
	// @brief 로드된 설정을 CGatewayServerMain::Start()에 넘길 옵션으로 변환합니다.
	//***************************************************************************
	void ToOptions(SGatewayOptions& options) const;

private:
	TCHAR		_tszZoneIP[HOSTNAME_STRLEN];	// 존 서버 접속 바인딩 주소
	uint16		_nZonePort;						// 존 서버 접속 포트
	int32		_nZoneMaxSessionCount;			// 최대 존 서버 세션 수
	int32		_nZoneWorkerThreadCnt;			// 존 서버 서비스 워커 수
	uint32		_nHeartbeatIntervalMs;			// 존 서버에 지시하는 하트비트 주기
	uint32		_nHeartbeatTimeoutMs;			// 이 시간 동안 하트비트가 없으면 연결을 끊음
};

#endif // ndef GATEWAYSERVERCONFIG_H