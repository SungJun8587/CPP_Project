
//***************************************************************************
// ChatServerMain.cpp: implementation of the CChatServerMain class.
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include <Redis/RedisResultSet.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"
#include "DBSignupRequest.h"
#include "DBChangeNicknameRequest.h"
#include "DBSetProfileImageUrlRequest.h"
#include "DBSelectProfileImageRequest.h"
#include "DBDeleteProfileImageRequest.h"
#include "DBCreateRoomRequest.h"
#include "DBDeleteRoomRequest.h"
#include "DBRenameRoomRequest.h"
#include "DBListRoomsRequest.h"
#include "DBTransferRoomOwnerRequest.h"
#include "DBSetRoomImageRequest.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace
{
	// PushDBAsyncRequest()의 백프레셔 임계값 — 이 이상 쌓여 있으면 새
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
	// 판단).
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
// @brief 새로 브로드캐스트할 채팅 메시지에 부여할 다음 고유 ID를 반환하고,
//        그 발신자/방 정보를 삭제 검증용으로 기억해둔다(kMaxTrackedMessages개
//        초과 시 가장 오래된 항목부터 자동으로 밀어낸다).
//***************************************************************************
int64 CChatServerMain::RegisterOutgoingMessage(const std::array<BYTE, kPublicIdBytes>& senderPublicId, int32 roomId)
{
	const int64 messageId = _nextMessageId.fetch_add(1);

	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	_messageOwners[messageId] = SMessageOwnerRecord{ senderPublicId, roomId };
	_messageOwnerOrder.push_back(messageId);

	while( _messageOwnerOrder.size() > kMaxTrackedMessages )
	{
		const int64 oldestId = _messageOwnerOrder.front();
		_messageOwnerOrder.pop_front();
		_messageOwners.erase(oldestId);
	}

	return messageId;
}

//***************************************************************************
// @brief messageId 삭제를 시도한다 — 추적 저장소에 남아있고 발신자가
//        일치할 때만 성공, 성공 시 저장소에서 제거하고 outRoomId를 채운다.
// @details [수정 — 순서 큐 정리] 삭제에 성공한 messageId를 _messageOwners에서만
//          지우고 _messageOwnerOrder(삽입 순서 큐)에 그대로 남겨두면,
//          나중에 RegisterOutgoingMessage()의 밀어내기 루프가 이미 없는
//          ID를 큐에서 뽑아 erase()를 또 호출하는 낭비(해는 없지만 불필요한
//          작업)가 쌓인다 — std::deque에서 특정 원소 하나를 지우는 건
//          O(n)이라 매 삭제마다 하기엔 아깝지만, 이 큐는 애초에
//          kMaxTrackedMessages(500)로 크기가 짧게 묶여있어 실질적인
//          비용은 무시할 수준이다.
//***************************************************************************
CChatServerMain::EDeleteMessageResult CChatServerMain::TryDeleteMessage(int64 messageId, const std::array<BYTE, kPublicIdBytes>& requesterPublicId, int32& outRoomId)
{
	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	auto it = _messageOwners.find(messageId);
	if( it == _messageOwners.end() )
		return EDeleteMessageResult::NotFound;

	if( it->second.senderPublicId != requesterPublicId )
		return EDeleteMessageResult::NotOwner;

	outRoomId = it->second.roomId;
	_messageOwners.erase(it);

	auto orderIt = std::find(_messageOwnerOrder.begin(), _messageOwnerOrder.end(), messageId);
	if( orderIt != _messageOwnerOrder.end() )
		_messageOwnerOrder.erase(orderIt);

	return EDeleteMessageResult::Ok;
}

//***************************************************************************
// @brief 회원가입/재접속 검증을 DB 비동기 워커에 요청합니다.
// @details
// [스레드 이관] AccountDBHandler.cpp의 핸들러는 DB 비동기 워커 스레드
// (MEMBER_DB_ASYNC 내부 워커)에서 실행되며, 그 안에서 req->onComplete()를
// 직접 호출한다. 여기서 그 결과를 곧바로 넘기는 대신 _jobQueue로 한 번
// 이관해 CRedisService 콜백과 동일한 스레드 모델로 통일한다.
//
// [설계] 요청 생성+백프레셔+AddOutstandingRequest/Push 대칭 처리를
// PushDBAsyncRequest() 헬퍼로 위임했다. COdbcAsyncSrv 자신은 이제
// Instance()가 없어서(도메인별 다중 인스턴스를 CDbServiceManager가
// 소유) 인스턴스를 직접 넘기는 오버로드를 쓴다. 이 헬퍼의 백프레셔는
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
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::array<BYTE, kTokenBytes>& token,
	std::function<void(ELoginResult result, const std::string& nickname,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& newToken,
		const std::string& profileImageUrl)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// DB 워커 스레드 → JobQueue로 이관하는 콜백. CChatSession의 public
	// API만 쓰면 어느 스레드가 실제로 이 잡을 실행하든 안전하다.
	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::string& completedNickname,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::array<BYTE, kTokenBytes>& newToken,
		const std::string& profileImageUrl)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] DB(AccountDBHandler.cpp)가 돌려준 image_ref가 상대
			// 경로("/images/...")로 저장돼 있으면 클라이언트에게 나가기
			// 전에 완전한 URL로 복원한다 — ToDisplayImageUrl() 참고.
			const std::string displayProfileImageUrl = ToDisplayImageUrl(profileImageUrl);

			jobQueue->DoAsync([onComplete, result, completedNickname, completedPublicId, newToken, displayProfileImageUrl]()
				{
					if( onComplete )
						onComplete(result, completedNickname, completedPublicId, newToken, displayProfileImageUrl);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SIGNUP_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_Signup,
		[&nickname, hasToken, &publicId, &token, dispatchToJobQueue](ST_SIGNUP_REQ* req)
		{
			const size_t copyLen = (std::min)(nickname.size(), sizeof(req->nickname) - 1);
			::memcpy(req->nickname, nickname.data(), copyLen);
			// 나머지는 {} 초기화로 이미 0-채움 → NUL 종단 보장

			req->hasToken = hasToken;
			if( hasToken )
			{
				::memcpy(req->publicId, publicId.data(), publicId.size());
				::memcpy(req->token, token.data(), token.size());
			}

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
			onComplete(ELoginResult::DbError, nickname, std::array<BYTE, kPublicIdBytes>{}, std::array<BYTE, kTokenBytes>{}, std::string());
	}
}

//***************************************************************************
// @brief 닉네임 변경을 DB 비동기 워커에 요청합니다.
// @details RequestSignup()과 완전히 동일한 구조(백프레셔/JobQueue 이관)를
// 따른다 — 차이는 요청/콜백 타입뿐이다.
//***************************************************************************
void CChatServerMain::RequestChangeNickname(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::string& newNickname,
	std::function<void(ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newNickname)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [jobQueue, onComplete](ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::string& completedNewNickname)
		{
			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedPublicId, completedNewNickname]()
				{
					if( onComplete )
						onComplete(result, completedPublicId, completedNewNickname);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_CHANGE_NICKNAME_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ChangeNickname,
		[&publicId, &newNickname, dispatchToJobQueue](ST_CHANGE_NICKNAME_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());

			const size_t newCopyLen = (std::min)(newNickname.size(), sizeof(req->newNickname) - 1);
			::memcpy(req->newNickname, newNickname.data(), newCopyLen);

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, publicId, newNickname);
	}
}

