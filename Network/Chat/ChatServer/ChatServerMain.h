
//***************************************************************************
// ChatServerMain.h : interface for the CChatServerMain class.
//
//***************************************************************************

#ifndef UC_CHATSERVERMAIN_H
#define UC_CHATSERVERMAIN_H

#include <ServerConnectInfo.h>		
#include <Crypto/CryptoUtil.h>
#include <Network/NetworkCommon.h>
#include <Redis/RedisService.h>
#include <Redis/RedisServerHeartbeat.h>
#include <DB/OdbcAsyncSrv.h>
#include "ChatPacket.h"

#include <string>
#include <memory>
#include <functional>
#include <array>
#include <unordered_map>
#include <vector>
#include <mutex>

class CChatSession;

//***************************************************************************
// @class CChatServerMain
// @brief IOCP 서버 서비스 + Redis(로그인 상태/서버 하트비트) + DB(회원가입/재접속)를
//        함께 구동하는 채팅 서버 파사드.
// @details
// 역할:
//     1. CIocpServerService 구동 (CNetworkFactory를 통해 생성)
//     2. CRedisService 초기화 및 CRedisServerHeartbeat로 서버 생존 신고
//     3. 회원 DB(ODBC) 초기화 — CDbServiceManager::Instance().MemberDB()
//        (MEMBER_DB_ASYNC)가 도메인별 COdbcAsyncSrv 인스턴스를 소유한다.
//        회원가입/재접속 토큰 검증 핸들러는 AccountDBHandler.cpp의
//        DECLARE_DBASYNC_HANDLER_VIA가 정적 초기화 시점에 자동 등록 —
//        이 클래스가 별도로 소유/등록하지 않음. 다만 실제 DB 접속/워커
//        스레드 기동(StartService())은 이 클래스의 Start()가 담당한다.
//     4. CChatSession으로부터 로그인/로그아웃/브로드캐스트 요청을 위임받아 처리
//
// [설계 변경 — uid/public_id 분리] users.nickname이 더 이상 PRIMARY KEY가
// 아니다(create_chat_db.sql 참고). 이 클래스의 Redis 키(BuildUserKey())/
// 로그인·로그아웃 기록은 이제 nickname이 아니라 public_id(계정의 안정
// 식별자, 닉네임 변경과 무관하게 고정)를 기준으로 한다 — 그래서 예전에
// 있던 OnUserNicknameChanged()(닉네임 변경 시 Redis 키를 RENAME하던 함수)가
// 더 이상 필요 없어져 제거했다.
//
// 소유 순서(Start()에서의 생성 순서, Stop()에서는 역순 정리):
//     _iocpCore/_jobQueue → _redisService → MEMBER_DB_ASYNC(CDbServiceManager가
//     소유 — 이 클래스는 StartService()만 호출하고 소유권을 갖지 않음) →
//     _service(IOCP) → _heartbeat
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
	// @param dbNodeVec 회원 DB 접속 정보(ODBC) — MEMBER_DB_ASYNC.StartService()에 그대로 전달
	// @param dbMaxThreadCnt DB 비동기 워커 스레드 수 (0=자동 — [가정] StartService 내부 정책)
	// @param serverGroupId/serverChannelId 하트비트 등록에 쓰일 식별자 (예: "ChatServer", "1")
	// @param maxSessionCount 최대 동시 접속 수
	// @param workerThreadCount IOCP 워커 스레드 개수 (0=자동)
	// @param heartbeatTtlSec/heartbeatIntervalSec 하트비트 TTL/갱신 주기
	// @return 모든 초기화 단계가 성공하면 true
	//***************************************************************************
	bool Start(
		const _tstring& bindIp, uint16 bindPort,
		CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
		CVector<CDBNode> dbNodeVec, int32 dbMaxThreadCnt,
		std::string serverName, std::string serverGroupId, std::string serverChannelId,
		int32 maxSessionCount = 1000, uint32 workerThreadCount = 0,
		int32 heartbeatTtlSec = 15, int32 heartbeatIntervalSec = 5);

	//***************************************************************************
	// @brief 채팅 서버를 정지합니다. 하트비트 → IOCP 서비스 순으로 정리합니다.
	// @details MEMBER_DB_ASYNC(CDbServiceManager 소유)는 이 클래스가
	//          정지시키지 않습니다 — 다른 서버 모듈이 같이 쓰고 있을 수
	//          있는 프로세스 전역 자원이라 소유권 밖입니다. 실제 정지는
	//          프로세스 종료 시 CDbServiceManager::Instance().ShutdownAll()
	//          로 한 번에 처리합니다(ChatServer.cpp::MainClose() 참고).
	//***************************************************************************
	void Stop();

