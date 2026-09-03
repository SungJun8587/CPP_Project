
//***************************************************************************
// ChatServer.cpp: implementation of the CChatServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "ChatServerMain.h"
#include "ChatSession.h"

//***************************************************************************
// @brief 소멸자 — 아직 실행 중이면 Stop()으로 정리합니다.
//***************************************************************************
CChatServerMain::~CChatServerMain()
{
	Stop();
}

//***************************************************************************
// @brief 채팅 서버 구동.
//***************************************************************************
bool CChatServerMain::Start(
	const _tstring& bindIp, uint16 bindPort,
	const std::string& redisIp, uint16 redisPort, int32 redisPoolSize,
	std::string serverType, std::string serverId,
	int32 maxSessionCount, uint32 workerThreadCount,
	int32 heartbeatTtlSec, int32 heartbeatIntervalSec)
{
	_serverType = std::move(serverType);
	_serverId = std::move(serverId);

	// 1. IOCP 코어 + JobQueue(Redis 콜백 디스패치용) 준비
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 세션/하트비트보다 먼저 준비되어야 로그인 즉시 기록 가능
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisIp, redisPort, redisPoolSize) )
		return false;

	// 3. IOCP 서버 서비스 시작 — 세션 팩토리가 CChatSession을 생성하며 this를 주입
	SessionFactory factory = [this]() -> CSessionRef
		{
			return std::make_shared<CChatSession>(this);
		};

	EngineCoreRef engineCore = _iocpCore;
	CNetServiceRef service = CNetworkFactory::CreateServerService(
		engineCore, CNetAddress(bindIp, bindPort), factory, maxSessionCount, workerThreadCount);

	_service = std::static_pointer_cast<CIocpServerService>(service);
	if( _service == nullptr )
		return false;

	if( !_service->Start() )
	{
		_service = nullptr;
		return false;
	}

	// 4. 서버 생존 하트비트 시작 — IOCP가 실제로 뜬 뒤에 시작(뜨기 전에 하트비트가
	//    "살아있다"고 알리는 것은 의미가 없음)
	_heartbeat = std::make_unique<CRedisServerHeartbeat>(_redisService.get(), _serverType, _serverId, bindPort);
	if( !_heartbeat->Start(heartbeatTtlSec, heartbeatIntervalSec) )
	{
		Stop();
		return false;
	}

	return true;
}

//***************************************************************************
// @brief 채팅 서버 정지. 하트비트 → IOCP 서비스 순으로 정리합니다.
// @details 순서가 중요: 하트비트를 먼저 멈춰 "서버 죽음"을 Redis에 알린 뒤에
//          세션들을 끊어야, 하트비트가 아직 살아있는 상태에서 유저들이
//          접속 중인 것처럼 보이는 창을 최소화한다.
//***************************************************************************
void CChatServerMain::Stop()
{
	if( _heartbeat )
	{
		_heartbeat->Stop();
		_heartbeat.reset();
	}

	if( _service )
	{
		_service->Close(); // 모든 세션 종료까지 블로킹 대기 — 각 세션의 OnDisconnected()가 OnUserLogout()을 호출해 User: 키 정리
		_service.reset();
	}

	_redisService.reset();
	_jobQueue.reset();
	_iocpCore.reset();
}

//***************************************************************************
// @brief 유저 Redis 키를 생성합니다.
//***************************************************************************
std::string CChatServerMain::BuildUserKey(const std::string& userId) const
{
	return "User:" + userId;
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에 기록합니다.
// @details [알려진 한계] TTL을 걸지 않는다 — 로그인 상태는 정상적으로는
//          OnUserLogout()의 DEL로만 지워진다. 서버가 크래시(정상 종료 경로를
//          못 타는 경우)하면 이 키가 "online"으로 영구히 남을 수 있다.
//          기본 뼈대 범위 밖으로 남겨두며, 필요하면 하트비트처럼 TTL+주기
//          갱신을 붙이거나, 서버 시작 시 자신의 serverId가 찍힌 잔여 User
//          키를 스캔해 정리하는 별도 로직을 추가하는 것을 권장한다.
//***************************************************************************
void CChatServerMain::OnUserLogin(const std::string& userId)
{
	if( _redisService == nullptr )
		return;

	CVector<std::string> args;
	args.push_back("HSET");
	args.push_back(BuildUserKey(userId));
	args.push_back("serverType");	args.push_back(_serverType);
	args.push_back("serverId");	args.push_back(_serverId);
	args.push_back("status");		args.push_back("online");

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에서 제거합니다.
//***************************************************************************
void CChatServerMain::OnUserLogout(const std::string& userId)
{
	if( _redisService == nullptr )
		return;

	CVector<std::string> args;
	args.push_back("DEL");
	args.push_back(BuildUserKey(userId));

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 모든 접속 세션에게 브로드캐스트합니다.
//***************************************************************************
void CChatServerMain::Broadcast(const void* data, uint16 size)
{
	if( _service )
		_service->GetSessionManager().Broadcast(data, size);
}