//***************************************************************************
// @brief 프로필 이미지 URL 설정을 DB 비동기 워커에 요청합니다.
// @details RequestChangeNickname()과 완전히 동일한 구조 — 차이는 요청/콜백 타입뿐이다.
//***************************************************************************
void CChatServerMain::RequestSetProfileImageUrl(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::string& newUrl,
	std::function<void(ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newUrl,
		int64 newImageId)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// [추가] DB엔 "{fileServerUrl}/images/{경로}" 형태 대신 "/images/{경로}"만
	// 저장한다 — ToStorableImageRef() 참고. 외부 URL이면 그대로 통과된다.
	const std::string storableUrl = ToStorableImageRef(newUrl);

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::string& completedNewUrl,
		int64 newImageId)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] DB(SetProfileImageUrlDBHandler.cpp)는 방금 저장한 값을
			// 그대로 에코해주므로, 상대 경로로 저장했다면 이 시점의
			// completedNewUrl도 상대 경로다 — 클라이언트에겐 항상 완전한
			// URL을 줘야 하므로 여기서 다시 복원한다.
			const std::string displayUrl = ToDisplayImageUrl(completedNewUrl);

			jobQueue->DoAsync([onComplete, result, completedPublicId, displayUrl, newImageId]()
				{
					if( onComplete )
						onComplete(result, completedPublicId, displayUrl, newImageId);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SET_PROFILE_IMAGE_URL_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SetProfileImageUrl,
		[&publicId, &storableUrl, dispatchToJobQueue](ST_SET_PROFILE_IMAGE_URL_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());

			const size_t urlCopyLen = (std::min)(storableUrl.size(), sizeof(req->url) - 1);
			::memcpy(req->url, storableUrl.data(), urlCopyLen);

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, publicId, newUrl, 0);
	}
}

//***************************************************************************
// @brief 세션을 지정한 위치(로비 또는 특정 룸)로 옮깁니다.
//***************************************************************************
void CChatServerMain::MoveToRoom(std::shared_ptr<CChatSession> session, int32 newRoomId, int32& outNewRoomUserCount)
{
	outNewRoomUserCount = 0;
	if( session == nullptr )
		return;

	int32 oldRoomId = -1;
	int32 oldRoomRemainingCount = 0;
	bool hadOldRoom = false;
	int32 newRoomCountAfter = 0;
	// [추가] 방장 이양 판단(HandleRoomOwnershipOnLeave)에 쓸 스냅샷 — 이
	// 함수는 _roomMutex를 해제한 뒤에 호출해야 하므로(그 안에서
	// BroadcastToRoom()/MoveToRoom() 재귀 호출이 다시 _roomMutex를 잠금),
	// 락이 걸려있는 동안 필요한 정보만 미리 복사해둔다.
	std::vector<std::shared_ptr<CChatSession>> oldRoomRemainingMembers;
	const std::array<BYTE, kPublicIdBytes> leavingPublicId = session->GetPublicId();

	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		oldRoomId = session->GetRoomId();

		// 기존 방/로비에서 제거 (oldRoomId == -1이면 "아직 어디에도 배정된
		// 적 없음"이라 제거할 대상 자체가 없다 — 로그인 직후 최초 배정 시나리오)
		if( oldRoomId >= 0 )
		{
			auto oldIt = _roomMembers.find(oldRoomId);
			if( oldIt != _roomMembers.end() )
			{
				auto& members = oldIt->second;
				members.erase(std::remove_if(members.begin(), members.end(),
					[&session](const std::weak_ptr<CChatSession>& w)
					{
						auto s = w.lock();
						return !s || s == session;
					}), members.end());

				oldRoomRemainingCount = static_cast<int32>(members.size());
				hadOldRoom = true;

				oldRoomRemainingMembers.reserve(members.size());
				for( auto& w : members )
				{
					if( auto s = w.lock() )
						oldRoomRemainingMembers.push_back(s);
				}
			}
		}

		// 새 위치에 추가
		session->SetRoomId(newRoomId);
		auto& newMembers = _roomMembers[newRoomId];
		newMembers.push_back(session);

		// 죽은 weak_ptr 청소 겸 실제 인원수 계산
		int32 count = 0;
		for( auto it = newMembers.begin(); it != newMembers.end(); )
		{
			if( it->lock() )
			{
				++count;
				++it;
			}
			else
			{
				it = newMembers.erase(it);
			}
		}
		newRoomCountAfter = count;
	}

	outNewRoomUserCount = newRoomCountAfter;

	// 인원수가 바뀐 두 위치(원래 있던 곳/새로 들어간 곳)에 갱신된 인원수를
	// 알린다 — 이미 그 방에 있던 다른 사람들의 화면도 실시간으로 갱신되게.
	if( hadOldRoom && oldRoomId != newRoomId )
	{
		NotifyRoomUserCount(oldRoomId, oldRoomRemainingCount);

		// [추가] 방금 나간 사람이 그 방의 방장이었는지 확인하고, 맞으면
		// 이양/삭제를 처리한다 — 로비(kLobbyRoomId)나 레지스트리에 없는
		// 위치면 이 함수가 즉시 반환하므로 항상 호출해도 안전하다.
		HandleRoomOwnershipOnLeave(oldRoomId, leavingPublicId, oldRoomRemainingMembers);
	}
	NotifyRoomUserCount(newRoomId, newRoomCountAfter);
}

