
//***************************************************************************
// ChatServerMain.cpp: implementation of the CChatServerMain class — core.
//
// [분리] ChatServerMain.h 상단의 "구현 파일 분리" 설명 참고. 여기엔
// 생애주기(Start/Stop), 로그인 상태(Redis), 전체 브로드캐스트, 프로필/방
// 이미지 URL 저장/표시 변환 유틸(ToStorableImageRef/ToDisplayImageUrl —
// 계정/방 양쪽이 같이 쓰므로 core에 둠)이 모여있다. 계정/방/채팅 관련
// 구현은 각각 ChatServerMainAccount.cpp/ChatServerMainRoom.cpp/
// ChatServerMainChat.cpp 참고.
//***************************************************************************

#include "pch.h"
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"

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

	// [추가] DB(rooms 테이블)에 저장된 방 목록을 인메모리 레지스트리로
	// 읽어들인다 — 비동기라 이 시점엔 완료를 기다리지 않는다(완료 전
	// 짧은 창에서는 RoomEnterReq가 그 방들을 "존재하지 않음"으로 볼 수
	// 있으나, DB 워커가 보통 매우 빨리 처리하므로 실무 영향은 미미하다고
	// 판단). 구현은 ChatServerMainRoom.cpp 참고.
	LoadRoomRegistryFromDb();

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
	return "User:" + Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에 기록합니다.
// @details [알려진 한계] TTL을 걸지 않는다 — 로그인 상태는 정상적으로는
//          OnUserLogout()의 DEL로만 지워진다. 서버가 크래시(정상 종료 경로를
//          못 타는 경우)하면 이 키가 "online"으로 영구히 남을 수 있다.
//***************************************************************************
void CChatServerMain::OnUserLogin(const std::array<BYTE, kPublicIdBytes>& publicId)
{
	if( _redisService == nullptr )
		return;

	CVector<std::string> args;
	args.push_back("HSET");
	args.push_back(BuildUserKey(publicId));
	args.push_back("serverGroupId");	args.push_back(_serverGroupId);
	args.push_back("serverChannelId");	args.push_back(_serverChannelId);
	args.push_back("status");		args.push_back("online");

	_redisService->SendCommand(args, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 유저 로그인 상태를 Redis에서 제거합니다.
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

	// "{fileServerUrl}/images/{상대경로}" 형식만 우리 파일 서버 소유로 본다 —
	// FileServerMain::BuildPublicUrl()이 만드는 형식과 정확히 일치해야 한다.
	std::string prefix = _fileServerUrl;
	if( !prefix.empty() && prefix.back() == '/' )
		prefix.pop_back();
	prefix += "/images/";

	if( deletedImageRef.rfind(prefix, 0) != 0 )
		return; // 외부 URL(사용자가 직접 등록한 URL 등) — 우리가 지울 대상이 아님

	const std::string relativePath = deletedImageRef.substr(prefix.size());
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
	if( url.empty() || _fileServerUrl.empty() )
		return url;

	std::string hostPrefix = _fileServerUrl;
	if( !hostPrefix.empty() && hostPrefix.back() == '/' )
		hostPrefix.pop_back();

	// "{fileServerUrl}/images/..." -> "/images/..." — 맨 앞 "/"(=/images/의
	// 시작)는 hostPrefix에 포함시키지 않고 남겨서, 결과가 항상 "/"로
	// 시작하는 상대 경로가 되게 한다(ToDisplayImageUrl()이 이 "/"로 판별함).
	if( url.rfind(hostPrefix, 0) == 0 )
	{
		const std::string remainder = url.substr(hostPrefix.size());
		if( !remainder.empty() && remainder.front() == '/' )
			return remainder;
	}

	return url; // 외부 URL이거나 형식이 안 맞음 — 그대로 저장(자르지 않음)
}

//***************************************************************************
// @brief 클라이언트에게 보내기 직전 — ToStorableImageRef()의 역방향.
// @details "/"로 시작하지 않으면(=이미 완전한 URL, 외부 URL이거나 이 기능
//          적용 전에 저장된 예전 값) 그대로 돌려준다 — 그래서 기존 행을
//          마이그레이션하지 않아도 안전하게 계속 동작한다.
//***************************************************************************
std::string CChatServerMain::ToDisplayImageUrl(const std::string& imageRef) const
{
	if( imageRef.empty() || _fileServerUrl.empty() || imageRef.front() != '/' )
		return imageRef;

	std::string hostPrefix = _fileServerUrl;
	if( !hostPrefix.empty() && hostPrefix.back() == '/' )
		hostPrefix.pop_back();

	return hostPrefix + imageRef;
}