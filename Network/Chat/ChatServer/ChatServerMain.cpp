
//***************************************************************************
// ChatServerMain.cpp: implementation of the CChatServerMain class.
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DBSignupRequest.h"

#include <cstring>

namespace
{
	// TryPushDBAsyncRequest()의 백프레셔 임계값 — 이 이상 쌓여 있으면 새
	// 요청 게시 자체를 거부(DbError)한다. COdbcAsyncSrv의
	// MAX_WARNING_QUERY_QUEUE_SIZE(100000, 경고 로그용 임계값)보다 훨씬
	// 보수적으로 낮게 잡았다 — 로그인 큐는 "경고만 남기고 계속 쌓이는"
	// 것보다 "일정 수준 이상이면 빠르게 실패시켜 클라이언트가 재시도하게"
	// 하는 편이 낫다고 판단.
	constexpr size_t kMaxDbQueueCapacity = 5000;
}

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
	CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
	CVector<CDBNode> dbNodeVec, int32 dbMaxThreadCnt,
	std::string serverType, std::string serverId,
	int32 maxSessionCount, uint32 workerThreadCount,
	int32 heartbeatTtlSec, int32 heartbeatIntervalSec)
{
	_serverType = std::move(serverType);
	_serverId = std::move(serverId);

	// 1. IOCP 코어 + JobQueue(Redis/DB 콜백 디스패치용) 준비
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 세션/하트비트보다 먼저 준비되어야 로그인 즉시 기록 가능
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisNodeVec, redisPoolSize) )
		return false;

	// 2-1. 회원 DB(ODBC) 초기화. 회원가입/재접속 검증 핸들러(kDbCallIdent_Signup)는
	// AccountDBHandler.cpp의 DECLARE_ODBC_DBASYNC_HANDLER 매크로가 정적
	// 초기화 시점(main() 진입 전)에 COdbcAsyncSrv::Instance()->Regist()로
	// 이미 등록해뒀으므로 여기서 별도로 만들거나 등록할 필요가 없다.
	// COdbcAsyncSrv는 싱글턴 — 다른 서버 모듈과 공유될 수 있으므로 여기서
	// 실행 중인 서비스를 덮어쓰지 않도록 StartService()가 멱등인지(이미 열려
	// 있으면 재호출을 무시하는지) 여부는 실제 구현 확인이 필요합니다.
	if( !COdbcAsyncSrv::Instance()->StartService(dbNodeVec, dbMaxThreadCnt) )
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

	// COdbcAsyncSrv 싱글턴 자체는 정지하지 않는다(소유권 밖 — 다른 서버
	// 모듈과 공유될 수 있음). 회원가입 핸들러 등록도 이제 별도 소유물이 아니라
	// AccountDBHandler.cpp의 정적 초기화가 프로세스 전체 수명 동안 갖고
	// 있으므로 여기서 정리할 것이 없다.

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

//***************************************************************************
// @brief 회원가입/재접속 검증을 DB 비동기 워커에 요청합니다.
// @details
// [스레드 이관] AccountDBHandler.cpp의 핸들러는 DB 비동기 워커 스레드
// (COdbcAsyncSrv 내부 워커)에서 실행되며, 그 안에서 req->onComplete()를 직접
// 호출한다. 여기서 그 결과를 곧바로 넘기는 대신 _jobQueue로 한 번 이관해
// CRedisService 콜백과 동일한 스레드 모델로 통일한다.
//
// [설계] 요청 생성+백프레셔+AddOutstandingRequest/Push 대칭 처리를
// TryPushDBAsyncRequest() 헬퍼로 위임했다. 이 헬퍼의 백프레셔는
// COdbcAsyncSrv::WaitPushCapacity()(블로킹) 대신 큐 크기를 논블로킹으로
// 확인만 하고 초과 시 즉시 실패시키는 방식이다 — 이 함수가 IOCP 워커
// 스레드(ChatLoginHandler.cpp::HandleLoginReq())에서 호출되므로, 여기서
// 블로킹 대기를 걸면 그 워커가 담당하는 다른 세션들의 I/O 처리까지 함께
// 지연되기 때문이다.
//***************************************************************************
void CChatServerMain::RequestSignup(
	std::shared_ptr<CChatSession> session,
	const std::string& nickname,
	bool hasToken,
	const std::array<BYTE, kTokenBytes>& token,
	std::function<void(ELoginResult result, const std::string& nickname,
		const std::array<BYTE, kTokenBytes>& newToken)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// DB 워커 스레드 → JobQueue로 이관하는 콜백. CChatSession의 public
	// API만 쓰면 어느 스레드가 실제로 이 잡을 실행하든 안전하다.
	auto dispatchToJobQueue = [jobQueue, onComplete](ELoginResult result, const std::string& completedNickname,
		const std::array<BYTE, kTokenBytes>& newToken)
		{
			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedNickname, newToken]()
				{
					if( onComplete )
						onComplete(result, completedNickname, newToken);
				});
		};

	const bool pushed = TryPushDBAsyncRequest<COdbcAsyncSrv, ST_SIGNUP_REQ>(
		kDbCallIdent_Signup,
		[&nickname, hasToken, &token, dispatchToJobQueue](ST_SIGNUP_REQ* req)
		{
			const size_t copyLen = (std::min)(nickname.size(), sizeof(req->nickname) - 1);
			::memcpy(req->nickname, nickname.data(), copyLen);
			// 나머지는 {} 초기화로 이미 0-채움 → NUL 종단 보장

			req->hasToken = hasToken;
			if( hasToken )
				::memcpy(req->token, token.data(), token.size());

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		// 게시 실패 — 백프레셔로 거부됐거나(큐 포화) Push() 자체가
		// 실패(서비스 종료 시점 등)한 경우. 어느 쪽이든 요청이 큐에
		// 들어가지 않았으므로 콜백을 직접(이 스레드에서) 호출해 세션이
		// 응답을 무한정 기다리지 않게 한다.
		if( onComplete )
			onComplete(ELoginResult::DbError, nickname, std::array<BYTE, kTokenBytes>{});
	}
}