
//***************************************************************************
// ChatServerMain.cpp: implementation of the CChatServerMain class — core.
//
//***************************************************************************

#include "pch.h"
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"

#include <chrono>
#include <algorithm>

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
	std::string serverName, std::string serverGroupId, std::string serverChannelId,
	int32 maxSessionCount, uint32 workerThreadCount,
	int32 heartbeatTtlSec, int32 heartbeatIntervalSec)
{
	_serverName = std::move(serverName);
	_serverGroupId = std::move(serverGroupId);
	_serverChannelId = std::move(serverChannelId);

	// 메시지 id를 (현재 시각 ms << 10)부터 발급한다 — 재시작 후에도 id가 단조 증가해
	// Redis에 남아 있는 방 대화 기록(RoomChatOrder/RoomChatMsg)의 id와 겹치지 않는다.
	_nextMessageId.store(
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << 10);

	// 1. IOCP 코어 + JobQueue(Redis/DB 콜백 디스패치용) 준비
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 세션/하트비트보다 먼저 준비되어야 로그인 즉시 기록 가능
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisNodeVec, redisPoolSize) )
		return false;

	// 2-1. 회원 DB(ODBC) 초기화. 회원가입/재접속 검증 핸들러(kDbCallIdent_Signup)는
	// AccountDBHandler.cpp의 DECLARE_DBASYNC_HANDLER_EX 매크로가 정적
	// 초기화 시점(main() 진입 전)에 MEMBER_DB_ASYNC(CDbServiceManager::Instance().MemberDB())에
	// 이미 등록해뒀으므로 여기서 핸들러를 별도로 만들거나 등록할 필요는
	// 없다. 다만 COdbcAsyncSrv 자신은 이제 싱글턴이 아니라 CDbServiceManager가
	// 도메인별로 소유하는 재사용 가능한 부품이므로, 실제 DB 접속/워커
	// 스레드 기동(StartService())은 여기서 명시적으로 호출해야 한다.
	// [주의] StartService()는 인스턴스당 1회만 허용된다(재호출 시
	// false 반환) — 이 함수가 여러 번 불리는 경로가 있다면 여기서
	// 실패로 걸린다.
	if( !MEMBER_DB_ASYNC.StartService(dbNodeVec, dbMaxThreadCnt) )
		return false;

	// DB(rooms 테이블)에 저장된 방 목록을 인메모리 레지스트리로 읽어들인다. 세션을 받기 전에
	// 완료를 기다려, 기존 방이 "존재하지 않음"으로 보이는 구간이 없게 한다. 구현은 ChatServerMainRoom.cpp 참고.
	if( !LoadRoomRegistryFromDb() )
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
	_heartbeat = std::make_unique<CRedisServerHeartbeat>(_redisService.get(), _serverName, _serverGroupId, _serverChannelId, bindPort);

	// [추가] 세션 수(동접자수) 콜백 — Start() 전에 등록해야 최초 HSET 등록
	// (RegisterInitial())부터 값이 반영된다. _service는 바로 위(3번)에서
	// 이미 시작됐으므로 이 시점엔 항상 유효 — Stop()에서도 _heartbeat를
	// _service보다 먼저 정지/정리하므로, 이 콜백이 살아있는 동안 _service가
	// 사라져 있는 경우는 없다.
	_heartbeat->SetSessionCountProvider([this]() -> int32
		{
			return _service ? static_cast<int32>(_service->GetSessionManager().GetSessionCount()) : 0;
		});

	if( !_heartbeat->Start(heartbeatTtlSec, heartbeatIntervalSec) )
	{
		Stop();
		return false;
	}

	// 로그인 키(User:{id})도 하트비트와 같은 TTL/주기로 갱신한다 — 서버가 비정상 종료하면
	// 하트비트 키와 함께 로그인 키도 TTL 안에 사라진다.
	StartMaintenance(heartbeatTtlSec, heartbeatIntervalSec);

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
	StopMaintenance();

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

	// MEMBER_DB_ASYNC(CDbServiceManager 소유)는 정지하지 않는다(소유권
	// 밖 — 다른 서버 모듈과 공유될 수 있는 프로세스 전역 자원). 회원가입
	// 핸들러 등록도 이제 이 클래스의 소유물이 아니라 AccountDBHandler.cpp의
	// 정적 초기화가 프로세스 전체 수명 동안 갖고 있으므로 여기서 정리할
	// 것이 없다.

	_redisService.reset();
	_jobQueue.reset();
	_iocpCore.reset();
}