//***************************************************************************
// @brief 세션이 지금 있는 방(로비 포함)에서만 빠집니다. 연결 종료 전용.
//***************************************************************************
void CChatServerMain::LeaveCurrentRoom(CChatSession* session)
{
	if( session == nullptr )
		return;

	int32 roomId = -1;
	int32 remainingCount = 0;
	bool hadRoom = false;

	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		roomId = session->GetRoomId();
		if( roomId < 0 )
			return; // 로그인 직후 방 배정 전에 끊긴 극히 드문 경우 — 정리할 게 없음

		auto it = _roomMembers.find(roomId);
		if( it == _roomMembers.end() )
			return;

		auto& members = it->second;
		members.erase(std::remove_if(members.begin(), members.end(),
			[session](const std::weak_ptr<CChatSession>& w)
			{
				auto s = w.lock();
				return !s || s.get() == session;
			}), members.end());

		remainingCount = static_cast<int32>(members.size());
		hadRoom = true;
	}

	// [수정 — 버그] 예전엔 여기서도 HandleRoomOwnershipOnLeave()를 불러서,
	// 방장이 "방 나가기"/"방 삭제"를 명시적으로 하지 않고 그냥 접속만
	// 끊어도(네트워크 끊김, 앱 재시작 등) 방장이 자동 이양되거나 — 아무도
	// 안 남았으면 방 자체가 DB에서 삭제됐다. 그런데 방 대화 기록을
	// "방이 없어질 때까지 유지"하기로 한 이상, 단순 접속 끊김만으로 방과
	// 그 기록이 통째로 사라지는 건 사용자가 기대하는 동작이 아니다 —
	// 방장은 오프라인 상태에서도 여전히 방장이고, 재접속하면 그대로 방을
	// 관리할 수 있어야 한다. 그래서 이 함수(연결 종료 전용)에서는 인원수
	// 알림만 보내고, 방장 이양/방 삭제는 명시적 행동(RoomLeaveReq ->
	// MoveToRoom(), DeleteRoomReq -> RequestDeleteRoom())에서만 일어나게
	// 한다 — MoveToRoom()은 여전히 HandleRoomOwnershipOnLeave()를 호출한다.
	if( hadRoom )
		NotifyRoomUserCount(roomId, remainingCount);
}

//***************************************************************************
// @brief 방(로비 포함)의 현재 인원수를 조회합니다.
//***************************************************************************
int32 CChatServerMain::GetRoomUserCount(int32 roomId) const
{
	std::lock_guard<std::mutex> lock(_roomMutex);

	auto it = _roomMembers.find(roomId);
	if( it == _roomMembers.end() )
		return 0;

	int32 count = 0;
	for( auto& w : it->second )
	{
		if( !w.expired() )
			++count;
	}
	return count;
}

//***************************************************************************
// @brief 지정한 방(로비 포함)에 있는 세션들에게만 브로드캐스트합니다.
//***************************************************************************
void CChatServerMain::BroadcastToRoom(int32 roomId, const void* data, uint16 size)
{
	if( data == nullptr || size == 0 )
		return;

	std::vector<std::shared_ptr<CChatSession>> targets;
	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		auto it = _roomMembers.find(roomId);
		if( it == _roomMembers.end() )
			return;

		targets.reserve(it->second.size());
		for( auto& w : it->second )
		{
			if( auto s = w.lock() )
				targets.push_back(s);
		}
	}

	for( auto& s : targets )
	{
		if( s->IsConnected() )
			s->Send(data, size);
	}
}

//***************************************************************************
// @brief roomId의 갱신된 인원수를 그 방의 멤버 전원에게 알립니다.
//***************************************************************************
void CChatServerMain::NotifyRoomUserCount(int32 roomId, int32 userCount)
{
	RoomUserCountNotifyPacket notify{};
	notify.size = sizeof(notify);
	notify.type = static_cast<uint16>(EChatPacketType::RoomUserCountNotify);
	notify.roomId = roomId;
	notify.userCount = userCount;

	BroadcastToRoom(roomId, &notify, notify.size);
}