public:
	// CChatSession/핸들러에서 호출하는 콜백들
	void OnUserLogin(const std::array<BYTE, kPublicIdBytes>& publicId);
	void OnUserLogout(const std::array<BYTE, kPublicIdBytes>& publicId);

	void Broadcast(const void* data, uint16 size);

	//***************************************************************************
	// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true)을
	//        DB 비동기 워커에 요청합니다.
	// @param session 요청을 보낸 세션 (완료 시 콜백에서 weak_ptr로 안전하게 재확인)
	// @param nickname hasToken==false일 때만 의미: 요청된 닉네임(형식 검증은
	//        AccountDBHandler.cpp가 다시 한번 수행)
	// @param hasToken true면 publicId+token으로 재접속 검증, false면 신규 가입 시도
	// @param publicId hasToken==true일 때만 의미: 재접속 대상 계정의 안정 식별자
	// @param token 재접속 토큰 원문(hasToken==false면 무시됨)
	// @param onComplete DB 워커 스레드에서 호출되는 완료 콜백(내부적으로 JobQueue로
	//        이관됨) — 세션/IOCP 객체를 건드리는 코드는 CChatSession의 public API로만
	//        수행할 것. 성공 시 반환되는 publicId는 hasToken==false면 새로 발급된
	//        값, hasToken==true면 요청에 실었던 값을 그대로 echo.
	//***************************************************************************
	void RequestSignup(
		std::shared_ptr<CChatSession> session,
		const std::string& nickname,
		bool hasToken,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& token,
		std::function<void(ELoginResult result, const std::string& nickname,
			const std::array<BYTE, kPublicIdBytes>& publicId,
			const std::array<BYTE, kTokenBytes>& newToken)> onComplete);

	//***************************************************************************
	// @brief 로그인된 계정(publicId로 식별)의 닉네임 변경을 DB 비동기 워커에 요청합니다.
	// @param session 요청을 보낸 세션(로그인 상태여야 함 — 호출부가 사전 확인)
	// @param publicId 대상 계정의 안정 식별자(세션의 GetPublicId())
	// @param newNickname 바꿀 닉네임 (형식 검증은 ChangeNicknameDBHandler.cpp가 다시 수행)
	// @param onComplete DB 워커 스레드 완료 콜백(JobQueue로 이관됨). 성공 시에도
	//        세션의 표시용 닉네임 갱신은 이 함수가 아니라 콜백을 소비하는 쪽
	//        (ChatChangeNicknameHandler.cpp)의 책임이다. publicId 자체는 이
	//        요청으로 절대 바뀌지 않는다.
	//***************************************************************************
	void RequestChangeNickname(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newNickname,
		std::function<void(ELoginResult result,
			const std::array<BYTE, kPublicIdBytes>& publicId,
			const std::string& newNickname)> onComplete);

	//***************************************************************************
	// @brief 세션을 지정한 위치(로비 또는 특정 룸)로 옮깁니다.
	// @details 로그인 성공 직후 자동으로 로비(kLobbyRoomId)에 배정하는 데도
	//          쓰이고, 클라이언트의 명시적 방 입장 요청(RoomEnterHandler.cpp)
	//          처리에도 쓰인다 — "로비도 결국 하나의 방"이라는 관점이라 두
	//          경우를 같은 함수로 통일했다.
	//          기존에 있던 방/로비에서는 자동으로 빠지고, 새 위치의 멤버
	//          목록에 추가된다. 이동 전/후 두 위치(같은 곳이 아니라면 둘 다)의
	//          갱신된 인원수를 그 방에 남아있는 다른 사람들에게 알림
	//          (NotifyRoomUserCount() 참고) — 그래야 이미 그 방에 있던
	//          사람들의 "방 인원수" 표시도 실시간으로 갱신된다.
	// @param session 이동할 세션
	// @param newRoomId 이동할 위치 — kLobbyRoomId 또는 1~kMaxRoomId
	// @param outNewRoomUserCount [out] 이동 직후 newRoomId의 인원수(자신 포함)
	//***************************************************************************
	void MoveToRoom(std::shared_ptr<CChatSession> session, int32 newRoomId, int32& outNewRoomUserCount);

	//***************************************************************************
	// @brief 세션이 지금 있는 방(로비 포함)에서만 빠집니다 — 어디로도 새로
	//        옮기지 않습니다. 연결 종료(OnDisconnected()) 전용 — 세션이
	//        곧 사라지므로 "새 위치로 옮긴다"는 개념 자체가 필요 없다.
	// @details shared_ptr이 아니라 raw pointer를 받는다 — 소멸 과정
	//          중(OnDisconnected() 내부)에 shared_from_this()를 새로 만드는
	//          부담/위험을 피하기 위함(이미 다른 곳에서도 이 시점엔 순수
	//          데이터만 넘기는 관례 — OnUserLogout()도 동일).
	//***************************************************************************
	void LeaveCurrentRoom(CChatSession* session);

	//***************************************************************************
	// @brief 방(로비 포함)의 현재 인원수를 조회합니다. 존재하지 않거나
	//        빈 방이면 0을 반환합니다.
	//***************************************************************************
	int32 GetRoomUserCount(int32 roomId) const;

	//***************************************************************************
	// @brief 지정한 방(로비 포함)에 있는 세션들에게만 브로드캐스트합니다.
	// @details ChatMessageHandler.cpp가 채팅 메시지를 "발신자가 지금 있는
	//          방"으로만 좁혀 보낼 때 사용한다 — Broadcast()(전체 브로드캐스트)와
	//          달리 이건 방 멤버십 기준으로 대상을 제한한다.
	//***************************************************************************
	void BroadcastToRoom(int32 roomId, const void* data, uint16 size);

	//***************************************************************************
	// @brief 이 서버 프로세스의 현재 전체 접속자 수(TCP 연결 기준, 로그인
	//        여부 무관)를 반환합니다.
	// @details [설계 변경] 예전엔 이 값이 바뀔 때마다 전체 세션에게 자발적으로
	//          브로드캐스트했지만(NotifyServerUserCount()), 지금은 클라이언트가
	//          ServerUserCountReq로 주기적으로 물어보면 그 시점의 값을 그대로
	//          돌려주는 폴링 방식으로 바꿨다 — ServerUserCountHandler.cpp가
	//          이 함수를 호출해 응답을 만든다.
	//***************************************************************************
	int32 GetServerUserCount() const;