//***************************************************************************
// @brief 유저 Redis 키를 생성합니다.
// @details [설계 변경] nickname이 아니라 public_id(16진 인코딩) 기준으로
// 바뀌었다 — 닉네임이 바뀌어도 이 키는 절대 흔들리지 않는다.
//***************************************************************************
std::string CChatServerMain::BuildUserKey(const std::array<BYTE, kPublicIdBytes>& publicId) const
{
	return "User:" + PublicIdToHex(publicId);
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에 기록합니다.
// @details 로그인 키에는 TTL(하트비트 TTL과 동일)이 걸리고, 접속 중인 동안에는
//          RefreshLoginKeys()가 하트비트 주기로 갱신한다. 정상적으로는 OnUserLogout()의
//          DEL로 지워지고, 서버가 비정상 종료해 DEL이 실행되지 못하면 TTL로 저절로 사라진다.
//          HSET과 EXPIRE는 하나의 EVAL로 보내 키가 TTL 없이 남는 순간이 없게 한다.
//***************************************************************************
void CChatServerMain::OnUserLogin(const std::array<BYTE, kPublicIdBytes>& publicId)
{
	if( _redisService == nullptr )
		return;

	static const std::string kLoginScript =
		"redis.call('HSET', KEYS[1], 'serverGroupId', ARGV[1], 'serverChannelId', ARGV[2], 'status', 'online')\n"
		"if tonumber(ARGV[3]) > 0 then redis.call('EXPIRE', KEYS[1], ARGV[3]) end\n"
		"return 1";

	CVector<std::string> args;
	args.push_back("EVAL");
	args.push_back(kLoginScript);
	args.push_back("1");
	args.push_back(BuildUserKey(publicId));
	args.push_back(_serverGroupId);
	args.push_back(_serverChannelId);
	args.push_back(std::to_string(_loginKeyTtlSec));

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 유지보수 스레드를 시작합니다.
// @param ttlSec 로그인 키 TTL(초) — 서버 하트비트 TTL과 같은 값. 0 이하면 로그인 키 갱신은 하지 않는다.
// @param intervalSec 로그인 키 갱신 주기(초) — 서버 하트비트 주기와 같은 값. TTL 이상이면 TTL의 1/3로 보정한다.
//***************************************************************************
void CChatServerMain::StartMaintenance(int32 ttlSec, int32 intervalSec)
{
	_loginKeyTtlSec = (ttlSec > 0) ? ttlSec : 0;
	if( _loginKeyTtlSec > 0 && (intervalSec <= 0 || intervalSec >= _loginKeyTtlSec) )
		intervalSec = (std::max)(1, _loginKeyTtlSec / 3);

	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		_maintenanceStop = false;
	}
	_maintenanceThread = std::thread([this, intervalSec]() { MaintenanceLoop(intervalSec); });
}

//***************************************************************************
// @brief 유지보수 스레드를 멈추고 합류합니다(실행 중이 아니면 아무것도 하지 않음). 남은 지연 작업은 버린다.
//***************************************************************************
void CChatServerMain::StopMaintenance()
{
	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		_maintenanceStop = true;
	}
	_maintenanceCv.notify_all();

	if( _maintenanceThread.joinable() )
		_maintenanceThread.join();

	std::lock_guard<std::mutex> lock(_maintenanceMutex);
	_delayedTasks.clear();
	_dirtyRoomCounts.clear();
}