//***************************************************************************
// @brief 파일 서버 업로드용 임시 토큰을 발급합니다.
// @details Redis에 "UploadToken:{tokenHex}" 키로 이 계정의 public_id(16진)를
// 값으로, TTL 60초로 저장한다. 파일 서버는 이 키를 조회해서 토큰을
// 검증한다 — 채팅 서버와 파일 서버는 서로 직접 통신하지 않는다.
//***************************************************************************
void CChatServerMain::RequestUploadToken(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	std::function<void(bool success, const std::string& uploadToken, const std::string& fileServerUrl)> onComplete)
{
	if( session == nullptr )
		return;

	if( _redisService == nullptr || _fileServerUrl.empty() )
	{
		// Redis가 없거나 파일 서버 주소가 설정 안 돼 있으면 업로드 기능
		// 자체를 못 쓰는 상태 — 실패로 응답한다.
		if( onComplete )
			onComplete(false, std::string(), std::string());
		return;
	}

	// 토큰 = 무작위 32바이트를 16진 인코딩(64자) — 추측 불가능한 값이어야
	// 다른 사람이 남의 토큰을 짐작해서 도용할 수 없다.
	BYTE randomBytes[32] = {};
	if( !Crypto::CCryptoUtil::GenerateRandomBytes(randomBytes, sizeof(randomBytes)) )
	{
		if( onComplete )
			onComplete(false, std::string(), std::string());
		return;
	}

	const std::string tokenHex = Crypto::CCryptoUtil::ToHex(randomBytes, sizeof(randomBytes));
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	const std::string redisKey = "UploadToken:" + tokenHex;
	const std::string fileServerUrl = _fileServerUrl;

	// SET key value EX seconds — 한 커맨드로 등록+TTL을 동시에 건다
	// (RedisServerHeartbeat.cpp의 HSET+EXPIRE 두 단계보다 간단 — 여긴 필드가
	// 값 하나뿐이라 SET의 EX 옵션만으로 충분하다).
	CVector<std::string> args;
	args.push_back("SET");
	args.push_back(redisKey);
	args.push_back(publicIdHex);
	args.push_back("EX");
	args.push_back("60");

	_redisService->SendCommand(args, [onComplete, tokenHex, fileServerUrl](const RedisValue& /*res*/)
		{
			// [참고] RedisValue의 성공/에러 판별 API를 이 헤더만으론 확인 못해,
			// OnUserLogin()과 마찬가지로 콜백이 왔다는 것 자체를 "등록 완료"로
			// 본다(TODO: 에러 체크 메서드가 있다면 감싸는 것을 권장).
			// [수정] CRedisService::SendCommand()의 콜백은 이미 생성자에
			// 넘긴 CJobQueue 스레드에서 안전하게 실행된다(RedisService.cpp
			// 내부에서 DoAsync로 이관해줌) — 여기서 또 감싸는 건 불필요한
			// 이중 디스패치였다.
			if( onComplete )
				onComplete(true, tokenHex, fileServerUrl);
		});
}