private:
	std::string BuildUserKey(const std::array<BYTE, kPublicIdBytes>& publicId) const;

	//***************************************************************************
	// @brief roomId의 갱신된 인원수를 그 방의 멤버 전원에게 알립니다.
	// @details MoveToRoom()/LeaveCurrentRoom()이 인원수 변경 시 내부적으로
	//          호출한다 — 직접 호출할 일 없음.
	//***************************************************************************
	void NotifyRoomUserCount(int32 roomId, int32 userCount);

private:
	CIocpCoreRef							_iocpCore;
	CJobQueueRef							_jobQueue;
	std::unique_ptr<CRedisService>			_redisService;
	CIocpServerServiceRef					_service;
	std::unique_ptr<CRedisServerHeartbeat>	_heartbeat;

	std::string	_serverName;
	std::string	_serverGroupId;
	std::string	_serverChannelId;

	// 방(로비 포함) 멤버십 레지스트리. CIocpSessionManager(프레임워크)를
	// 건드리지 않고 채팅 애플리케이션 계층에서 자체 관리한다 — "방" 개념은
	// IOCP 세션 관리 자체와 무관한, 순전히 채팅 서버만의 개념이기 때문이다.
	// weak_ptr로 보관해 세션이 끊겨도 이 맵이 소유권을 붙들지 않게 한다
	// (끊긴 세션은 순회 시점에 lock() 실패로 자연스럽게 걸러지고, MoveToRoom()/
	// LeaveCurrentRoom() 호출 시 청소된다).
	mutable std::mutex	_roomMutex;
	std::unordered_map<int32, std::vector<std::weak_ptr<CChatSession>>>	_roomMembers;
};

#endif // ndef UC_CHATSERVERMAIN_H