void CChatServerMain::MaintenanceLoop(int32 loginKeyIntervalSec)
{
	using Clock = std::chrono::steady_clock;
	auto nextLoginKeyRefresh = Clock::now() + std::chrono::seconds((std::max)(1, loginKeyIntervalSec));

	std::unique_lock<std::mutex> lock(_maintenanceMutex);
	while( !_maintenanceStop )
	{
		if( _maintenanceCv.wait_for(lock, std::chrono::milliseconds(kMaintenanceTickMs), [this]() { return _maintenanceStop; }) )
			break;

		lock.unlock();
		FlushRoomUserCounts();
		RunDueDelayedTasks();
		if( _loginKeyTtlSec > 0 && Clock::now() >= nextLoginKeyRefresh )
		{
			RefreshLoginKeys();
			nextLoginKeyRefresh = Clock::now() + std::chrono::seconds((std::max)(1, loginKeyIntervalSec));
		}
		lock.lock();
	}
}

//***************************************************************************
// @brief 표시된 방들의 현재 인원수를 각 방 멤버에게 한 번씩 알립니다.
//***************************************************************************
void CChatServerMain::FlushRoomUserCounts()
{
	std::unordered_set<int32> rooms;
	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		if( _dirtyRoomCounts.empty() )
			return;
		rooms.swap(_dirtyRoomCounts);
	}

	for( int32 roomId : rooms )
	{
		RoomUserCountNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::RoomUserCountNotify);
		notify.roomId = roomId;
		notify.userCount = GetRoomUserCount(roomId);
		BroadcastToRoom(roomId, &notify, notify.size);
	}
}

void CChatServerMain::ScheduleDelayed(int32 delayMs, std::function<void()> task)
{
	if( !task )
		return;

	const auto due = std::chrono::steady_clock::now() + std::chrono::milliseconds((std::max)(0, delayMs));
	std::lock_guard<std::mutex> lock(_maintenanceMutex);
	_delayedTasks.emplace(due, std::move(task));
}

void CChatServerMain::RunDueDelayedTasks()
{
	const auto now = std::chrono::steady_clock::now();
	std::vector<std::function<void()>> due;
	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		auto end = _delayedTasks.upper_bound(now);
		for( auto it = _delayedTasks.begin(); it != end; ++it )
			due.push_back(std::move(it->second));
		_delayedTasks.erase(_delayedTasks.begin(), end);
	}
	for( auto& task : due )
		task();
}