//***************************************************************************
// @brief 로그인된 계정이 갖고 있는 프로필 이미지 전체 목록을 조회합니다.
//***************************************************************************
void CChatServerMain::RequestListProfileImages(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	std::function<void(ELoginResult result, const std::vector<SProfileImageEntry>& images)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::vector<SProfileImageEntry>& images)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] 목록 항목마다 image_ref가 상대 경로로 저장돼 있으면
			// 완전한 URL로 복원한다 — RequestSignup()과 동일한 이유
			// (ToDisplayImageUrl() 참고). 벡터를 복사해서 그 자리에서
			// 바꿔치기한다 — 원본 images는 DB 콜백 스코프가 끝나면 사라지므로
			// 어차피 복사가 필요했다.
			std::vector<SProfileImageEntry> displayImages = images;
			for( SProfileImageEntry& entry : displayImages )
				entry.imageRef = ToDisplayImageUrl(entry.imageRef);

			jobQueue->DoAsync([onComplete, result, displayImages]()
				{
					if( onComplete )
						onComplete(result, displayImages);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_PROFILE_IMAGES_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListProfileImages,
		[&publicId, dispatchToJobQueue](ST_LIST_PROFILE_IMAGES_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
	}
}

//***************************************************************************
// @brief 갤러리에 이미 있는 이미지 하나를 대표로 지정합니다.
//***************************************************************************
void CChatServerMain::RequestSelectProfileImage(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	int64 imageId,
	std::function<void(ELoginResult result, int64 imageId, const std::string& selectedImageRef)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [jobQueue, onComplete](ELoginResult result, int64 completedImageId, const std::string& selectedImageRef)
		{
			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedImageId, selectedImageRef]()
				{
					if( onComplete )
						onComplete(result, completedImageId, selectedImageRef);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SELECT_PROFILE_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SelectProfileImage,
		[&publicId, imageId, dispatchToJobQueue](ST_SELECT_PROFILE_IMAGE_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->imageId = imageId;
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, imageId, std::string());
	}
}

//***************************************************************************
// @brief 갤러리에서 이미지 하나를 삭제합니다. DB 레코드만 지우고, 실제
//        파일 삭제는 파일 서버 소관이라 이 서버는 관여하지 않습니다.
//***************************************************************************
void CChatServerMain::RequestDeleteProfileImage(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	int64 imageId,
	std::function<void(ELoginResult result, int64 imageId, bool wasActive, const std::string& deletedImageRef)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, int64 completedImageId,
		bool wasActive, const std::string& deletedImageRef)
		{
			// [설계] 실제 파일 삭제는 이 서버가 직접 하지 않는다 — 대신
			// deletedImageRef가 우리 파일 서버 소유면 Redis 큐에 "지울 것"만
			// 남겨두고, 파일 서버가 스스로 폴링하며 소비한다
			// (ScheduleFileDeletionIfOwned() 참고). DB 삭제가 확정된 뒤,
			// 아직 DB 워커 스레드인 이 시점에서 Redis에 적어둔다 — Redis
			// 쓰기 자체도 비동기라 이 스레드를 오래 붙잡지 않는다.
			if( result == ELoginResult::Ok )
				ScheduleFileDeletionIfOwned(deletedImageRef);

			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedImageId, wasActive, deletedImageRef]()
				{
					if( onComplete )
						onComplete(result, completedImageId, wasActive, deletedImageRef);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_PROFILE_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_DeleteProfileImage,
		[&publicId, imageId, dispatchToJobQueue](ST_DELETE_PROFILE_IMAGE_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->imageId = imageId;
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, imageId, false, std::string());
	}
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

namespace
{
	// RecordRoomChatMessage()가 저장하는 필드 구분자 — 클래스 선언 밖(익명
	// 네임스페이스)에 둬서 RemoveRoomChatMessage()/RequestRoomChatHistory()와
	// 공유한다. 일반 텍스트/닉네임/URL에 나올 일이 없는 제어문자라 이스케이프가
	// 필요 없다.
	constexpr char kChatHistoryFieldDelim = '\x01';

	std::string BuildRoomChatOrderKey(int32 roomId) { return "RoomChatOrder:" + std::to_string(roomId); }
	std::string BuildRoomChatMsgKey(int32 roomId) { return "RoomChatMsg:" + std::to_string(roomId); }
}

//***************************************************************************
// @brief 방 대화 메시지 하나를 Redis에 기록한다.
//***************************************************************************
void CChatServerMain::RecordRoomChatMessage(
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& senderPublicId,
	const std::string& nickname,
	const std::string& profileImageUrl,
	const std::string& message,
	int64 messageId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	const std::string senderPublicIdHex = Crypto::CCryptoUtil::ToHex(senderPublicId.data(), senderPublicId.size());
	const int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();

	std::string serialized;
	serialized.reserve(senderPublicIdHex.size() + nickname.size() + profileImageUrl.size() + message.size() + 32);
	serialized += senderPublicIdHex;
	serialized += kChatHistoryFieldDelim;
	serialized += nickname;
	serialized += kChatHistoryFieldDelim;
	serialized += profileImageUrl;
	serialized += kChatHistoryFieldDelim;
	serialized += message;
	serialized += kChatHistoryFieldDelim;
	serialized += std::to_string(nowMs);

	const std::string messageIdStr = std::to_string(messageId);

	// ZADD RoomChatOrder:{roomId} {messageId} {messageId} — score와 member
	// 둘 다 messageId(문자열)로 준다. score는 정렬 기준(오름차순 발급되는
	// int64라 시간순과 정확히 일치), member는 조회 후 HMGET에 그대로 쓸 키.
	CVector<std::string> zaddArgs;
	zaddArgs.push_back("ZADD");
	zaddArgs.push_back(BuildRoomChatOrderKey(roomId));
	zaddArgs.push_back(messageIdStr);
	zaddArgs.push_back(messageIdStr);
	_redisService->SendCommand(zaddArgs, [](const RedisValue& /*res*/) {});

	// HSET RoomChatMsg:{roomId} {messageId} {serialized}
	CVector<std::string> hsetArgs;
	hsetArgs.push_back("HSET");
	hsetArgs.push_back(BuildRoomChatMsgKey(roomId));
	hsetArgs.push_back(messageIdStr);
	hsetArgs.push_back(serialized);
	_redisService->SendCommand(hsetArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 메시지 하나를 대화 기록에서 제거한다.
//***************************************************************************
void CChatServerMain::RemoveRoomChatMessage(int32 roomId, int64 messageId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	const std::string messageIdStr = std::to_string(messageId);

	CVector<std::string> zremArgs;
	zremArgs.push_back("ZREM");
	zremArgs.push_back(BuildRoomChatOrderKey(roomId));
	zremArgs.push_back(messageIdStr);
	_redisService->SendCommand(zremArgs, [](const RedisValue& /*res*/) {});

	CVector<std::string> hdelArgs;
	hdelArgs.push_back("HDEL");
	hdelArgs.push_back(BuildRoomChatMsgKey(roomId));
	hdelArgs.push_back(messageIdStr);
	_redisService->SendCommand(hdelArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 방의 대화 기록 전체를 지운다(방 삭제 시 호출).
//***************************************************************************
void CChatServerMain::ClearRoomChatHistory(int32 roomId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	// DEL은 여러 키를 한 번에 받으므로 한 커맨드로 두 키를 같이 지운다.
	CVector<std::string> delArgs;
	delArgs.push_back("DEL");
	delArgs.push_back(BuildRoomChatOrderKey(roomId));
	delArgs.push_back(BuildRoomChatMsgKey(roomId));
	_redisService->SendCommand(delArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 방의 최근 대화 기록(최대 1000개)을 오래된 순서로 조회한다.
// @details ZREVRANGE로 최신 messageId 1000개(최신순)를 얻은 뒤, 그 목록으로
//          HMGET을 한 번 더 호출해 내용을 한꺼번에 가져온다 — 두 Redis
//          왕복을 콜백 체이닝으로 순차 처리한다. 최종적으로 오래된 순서로
//          뒤집어서 돌려준다(자연스러운 채팅 로그 순서).
//***************************************************************************
void CChatServerMain::RequestRoomChatHistory(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	std::function<void(const std::vector<SChatHistoryEntry>& history)> onComplete)
{
	if( session == nullptr )
		return;

	if( roomId == kLobbyRoomId || _redisService == nullptr )
	{
		if( onComplete )
			onComplete(std::vector<SChatHistoryEntry>());
		return;
	}

	CJobQueueRef jobQueue = _jobQueue;
	const std::string msgKey = BuildRoomChatMsgKey(roomId);

	CVector<std::string> zrevrangeArgs;
	zrevrangeArgs.push_back("ZREVRANGE");
	zrevrangeArgs.push_back(BuildRoomChatOrderKey(roomId));
	zrevrangeArgs.push_back("0");
	zrevrangeArgs.push_back("999"); // 최근 1000개(0~999, inclusive)

	_redisService->SendCommand(zrevrangeArgs, [this, jobQueue, onComplete, msgKey](const RedisValue& orderRes)
		{
			CRedisResultSet orderSet(orderRes);
			if( orderSet.IsEmpty() )
			{
				if( jobQueue == nullptr )
					return;
				jobQueue->DoAsync([onComplete]()
					{
						if( onComplete )
							onComplete(std::vector<SChatHistoryEntry>());
					});
				return;
			}

			std::vector<std::string> messageIds;
			messageIds.reserve(orderSet.GetSize());
			std::string idStr;
			while( orderSet.GetData(idStr) )
				messageIds.push_back(idStr);

			// HMGET RoomChatMsg:{roomId} {id1} {id2} ... — 여러 messageId의
			// 내용을 한 번에 조회한다. 존재하지 않는 필드(그 사이 삭제된
			// 메시지)는 CRedisResultSet이 빈 문자열로 채워준다.
			CVector<std::string> hmgetArgs;
			hmgetArgs.push_back("HMGET");
			hmgetArgs.push_back(msgKey);
			for( const std::string& id : messageIds )
				hmgetArgs.push_back(id);

			_redisService->SendCommand(hmgetArgs, [jobQueue, onComplete, messageIds](const RedisValue& hmgetRes)
				{
					CRedisResultSet hmgetSet(hmgetRes);

					std::vector<SChatHistoryEntry> historyNewestFirst;
					historyNewestFirst.reserve(messageIds.size());

					for( size_t i = 0; i < messageIds.size(); ++i )
					{
						std::string serialized;
						if( !hmgetSet.GetData(serialized) || serialized.empty() )
							continue; // 그 사이 삭제된 메시지 — 건너뜀

						// "senderPublicId\x01nickname\x01profileImageUrl\x01message\x01timestampMs" 분해.
						std::vector<std::string> fields;
						size_t start = 0;
						for( size_t pos = 0; pos <= serialized.size(); ++pos )
						{
							if( pos == serialized.size() || serialized[pos] == kChatHistoryFieldDelim )
							{
								fields.push_back(serialized.substr(start, pos - start));
								start = pos + 1;
							}
						}

						if( fields.size() != 5 )
							continue; // 형식이 깨진 값 — 방어적으로 건너뜀

						SChatHistoryEntry entry;
						try
						{
							entry.messageId = std::stoll(messageIds[i]);
							entry.timestampMs = std::stoll(fields[4]);
						}
						catch( const std::exception& )
						{
							continue;
						}
						entry.senderPublicId = fields[0];
						entry.nickname = fields[1];
						entry.profileImageUrl = fields[2];
						entry.message = fields[3];

						historyNewestFirst.push_back(std::move(entry));
					}

					// 최신순으로 받았으니 자연스러운 채팅 로그 순서(오래된
					// 것부터)로 뒤집는다.
					std::vector<SChatHistoryEntry> historyOldestFirst(historyNewestFirst.rbegin(), historyNewestFirst.rend());

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, historyOldestFirst]()
						{
							if( onComplete )
								onComplete(historyOldestFirst);
						});
				});
		});
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

//***************************************************************************
// @brief 방을 새로 만듭니다. RequestChangeNickname()과 동일한 구조를
//        따르되, 성공 시 인메모리 레지스트리 등록이라는 부수효과가 하나
//        더 있다 — 그 등록은 순수 데이터 작업(세션/IOCP 객체를 안 건드림)
//        이라 DB 워커 스레드에서 바로 해도 안전하다(RequestDeleteProfileImage()의
//        ScheduleFileDeletionIfOwned() 호출과 동일한 선례).
//***************************************************************************
void CChatServerMain::RequestCreateRoom(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& ownerPublicId,
	const std::string& roomName,
	std::function<void(ERoomResult result, int32 newRoomId)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_CREATE_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_CreateRoom,
		[this, ownerPublicId, roomName, jobQueue, onComplete](ST_CREATE_ROOM_REQ* req)
		{
			::memcpy(req->ownerPublicId, ownerPublicId.data(), ownerPublicId.size());
			req->roomName = roomName;
			req->maxRoomsPerOwner = _maxRoomsPerOwner;

			req->onComplete = [this, ownerPublicId, roomName, jobQueue, onComplete](ERoomResult result, int32 newRoomId)
				{
					if( result == ERoomResult::Ok )
					{
						std::lock_guard<std::mutex> lock(_roomRegistryMutex);
						_roomRegistry[newRoomId] = SRoomInfo{ roomName, ownerPublicId };
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result, newRoomId]()
						{
							if( onComplete )
								onComplete(result, newRoomId);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError, 0);
	}
}

//***************************************************************************
// @brief 방을 삭제합니다.
// @details [설계] 성공하면 그 방에 남아있던 멤버 전원에게
//          DeleteRoomNotifyPacket을 브로드캐스트하고 로비로 옮긴 뒤,
//          레지스트리에서도 제거한다 — 패킷 핸들러가 멤버 목록에 접근할
//          방법이 없어서(그 정보는 이 클래스만 갖고 있음) 여기서 전담한다.
//          이 부수효과들은 DB 워커 스레드에서 실행되지만, CChatSession::Send()가
//          스레드 세이프(헤더 문서 참고)하고 MoveToRoom()/BroadcastToRoom()도
//          자체 락(_roomMutex)으로 보호되므로 안전하다.
//***************************************************************************
void CChatServerMain::RequestDeleteRoom(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_DeleteRoom,
		[this, roomId, requesterPublicId, jobQueue, onComplete](ST_DELETE_ROOM_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());

			req->onComplete = [this, roomId, jobQueue, onComplete](ERoomResult result)
				{
					if( result == ERoomResult::Ok )
					{
						std::vector<std::shared_ptr<CChatSession>> members;
						{
							std::lock_guard<std::mutex> lock(_roomMutex);
							auto it = _roomMembers.find(roomId);
							if( it != _roomMembers.end() )
							{
								members.reserve(it->second.size());
								for( auto& w : it->second )
									if( auto s = w.lock() )
										members.push_back(s);
							}
						}

						DeleteRoomNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::DeleteRoomNotify);
						notify.roomId = roomId;
						BroadcastToRoom(roomId, &notify, notify.size);

						// 남아있던 멤버 전원을 로비로 이동. MoveToRoom() 내부가
						// 이 방(oldRoomId==roomId)의 방장 이양 로직도 같이 타게
						// 되는데, 방 자체가 곧 레지스트리에서 삭제될 것이므로
						// (바로 아래) 그 로직은 "레지스트리에 이미 없음" 경로로
						// 빠져 아무 일도 안 한다 — 순서가 중요하다(레지스트리
						// 삭제를 먼저 하면 이 멤버 이동들이 다른 코드 경로를
						// 타서 예상과 달라질 수 있음).
						for( auto& memberSession : members )
						{
							int32 lobbyCount = 0;
							MoveToRoom(memberSession, kLobbyRoomId, lobbyCount);
						}

						{
							std::lock_guard<std::mutex> lock(_roomRegistryMutex);
							_roomRegistry.erase(roomId);
						}

						// [추가] 방 자체가 삭제됐으니 그 방의 대화 기록도
						// 같이 지운다 — "방이 없어질 때까지 유지"라는 요구를
						// 방이 사라지는 이 시점에 자연스럽게 만족시킨다.
						ClearRoomChatHistory(roomId);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief 방 이름을 바꿉니다. 성공 시 그 방 멤버 전원에게
//        RenameRoomNotifyPacket을 브로드캐스트한다.
//***************************************************************************
void CChatServerMain::RequestRenameRoom(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	const std::string& newName,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_RENAME_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_RenameRoom,
		[this, roomId, requesterPublicId, newName, jobQueue, onComplete](ST_RENAME_ROOM_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());
			req->newName = newName;

			req->onComplete = [this, roomId, newName, jobQueue, onComplete](ERoomResult result)
				{
					if( result == ERoomResult::Ok )
					{
						{
							std::lock_guard<std::mutex> lock(_roomRegistryMutex);
							auto it = _roomRegistry.find(roomId);
							if( it != _roomRegistry.end() )
								it->second.name = newName;
						}

						RenameRoomNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::RenameRoomNotify);
						notify.roomId = roomId;
						const size_t nameCopyLen = (std::min)(newName.size(), sizeof(notify.newName) - 1);
						::memcpy(notify.newName, newName.data(), nameCopyLen);
						BroadcastToRoom(roomId, &notify, notify.size);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief 존재하는 모든 방을 조회합니다. RequestListProfileImages()와
//        완전히 동일한 구조(부수효과 없음 — 순수 조회).
//***************************************************************************
void CChatServerMain::RequestListRooms(
	std::shared_ptr<CChatSession> session,
	std::function<void(ELoginResult result, const std::vector<SRoomListEntry>& rooms)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::vector<SRoomListEntry>& rooms)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] 항목마다 image_ref가 상대 경로로 저장돼 있으면 완전한
			// URL로 복원한다 — RequestListProfileImages()와 동일한 이유
			// (ToDisplayImageUrl() 참고).
			std::vector<SRoomListEntry> displayRooms = rooms;
			for( SRoomListEntry& entry : displayRooms )
				entry.imageRef = ToDisplayImageUrl(entry.imageRef);

			jobQueue->DoAsync([onComplete, result, displayRooms]()
				{
					if( onComplete )
						onComplete(result, displayRooms);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_ROOMS_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListRooms,
		[dispatchToJobQueue](ST_LIST_ROOMS_REQ* req)
		{
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
	}
}

//***************************************************************************
// @brief 방 프로필 이미지를 설정/교체/해제합니다.
// @details [설계] RequestDeleteRoom()과 동일한 구조 — 성공 시 부수효과
//          (이전 파일 삭제 예약 + 방 멤버 브로드캐스트)를 DB 콜백 안에서
//          바로 처리한다(RequestDeleteProfileImage()의
//          ScheduleFileDeletionIfOwned() 호출과 같은 선례).
//***************************************************************************
void CChatServerMain::RequestSetRoomImage(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	const std::string& newImageUrl,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// DB엔 상대경로로 저장 — RequestSetProfileImageUrl()과 동일한 이유
	// (ToStorableImageRef() 참고). 외부 URL이면 그대로 통과된다.
	const std::string storableUrl = ToStorableImageRef(newImageUrl);

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SET_ROOM_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SetRoomImage,
		[this, roomId, requesterPublicId, storableUrl, jobQueue, onComplete](ST_SET_ROOM_IMAGE_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());
			req->imageRef = storableUrl;

			req->onComplete = [this, roomId, storableUrl, jobQueue, onComplete](ERoomResult result, const std::string& oldImageRef)
				{
					if( result == ERoomResult::Ok )
					{
						// 1) 교체되기 전 이미지가 우리 파일 서버 소유였으면
						// 실제 파일 삭제를 예약한다. oldImageRef는 DB에
						// 저장돼 있던 그대로(상대경로 형태)라, 완전한 URL
						// 형태를 기대하는 ScheduleFileDeletionIfOwned()에
						// 넘기기 전에 먼저 ToDisplayImageUrl()로 복원한다.
						if( !oldImageRef.empty() )
							ScheduleFileDeletionIfOwned(ToDisplayImageUrl(oldImageRef));

						// 2) 그 방 멤버 전원에게 브로드캐스트.
						RoomImageChangedNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::RoomImageChangedNotify);
						notify.roomId = roomId;

						const std::string displayUrl = ToDisplayImageUrl(storableUrl);
						const size_t urlCopyLen = (std::min)(displayUrl.size(), sizeof(notify.imageUrl) - 1);
						::memcpy(notify.imageUrl, displayUrl.data(), urlCopyLen);

						BroadcastToRoom(roomId, &notify, notify.size);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief roomId가 실제로 존재하는 방인지(로비 포함) 확인합니다.
//***************************************************************************
bool CChatServerMain::RoomExists(int32 roomId) const
{
	if( roomId == kLobbyRoomId )
		return true;

	std::lock_guard<std::mutex> lock(_roomRegistryMutex);
	return _roomRegistry.find(roomId) != _roomRegistry.end();
}

//***************************************************************************
// @brief roomId를 떠난 사람이 그 방의 방장이었는지 확인하고, 맞으면
//        이양(남은 멤버가 있을 때) 또는 삭제(아무도 안 남았을 때)를 한다.
// @details [주의] 호출부(MoveToRoom()/LeaveCurrentRoom())가 이미 _roomMutex를
//          해제한 뒤에 불러야 한다 — 이 함수가 내부적으로
//          BroadcastToRoom()/MoveToRoom()(빈 방 정리 경로는 아니지만, 삭제
//          쪽은 RequestDeleteRoom()의 onComplete가 그걸 하므로 여기서는
//          아님)을 호출하지 않으므로 실제로는 재진입 위험이 없지만,
//          _roomRegistryMutex와 _roomMutex의 락 순서를 항상 "먼저 걸린 락을
//          풀고 다음 락을 건다"로 유지하기 위한 방어적 설계다.
//***************************************************************************
void CChatServerMain::HandleRoomOwnershipOnLeave(int32 roomId, const std::array<BYTE, kPublicIdBytes>& leavingPublicId,
	const std::vector<std::shared_ptr<CChatSession>>& remainingMembers)
{
	std::array<BYTE, kPublicIdBytes> currentOwner{};

	{
		std::lock_guard<std::mutex> lock(_roomRegistryMutex);
		auto it = _roomRegistry.find(roomId);
		if( it == _roomRegistry.end() )
			return; // 로비 등 방장 개념이 없는 위치

		currentOwner = it->second.ownerPublicId;
	}

	if( currentOwner != leavingPublicId )
		return; // 방장이 나간 게 아니면 할 일 없음

	if( remainingMembers.empty() )
	{
		// 아무도 안 남음 — 방 자체를 지운다. 레지스트리는 즉시(동기) 반영해
		// 그 사이 들어오는 RoomEnterReq가 이미 없는 방으로 정확히 처리되게
		// 하고, DB 삭제는 비동기로 뒤따라간다(실패해도 인메모리 관점에서는
		// 이미 없는 방이라 사용자 체감 문제는 없음 — 다만 로그는 남긴다).
		{
			std::lock_guard<std::mutex> lock(_roomRegistryMutex);
			_roomRegistry.erase(roomId);
		}

		// [추가] RequestDeleteRoom()과 동일한 이유 — 방이 없어지는 이
		// 경로(빈 방 자동 삭제)에서도 대화 기록을 같이 지운다.
		ClearRoomChatHistory(roomId);

		PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_ROOM_REQ>(
			MEMBER_DB_ASYNC,
			kDbCallIdent_DeleteRoom,
			[roomId, leavingPublicId](ST_DELETE_ROOM_REQ* req)
			{
				req->roomId = roomId;
				::memcpy(req->requesterPublicId, leavingPublicId.data(), leavingPublicId.size());
				req->onComplete = [roomId](ERoomResult result)
					{
						if( result != ERoomResult::Ok )
							LOG_ERROR(_T("HandleRoomOwnershipOnLeave: 빈 방(roomId=%d) 자동 삭제 DB 반영 실패(reason=%d) — 인메모리에서는 이미 삭제됨"),
								roomId, static_cast<int32>(result));
					};
			},
			kMaxDbQueueCapacity);

		return;
	}

	// 가장 오래 있었던 멤버(벡터 맨 앞 — _roomMembers가 erase-remove로
	// 삽입 순서를 유지하므로)에게 이양한다.
	std::shared_ptr<CChatSession> newOwnerSession = remainingMembers.front();
	const std::array<BYTE, kPublicIdBytes> newOwnerPublicId = newOwnerSession->GetPublicId();
	const std::string newOwnerNickname = newOwnerSession->GetNickname();

	{
		std::lock_guard<std::mutex> lock(_roomRegistryMutex);
		auto it = _roomRegistry.find(roomId);
		if( it != _roomRegistry.end() )
			it->second.ownerPublicId = newOwnerPublicId;
	}

	PushDBAsyncRequest<COdbcAsyncSrv, ST_TRANSFER_ROOM_OWNER_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_TransferRoomOwner,
		[roomId, newOwnerPublicId](ST_TRANSFER_ROOM_OWNER_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->newOwnerPublicId, newOwnerPublicId.data(), newOwnerPublicId.size());
			req->onComplete = [roomId](bool success)
				{
					if( !success )
						LOG_ERROR(_T("HandleRoomOwnershipOnLeave: 방장 이양(roomId=%d) DB 반영 실패 — 인메모리는 이미 반영됨"), roomId);
				};
		},
		kMaxDbQueueCapacity);

	RoomOwnerChangedNotifyPacket notify{};
	notify.size = sizeof(notify);
	notify.type = static_cast<uint16>(EChatPacketType::RoomOwnerChangedNotify);
	notify.roomId = roomId;
	const size_t nicknameCopyLen = (std::min)(newOwnerNickname.size(), sizeof(notify.newOwnerNickname) - 1);
	::memcpy(notify.newOwnerNickname, newOwnerNickname.data(), nicknameCopyLen);

	BroadcastToRoom(roomId, &notify, notify.size);
}

//***************************************************************************
// @brief 서버 시작 시 DB에 저장된 모든 방을 인메모리 레지스트리로 읽어들인다.
// @details Crypto::CCryptoUtil::FromHex()(CryptoUtil.h 확인 완료 —
//          ToHex()의 정확한 역방향, 16진 문자열 -> 원본 바이트, 실패 시
//          false 반환)로 DB의 16진 owner_public_id 문자열을 다시 바이트로
//          복원한다. 형식이 깨진 값(있어서는 안 되지만 방어적으로)이면
//          그 방 하나만 건너뛰고 나머지는 계속 로딩한다.
//***************************************************************************
void CChatServerMain::LoadRoomRegistryFromDb()
{
	PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_ROOMS_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListRooms,
		[this](ST_LIST_ROOMS_REQ* req)
		{
			req->onComplete = [this](ELoginResult result, const std::vector<SRoomListEntry>& rooms)
				{
					if( result != ELoginResult::Ok )
					{
						LOG_ERROR(_T("LoadRoomRegistryFromDb: 방 목록 초기 로딩 실패"));
						return;
					}

					std::lock_guard<std::mutex> lock(_roomRegistryMutex);
					int32 loadedCount = 0;
					for( const SRoomListEntry& entry : rooms )
					{
						std::array<BYTE, kPublicIdBytes> ownerPublicId{};
						if( !Crypto::CCryptoUtil::FromHex(entry.ownerPublicId, ownerPublicId.data(), ownerPublicId.size()) )
						{
							LOG_ERROR(_T("LoadRoomRegistryFromDb: roomId=%d의 owner_public_id 16진 디코딩 실패 — 이 방은 건너뜀"), entry.roomId);
							continue;
						}

						_roomRegistry[entry.roomId] = SRoomInfo{ entry.name, ownerPublicId };
						++loadedCount;
					}

					LOG_INFO(_T("LoadRoomRegistryFromDb: 방 %d개 로딩 완료"), loadedCount);
				};
		},
		kMaxDbQueueCapacity);
}