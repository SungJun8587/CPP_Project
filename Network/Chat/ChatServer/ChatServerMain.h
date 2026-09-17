
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
#include "DBListProfileImagesRequest.h"

#include <string>
#include <memory>
#include <functional>
#include <array>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>

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
//        DECLARE_DBASYNC_HANDLER_EX가 정적 초기화 시점에 자동 등록 —
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
			const std::array<BYTE, kTokenBytes>& newToken,
			const std::string& profileImageUrl)> onComplete);

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
	// @brief 로그인된 계정(publicId로 식별)의 프로필 이미지 URL 설정을 DB
	//        비동기 워커에 요청합니다. RequestChangeNickname()과 동일한 구조.
	// @details [설계] 이미지 파일 자체는 채팅 서버를 거치지 않는다 — url은
	//          항상 실제 접근 가능한 URL이다(파일 서버가 업로드 완료 후
	//          돌려준 주소, 또는 사용자가 직접 지정한 외부 URL/CDN 주소).
	//          user_profile_images에 새 행으로 등록하고 대표로 지정한다.
	// @param newUrl 빈 문자열이면 "프로필 이미지 해제"로 처리된다.
	//***************************************************************************
	void RequestSetProfileImageUrl(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newUrl,
		std::function<void(ELoginResult result,
			const std::array<BYTE, kPublicIdBytes>& publicId,
			const std::string& newUrl,
			int64 newImageId)> onComplete);

	//***************************************************************************
	// @brief 파일 서버 업로드용 임시 토큰을 발급합니다.
	// @details [설계] 이미지 바이트는 채팅 서버를 거치지 않는다 — 이 서버는
	//          "지금 이 계정이 파일 서버에 업로드해도 되는 상태"라는 것만
	//          짧게 보증하는 토큰을 Redis에 등록해서 발급한다
	//          ("UploadToken:{tokenHex}" 키, 값은 이 계정의 public_id 16진,
	//          TTL 60초). 파일 서버는 채팅 서버에 직접 물어보지 않고, 이
	//          Redis 키를 조회해서 토큰을 검증한다 — 두 서버는 서로 직접
	//          통신하지 않고 Redis라는 공유 인프라를 통해서만 간접 조율한다.
	//          업로드가 끝나면 클라이언트는 파일 서버가 돌려준 URL을
	//          RequestSetProfileImageUrl()로 다시 채팅 서버에 등록한다.
	// @param onComplete 성공 시 발급된 토큰(16진 문자열)과 클라이언트가
	//        접속할 파일 서버 주소를 돌려준다. 파일 서버 주소는 설정 파일
	//        (CServerConfig)에서 읽은 값을 그대로 전달한다.
	//***************************************************************************
	void RequestUploadToken(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		std::function<void(bool success, const std::string& uploadToken, const std::string& fileServerUrl)> onComplete);

	//***************************************************************************
	// @brief 이 서버가 클라이언트에게 알려줄 파일 서버 주소를 설정합니다.
	//        Start() 이후(또는 이전) 아무 때나 호출 가능 — RequestUploadToken()
	//        응답에 그대로 실려 나간다. 설정 안 하면 빈 문자열이 나가고,
	//        클라이언트는 그 경우 업로드 기능을 못 씀을 알 수 있다.
	//***************************************************************************
	void SetFileServerUrl(std::string fileServerUrl) { _fileServerUrl = std::move(fileServerUrl); }

	//***************************************************************************
	// @brief 로그인된 계정이 갖고 있는 프로필 이미지 전체 목록을 조회합니다.
	//***************************************************************************
	void RequestListProfileImages(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		std::function<void(ELoginResult result, const std::vector<SProfileImageEntry>& images)> onComplete);

	//***************************************************************************
	// @brief 갤러리에 이미 있는 이미지 하나를 대표로 지정합니다. 성공하면
	//        onComplete로 그 이미지의 image_ref도 같이 돌려준다 — 호출부가
	//        세션의 표시용 값을 곧바로 갱신할 수 있게(다시 조회할 필요 없이).
	//***************************************************************************
	void RequestSelectProfileImage(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		int64 imageId,
		std::function<void(ELoginResult result, int64 imageId, const std::string& selectedImageRef)> onComplete);

	//***************************************************************************
	// @brief 갤러리에서 이미지 하나를 삭제합니다.
	// @details [설계] DB 행만 지운다 — 실제 파일 삭제는 이 서버의 책임이
	//          아니다(파일 서버 소관). local: 참조 방식을 쓰던 시절에는
	//          여기서 실제 파일도 같이 지웠지만, 이미지 저장을 파일 서버로
	//          분리하면서 이 서버는 DB 레코드 관리만 담당한다.
	// @param wasActive [콜백 파라미터] 지운 이미지가 대표였으면 true — 호출부가
	//        이걸 보고 세션의 표시용 프로필 이미지 값을 비워야 하는지 판단한다.
	//***************************************************************************
	void RequestDeleteProfileImage(
		std::shared_ptr<CChatSession> session,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		int64 imageId,
		std::function<void(ELoginResult result, int64 imageId, bool wasActive, const std::string& deletedImageRef)> onComplete);

	//***************************************************************************
	// @brief deletedImageRef가 이 서버가 알고 있는 파일 서버(_fileServerUrl)
	//        소유의 참조("{fileServerUrl}/images/{경로}")면, 실제 파일 정리를
	//        위해 그 상대 경로를 Redis 큐("FileServer:PendingDeletions")에
	//        넣는다. 외부 URL이면(우리 파일 서버 소유가 아니면) 조용히 무시.
	// @details [설계] 채팅 서버는 파일 서버에 직접 요청을 보내지 않는다 —
	//          Redis 리스트에 "지울 것"만 적어두고, 파일 서버가 스스로
	//          폴링하며 소비한다(FileServerMain::PollPendingDeletions() 참고).
	//          두 서버가 서로 몰라도 되는 구조를 유지하기 위함.
	//***************************************************************************
	void ScheduleFileDeletionIfOwned(const std::string& deletedImageRef);

	//***************************************************************************
	// @brief [추가] 채팅 메시지 삭제 결과 사유.
	//***************************************************************************
	enum class EDeleteMessageResult : uint8
	{
		Ok = 0,
		NotFound = 1,	// messageId가 추적 창(최근 N개)을 벗어났거나 애초에 존재한 적 없음
		NotOwner = 2,	// 요청자가 이 메시지의 작성자가 아님
	};

	//***************************************************************************
	// @brief [추가] 새로 브로드캐스트할 채팅 메시지에 부여할 다음 고유 ID를
	//        발급하고 삭제 검증용 추적 목록에 등록합니다. ChatMessageHandler.cpp가
	//        브로드캐스트 직전에 호출합니다.
	// @details 등록된 개수가 kMaxTrackedMessages를 넘으면 가장 오래된
	//          항목을 하나 제거합니다(메모리가 무한정 늘어나지 않도록) —
	//          채팅 로그를 영구 보관하는 기능이 아니라 "방금 보낸 메시지
	//          취소" 수준의 가벼운 기능으로 설계했다.
	// @param senderPublicId 이 메시지를 보낸 계정의 안정 식별자(소유권 검증용)
	// @param roomId 이 메시지가 브로드캐스트되는 방(나중에 삭제 알림도
	//        같은 방으로 보내야 하므로 여기서 같이 기억해둔다)
	// @return 새로 발급된 메시지 ID(1부터 시작하는 단조 증가값)
	//***************************************************************************
	int64 RegisterOutgoingMessage(const std::array<BYTE, kPublicIdBytes>& senderPublicId, int32 roomId);

	//***************************************************************************
	// @brief [추가] 메시지 삭제를 시도합니다 — 존재 여부와 소유권을 확인한
	//        뒤, 성공하면 추적 목록에서 제거하고 원래 브로드캐스트됐던 방
	//        번호를 돌려줍니다(호출부가 그 방으로 삭제 알림을 브로드캐스트
	//        하기 위함).
	// @param messageId 지울 메시지 ID
	// @param requesterPublicId 요청자의 안정 식별자 — 메시지 소유자와
	//        일치해야 삭제가 허용된다.
	// @param outRoomId [out] 성공 시(Ok)만 유효 — 이 메시지가 원래 있던 방 번호.
	// @return EDeleteMessageResult (성공은 Ok)
	//***************************************************************************
	EDeleteMessageResult TryDeleteMessage(int64 messageId, const std::array<BYTE, kPublicIdBytes>& requesterPublicId, int32& outRoomId);

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
	// @details [수정 — 접근 지정자 버그] 이 함수는 원래 public이어야 하는데
	//          (ServerUserCountHandler.cpp가 외부에서 호출), 편집 과정에서
	//          바로 아래 private: 구역에 잘못 끼어들어가 있었다. "'private
	//          멤버에 액세스할 수 없습니다'" 컴파일 에러의 원인이었다.
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

	//***************************************************************************
	// @brief [추가] 메시지 삭제 기능을 위한 최근 메시지 추적 항목.
	//***************************************************************************
	struct SMessageOwnerRecord
	{
		std::array<BYTE, kPublicIdBytes>	senderPublicId;
		int32								roomId = -1;
	};

	// [추가] 최근 메시지 추적(삭제 기능용) — 서버 전체를 통틀어 최근
	// kMaxTrackedMessages개까지만 기억한다(무한정 쌓이는 걸 방지). 그보다
	// 오래된 메시지는 삭제 요청이 와도 NotFound로 처리된다 — 채팅 로그를
	// DB 등에 영구 보관하는 기능이 필요하면 별도로 설계해야 하고, 이건
	// 어디까지나 "방금 보낸 메시지 취소" 수준의 가벼운 기능이다.
	static constexpr size_t kMaxTrackedMessages = 500;

	mutable std::mutex							_messageOwnerMutex;
	std::unordered_map<int64, SMessageOwnerRecord>	_messageOwners;
	std::deque<int64>							_messageOwnerOrder;	// 삽입 순서 — 오래된 것부터 제거하기 위함
	std::atomic<int64>							_nextMessageId{ 1 };

	// [설계 변경] 프로필 이미지 저장을 별도 파일 서버로 분리하면서, 이
	// 서버는 더 이상 이미지 파일 자체를 갖고 있지 않는다 — IImageStorage/
	// 업로드 세션 상태(SUploadSession 등)를 전부 제거했다. 대신 클라이언트가
	// 파일 서버에 접속할 때 쓸 주소만 들고 있는다(SetFileServerUrl() 참고).
	std::string	_fileServerUrl;
};

#endif // ndef UC_CHATSERVERMAIN_H