//***************************************************************************
// @brief 접속 중인 모든 계정의 로그인 키 TTL을 갱신합니다.
// @details 한 번에 최대 kBatch명씩 EVAL 하나로 처리한다. 키가 없으면(Redis 재시작 등) 다시 만든다.
//          스크립트의 "User:" 접두사는 BuildUserKey()와 같아야 한다.
//***************************************************************************
void CChatServerMain::RefreshLoginKeys()
{
	if( _redisService == nullptr || _loginKeyTtlSec <= 0 )
		return;

	std::vector<std::string> ids;
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		ids.reserve(_onlineSessions.size());
		for( const auto& kv : _onlineSessions )
		{
			if( std::any_of(kv.second.begin(), kv.second.end(), [](const std::weak_ptr<CChatSession>& w) { return !w.expired(); }) )
				ids.push_back(kv.first);
		}
	}

	static const std::string kRefreshScript =
		"for i = 4, #ARGV do\n"
		"  local key = 'User:' .. ARGV[i]\n"
		"  if redis.call('EXPIRE', key, ARGV[3]) == 0 then\n"
		"    redis.call('HSET', key, 'serverGroupId', ARGV[1], 'serverChannelId', ARGV[2], 'status', 'online')\n"
		"    redis.call('EXPIRE', key, ARGV[3])\n"
		"  end\n"
		"end\n"
		"return 1";

	constexpr size_t kBatch = 200;
	for( size_t begin = 0; begin < ids.size(); begin += kBatch )
	{
		const size_t end = (std::min)(begin + kBatch, ids.size());

		CVector<std::string> args;
		args.push_back("EVAL");
		args.push_back(kRefreshScript);
		args.push_back("0");
		args.push_back(_serverGroupId);
		args.push_back(_serverChannelId);
		args.push_back(std::to_string(_loginKeyTtlSec));
		for( size_t i = begin; i < end; ++i )
			args.push_back(ids[i]);

		_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
	}
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에서 제거합니다.
// @details 같은 계정의 다른 세션이 남아 있는지는 호출부(CChatSession::OnDisconnected)가
//          OnFriendLogout()의 반환값으로 판단해 마지막 세션일 때만 호출한다.
//***************************************************************************
void CChatServerMain::OnUserLogout(const std::array<BYTE, kPublicIdBytes>& publicId)
{
	if( _redisService == nullptr )
		return;

	CVector<std::string> args;
	args.push_back("DEL");
	args.push_back(BuildUserKey(publicId));

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 이 publicId가 지금 서버 어딘가에 로그인해 있는지 조회합니다.
//***************************************************************************
bool CChatServerMain::IsUserOnline(const std::array<BYTE, kPublicIdBytes>& publicId) const
{
	std::lock_guard<std::mutex> lock(_onlineMutex);
	auto it = _onlineSessions.find(PublicIdToHex(publicId));
	return it != _onlineSessions.end() &&
		std::any_of(it->second.begin(), it->second.end(), [](const std::weak_ptr<CChatSession>& w) { return !w.expired(); });
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
// @brief 이 서버 프로세스의 현재 전체 접속자 수(TCP 연결 기준)를 반환합니다.
// @details [설계 변경] 로그인/로그아웃마다 브로드캐스트하던 방식(NotifyServerUserCount())을
//          없애고, ServerUserCountHandler.cpp가 클라이언트의 폴링 요청에
//          응답할 때 이 함수를 호출해 그 시점의 값을 그대로 돌려주는
//          방식으로 바꿨다.
//***************************************************************************
int32 CChatServerMain::GetServerUserCount() const
{
	return _service ? static_cast<int32>(_service->GetSessionManager().GetSessionCount()) : 0;
}

//***************************************************************************
// @brief deletedImageRef가 이 서버의 파일 서버 소유면 삭제 큐에 등록한다.
//***************************************************************************
void CChatServerMain::ScheduleFileDeletionIfOwned(const std::string& deletedImageRef)
{
	if( deletedImageRef.empty() || _fileServerUrl.empty() || _redisService == nullptr )
		return;

	// "{fileServerUrl}/images/{상대경로}" 형식만 우리 파일 서버 소유로 본다(ImageRef 규약).
	// 외부 URL(사용자가 직접 등록한 URL 등)은 우리가 지울 대상이 아니다.
	const std::string relativePath = ImageRef::ExtractOwnedRelativePath(_fileServerUrl, deletedImageRef);
	if( relativePath.empty() )
		return;

	CVector<std::string> args;
	args.push_back("RPUSH");
	args.push_back("FileServer:PendingDeletions");
	args.push_back(relativePath);

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief DB에 저장하기 직전 — "{fileServerUrl}/images/{경로}" 형태면
//        "/images/{경로}"만 남기고 호스트 부분을 잘라낸다.
// @details ScheduleFileDeletionIfOwned()와 정확히 같은 접두사 판별 방식을
//          쓴다 — FileServerMain::BuildPublicUrl()이 만드는 형식과 일치해야
//          하므로, 그 함수가 이미 검증해둔 로직을 그대로 재사용했다.
//***************************************************************************
std::string CChatServerMain::ToStorableImageRef(const std::string& url) const
{
	return ImageRef::ToStorable(_fileServerUrl, url);
}

//***************************************************************************
// @brief 클라이언트에게 보내기 직전 — ToStorableImageRef()의 역방향.
// @details "/"로 시작하지 않으면(=이미 완전한 URL, 외부 URL이거나 이 기능
//          적용 전에 저장된 예전 값) 그대로 돌려준다 — 그래서 기존 행을
//          마이그레이션하지 않아도 안전하게 계속 동작한다.
//***************************************************************************
std::string CChatServerMain::ToDisplayImageUrl(const std::string& imageRef) const
{
	return ImageRef::ToDisplay(_fileServerUrl, imageRef);
}

//***************************************************************************
// @brief url이 우리 파일 서버 소유 이미지인지(ImageRef 규약) 판별한다.
//***************************************************************************
bool CChatServerMain::IsOwnedImageUrl(const std::string& url) const
{
	return !ImageRef::ExtractOwnedRelativePath(_fileServerUrl, url).empty();
}