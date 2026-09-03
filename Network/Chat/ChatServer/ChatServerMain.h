
//***************************************************************************
// ChatServerMain.h : interface for the CChatServerMain class.
//
//***************************************************************************

#ifndef UC_CHATSERVER_H
#define UC_CHATSERVER_H

#include <Network/NetworkCommon.h>
#include <Redis/RedisCommon.h>

#include <string>
#include <memory>

//***************************************************************************
// @class CChatServerMain
// @brief IOCP 서버 서비스 + Redis(로그인 상태/서버 하트비트)를 함께 구동하는
//        채팅 서버 파사드.
// @details
// 역할:
//     1. CIocpServerService 구동 (CNetworkFactory를 통해 생성)
//     2. CRedisService 초기화 및 CRedisServerHeartbeat로 서버 생존 신고
//     3. CChatSession으로부터 로그인/로그아웃/브로드캐스트 요청을 위임받아 처리
//
// 소유 순서(Start()에서의 생성 순서, Stop()에서는 역순 정리):
//     _iocpCore/_jobQueue → _redisService → _service(IOCP) → _heartbeat
//***************************************************************************
class CChatServerMain
{
public:
	CChatServerMain() = default;
	~CChatServerMain();

	CChatServerMain(const CChatServerMain&) = delete;
	CChatServerMain& operator=(const CChatServerMain&) = delete;

	//***************************************************************************
	// @brief 채팅 서버를 구동합니다 (IOCP 서비스 시작 + Redis 초기화 + 하트비트 시작).
	// @param bindIp/bindPort 클라이언트 접속을 받을 주소
	// @param redisIp/redisPort/redisPoolSize Redis 접속 정보
	// @param serverType/serverId 하트비트 등록에 쓰일 식별자 (예: "ChatServer", "1")
	// @param maxSessionCount 최대 동시 접속 수
	// @param workerThreadCount IOCP 워커 스레드 개수 (0=자동)
	// @param heartbeatTtlSec/heartbeatIntervalSec 하트비트 TTL/갱신 주기
	// @return 모든 초기화 단계가 성공하면 true
	//***************************************************************************
	bool Start(
		const _tstring& bindIp, uint16 bindPort,
		const std::string& redisIp, uint16 redisPort, int32 redisPoolSize,
		std::string serverType, std::string serverId,
		int32 maxSessionCount = 1000, uint32 workerThreadCount = 0,
		int32 heartbeatTtlSec = 15, int32 heartbeatIntervalSec = 5);

	//***************************************************************************
	// @brief 채팅 서버를 정지합니다. 하트비트 → IOCP 서비스 순으로 정리합니다.
	//***************************************************************************
	void Stop();

public:
	// CChatSession에서 호출하는 콜백들
	void OnUserLogin(const std::string& userId);
	void OnUserLogout(const std::string& userId);
	void Broadcast(const void* data, uint16 size);

private:
	std::string BuildUserKey(const std::string& userId) const;

private:
	CIocpCoreRef				_iocpCore;
	CJobQueueRef				_jobQueue;
	std::unique_ptr<CRedisService>			_redisService;
	CIocpServerServiceRef		_service;
	std::unique_ptr<CRedisServerHeartbeat>	_heartbeat;

	std::string	_serverType;
	std::string	_serverId;
};

#endif // ndef UC_CHATSERVER_H
