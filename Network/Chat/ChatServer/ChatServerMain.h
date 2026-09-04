
//***************************************************************************
// ChatServerMain.h : interface for the CChatServerMain class.
//
//***************************************************************************

#ifndef UC_CHATSERVERMAIN_H
#define UC_CHATSERVERMAIN_H

#include <ServerConnectInfo.h>
#include <Network/NetworkCommon.h>
#include <Redis/RedisService.h>
#include "Redis/RedisServerHeartbeat.h"
#include <DB/OdbcAsyncSrv.h>
#include <Crypto/CryptoUtil.h>

#include "AccountDBHandler.h"
#include "ChatSession.h"
#include "DBSignupRequest.h"

#include <string>
#include <memory>
#include <functional>
#include <array>

class CChatSession;

//***************************************************************************
// @class CChatServerMain
// @brief IOCP 서버 서비스 + Redis(로그인 상태/서버 하트비트) + DB(회원가입/재접속)를
//        함께 구동하는 채팅 서버 파사드.
// @details
// 역할:
//     1. CIocpServerService 구동 (CNetworkFactory를 통해 생성)
//     2. CRedisService 초기화 및 CRedisServerHeartbeat로 서버 생존 신고
//     3. COdbcAsyncSrv 초기화 + CAccountDBHandler 등록(회원가입/재접속 토큰 검증)
//     4. CChatSession으로부터 로그인/로그아웃/브로드캐스트 요청을 위임받아 처리
//
// 소유 순서(Start()에서의 생성 순서, Stop()에서는 역순 정리):
//     _iocpCore/_jobQueue → _redisService → COdbcAsyncSrv(싱글턴, 이 클래스가
//     소유하지 않음) → _service(IOCP) → _heartbeat
//***************************************************************************
class CChatServerMain
{
public:
	CChatServerMain() = default;
	~CChatServerMain();

	CChatServerMain(const CChatServerMain&) = delete;
	CChatServerMain& operator=(const CChatServerMain&) = delete;

	//***************************************************************************
	// @brief 채팅 서버를 구동합니다 (IOCP 서비스 시작 + Redis 초기화 + 하트비트 시작 + DB 초기화).
	// @param bindIp/bindPort 클라이언트 접속을 받을 주소
	// @param redisNodeVec Redis 노드 목록 — CRedisService::Init()에 그대로 전달(단일 서버면 노드 하나짜리 목록)
	// @param redisPoolSize 노드별 Redis 커넥션 풀 크기
	// @param dbNodeVec 회원 DB 접속 정보(ODBC) — COdbcAsyncSrv::StartService()에 그대로 전달
	// @param dbMaxThreadCnt DB 비동기 워커 스레드 수 (0=자동 — [가정] StartService 내부 정책)
	// @param serverType/serverId 하트비트 등록에 쓰일 식별자 (예: "ChatServer", "1")
	// @param maxSessionCount 최대 동시 접속 수
	// @param workerThreadCount IOCP 워커 스레드 개수 (0=자동)
	// @param heartbeatTtlSec/heartbeatIntervalSec 하트비트 TTL/갱신 주기
	// @return 모든 초기화 단계가 성공하면 true
	//***************************************************************************
	bool Start(
		const _tstring& bindIp, uint16 bindPort,
		CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
		CVector<CDBNode> dbNodeVec, int32 dbMaxThreadCnt,
		std::string serverType, std::string serverId,
		int32 maxSessionCount = 1000, uint32 workerThreadCount = 0,
		int32 heartbeatTtlSec = 15, int32 heartbeatIntervalSec = 5);

	//***************************************************************************
	// @brief 채팅 서버를 정지합니다. 하트비트 → IOCP 서비스 순으로 정리합니다.
	// @details COdbcAsyncSrv는 싱글턴이라 이 클래스가 정지시키지 않습니다
	//          (다른 서버 모듈이 같이 쓰고 있을 수 있으므로 소유권 밖).
	//***************************************************************************
	void Stop();

public:
	// CChatSession/핸들러에서 호출하는 콜백들
	void OnUserLogin(const std::string& userId);
	void OnUserLogout(const std::string& userId);
	void Broadcast(const void* data, uint16 size);

	//***************************************************************************
	// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true)을
	//        DB 비동기 워커에 요청합니다.
	// @param session 요청을 보낸 세션 (완료 시 콜백에서 weak_ptr로 안전하게 재확인)
	// @param nickname 요청된 닉네임 (형식 검증은 CAccountDBHandler가 다시 한번 수행)
	// @param hasToken true면 token으로 재접속 검증, false면 신규 가입 시도
	// @param token 재접속 토큰 원문(hasToken==false면 무시됨)
	// @param onComplete DB 워커 스레드에서 호출되는 완료 콜백(내부적으로 JobQueue로
	//        이관됨) — 세션/IOCP 객체를 건드리는 코드는 CChatSession의 public API로만
	//        수행할 것.
	//***************************************************************************
	void RequestSignup(
		std::shared_ptr<CChatSession> session,
		const std::string& nickname,
		bool hasToken,
		const std::array<BYTE, kTokenBytes>& token,
		std::function<void(ELoginResult result, const std::string& nickname,
			const std::array<BYTE, kTokenBytes>& newToken)> onComplete);

private:
	std::string BuildUserKey(const std::string& userId) const;

private:
	CIocpCoreRef				_iocpCore;
	CJobQueueRef				_jobQueue;
	std::unique_ptr<CRedisService>			_redisService;
	CIocpServerServiceRef		_service;
	std::unique_ptr<CRedisServerHeartbeat>	_heartbeat;
	std::shared_ptr<CAccountDBHandler>		_accountHandler;	// COdbcAsyncSrv::Regist()에 등록해두는 핸들러 — CChatServerMain가 소유(수명 보장)

	std::string	_serverType;
	std::string	_serverId;
};

#endif // ndef UC_CHATSERVERMAIN_H