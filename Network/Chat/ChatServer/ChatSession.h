
//***************************************************************************
// ChatSession.h : interface for the CChatSession class.
//
//***************************************************************************

#ifndef CHATSESSION_H
#define CHATSESSION_H

#include <Network/IOCP/IocpSession.h>

#include <string>
#include <array>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_set>

class CChatServerMain;

//***************************************************************************
// @class CChatSession
// @brief CIocpSession을 상속받는 채팅 서버 전용 세션.
// @details
// 로그인 상태(_loggedIn/_publicId/_nickname)는 이 세션 객체 안에서만 관리하고,
// 실제 Redis 반영(등록/삭제)은 CChatServerMain에 위임합니다 — 세션은 프로토콜
// 처리와 자기 상태만 알고, 인프라(Redis) 연동은 서버 파사드가 전담하는 구조입니다.
//
// [설계 변경 — 아이덴티티/표시명 분리] users.nickname이 더 이상 PRIMARY
// KEY가 아니게 되면서(create_chat_db.sql 참고), 세션의 "진짜 정체성"과
// "화면에 보이는 이름"이 서로 다른 값이 됐다:
//   - _publicId : 로그인 성공 시 서버가 확정하는 안정 식별자. 닉네임이
//     바뀌어도 절대 안 바뀐다 — Redis 키/DB 조회는 전부 이 값 기준.
//   - _nickname : 표시용. ChatAccountHandler.cpp가 언제든 바꿀 수
//     있고, 그래도 _publicId/Redis 키/세션 자체는 전혀 영향받지 않는다.
//***************************************************************************
class CChatSession : public CIocpSession
{
public:
	explicit CChatSession(CChatServerMain* server);
	virtual ~CChatSession() = default;

protected:
	// CIocpSession의 상위 콘텐츠 레이어 훅 오버라이드
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

public:
	//***************************************************************************
	// @brief 현재 로그인 상태를 반환합니다.
	//***************************************************************************
	bool IsLoggedIn() const { return _loggedIn.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 로그인한 계정의 안정 식별자를 반환합니다(로그인 전이면 전부 0).
	// @details 닉네임 변경과 무관하게 고정 — Redis 키/DB 조회는 이 값을 쓴다.
	//***************************************************************************
	const std::array<BYTE, kPublicIdBytes>& GetPublicId() const { return _publicId; }

	//***************************************************************************
	// @brief 로그인한 유저의 표시용 닉네임을 반환합니다(로그인 전이면 빈 문자열).
	// @details ChatAccountHandler.cpp가 성공적으로 바꾸면 이 값만
	//          갱신된다 — GetPublicId()는 영향받지 않는다.
	//***************************************************************************
	// @details 다른 스레드(DB 콜백·브로드캐스트)가 동시에 갱신할 수 있으므로 값으로 반환한다.
	std::string GetNickname() const
	{
		std::lock_guard<std::mutex> lock(_profileMutex);
		return _nickname;
	}

	//***************************************************************************
	// @brief 로그인한 계정의 프로필 이미지 URL을 반환합니다(미설정이면 빈 문자열).
	//***************************************************************************
	std::string GetProfileImageUrl() const
	{
		std::lock_guard<std::mutex> lock(_profileMutex);
		return _profileImageUrl;
	}

	//***************************************************************************
	// @brief 이 세션이 속한 채팅 서버를 반환합니다.
	//***************************************************************************
	CChatServerMain* GetServer() const { return _server; }

	//***************************************************************************
	// @brief 로그인 완료 상태를 세션에 반영합니다.
	// @details [설계 노트] 자체 등록형 패킷 핸들러가 별도 파일의 자유 함수로
	//          분리되면서 더 이상 CChatSession의 private 멤버에 직접 접근할 수
	//          없다 — 그 대가로 캡슐화를 "핸들러가 필요로 하는 최소한의 public
	//          API"로 한 단계 완화했다. 이 메서드는 로그인 처리 핸들러
	//          (ChatAccountHandler.cpp)만 호출하는 것을 의도한 좁은 용도의 API이며,
	//          컴파일러가 강제하지는 못하므로 컨벤션으로 지킨다.
	//***************************************************************************
	void MarkLoggedIn(const std::array<BYTE, kPublicIdBytes>& publicId, std::string nickname, std::string profileImageUrl)
	{
		{
			std::lock_guard<std::mutex> lock(_profileMutex);
			_nickname = std::move(nickname);
			_profileImageUrl = std::move(profileImageUrl);
		}
		_publicId = publicId;
		// _publicId/프로필을 먼저 쓰고 release로 공개한다 — IsLoggedIn()이 true를 본 스레드는 값을 안전하게 읽는다.
		_loggedIn.store(true, std::memory_order_release);
	}

	//***************************************************************************
	// @brief 닉네임 변경 성공 후 세션의 표시용 닉네임만 갱신합니다.
	// @details MarkLoggedIn()과 동일한 좁은 용도 API — ChatAccountHandler.cpp만
	//          호출하는 것을 의도한다. _publicId/로그인 상태는 건드리지
	//          않는다(닉네임 변경은 계정 식별자에 영향을 주지 않으므로).
	//***************************************************************************
	void UpdateNickname(std::string newNickname)
	{
		std::lock_guard<std::mutex> lock(_profileMutex);
		_nickname = std::move(newNickname);
	}

	//***************************************************************************
	// @brief 프로필 이미지 URL 변경 성공 후 세션의 값만 갱신합니다.
	// @details UpdateNickname()과 동일한 좁은 용도 API —
	//          ChatProfileImageHandler.cpp만 호출하는 것을 의도한다.
	//***************************************************************************
	void UpdateProfileImageUrl(std::string newUrl)
	{
		std::lock_guard<std::mutex> lock(_profileMutex);
		_profileImageUrl = std::move(newUrl);
	}

	//***************************************************************************
	// @brief 현재 있는 위치(로비=kLobbyRoomId 또는 특정 룸 ID)를 반환합니다.
	// @details 로그인 전이거나 아직 로비 배정도 안 된 극히 짧은 순간에는 -1
	//          (어디에도 속하지 않음)일 수 있다 — CChatServerMain::LeaveCurrentRoom()이
	//          이 값을 보고 "정리할 방이 없음"을 판단한다.
	//***************************************************************************
	int32 GetRoomId() const { return _roomId.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 현재 위치를 갱신합니다.
	// @details [설계 노트] MarkLoggedIn()과 동일한 좁은 용도 API — CChatServerMain::MoveToRoom()만
	//          호출하는 것을 의도한다. 세션 스스로 방을 옮기는 게 아니라,
	//          방 멤버십을 총괄하는 CChatServerMain의 결정을 세션에 반영만
	//          하는 역할이다(실제 멤버십 목록 갱신은 CChatServerMain 쪽 책임).
	//***************************************************************************
	void SetRoomId(int32 roomId) { _roomId.store(roomId, std::memory_order_release); }

	//***************************************************************************
	// @brief 채팅성 메시지 한 건을 보내도 되는지 판정합니다(초당 kMaxMessagesPerSec건 고정 윈도우).
	// @details 세션의 수신 처리(OnRecv → 핸들러)는 세션당 직렬이라 별도 동기화가 필요 없다.
	//          한도를 넘은 메시지는 호출부가 조용히 버린다.
	//***************************************************************************
	bool AllowMessage()
	{
		const int64 nowMs = NowMs();
		if( nowMs - _rateWindowStartMs >= 1000 )
		{
			_rateWindowStartMs = nowMs;
			_rateCount = 0;
		}
		return ++_rateCount <= kMaxMessagesPerSec;
	}

	//***************************************************************************
	// @brief 로그인 요청을 시작해도 되는지 판정하고, 되면 "진행 중"으로 표시합니다.
	// @details 한 세션에서 LoginReq가 완료되기 전에 또 오면 가입이 중복 실행되거나 한 세션이
	//          여러 계정으로 등록될 수 있어, 진행 중이거나 이미 로그인했으면 거부한다.
	//***************************************************************************
	bool TryBeginLogin()
	{
		if( IsLoggedIn() )
			return false;
		return !_loginPending.exchange(true, std::memory_order_acq_rel);
	}

	//***************************************************************************
	// @brief 로그인이 실패로 끝났음을 반영합니다(다시 시도 가능). 실패가 kMaxLoginFailures회에 이르면 true.
	//***************************************************************************
	bool OnLoginFailed()
	{
		_loginPending.store(false, std::memory_order_release);
		return ++_loginFailures >= kMaxLoginFailures;
	}

	//***************************************************************************
	// @brief 패킷 한 건을 처리해도 되는지 판정합니다(초당 kMaxPacketsPerSec건 고정 윈도우).
	// @details 종류를 가리지 않는 전체 수신 상한이다. 넘으면 호출부(OnRecv)가 연결을 끊는다.
	//***************************************************************************
	bool AllowPacket()
	{
		const int64 nowMs = NowMs();
		if( nowMs - _pktWindowStartMs >= 1000 )
		{
			_pktWindowStartMs = nowMs;
			_pktCount = 0;
		}
		return ++_pktCount <= kMaxPacketsPerSec;
	}

	//***************************************************************************
	// @brief 방 입장 요청 간 최소 간격(kMinRoomEnterIntervalMs)을 지켰는지 판정합니다.
	// @details 입장 한 번이 DB 조회와 최대 1000건의 기록 스트리밍을 일으키므로 따로 제한한다.
	//***************************************************************************
	bool AllowRoomEnter()
	{
		const int64 nowMs = NowMs();
		if( nowMs - _lastRoomEnterMs < kMinRoomEnterIntervalMs )
			return false;
		_lastRoomEnterMs = nowMs;
		return true;
	}

	//***************************************************************************
	// @brief 이 계정이 차단한 사용자(publicId 16진 소문자 32자) 목록을 통째로 설정합니다.
	// @details 로그인 직후 CChatServerMain::OnFriendLogin()이 DB에서 읽어 한 번 채운다.
	//          수신(브로드캐스트) 스레드가 IsBlockingHex()로 읽고 JobQueue 스레드가
	//          갱신하므로 _blockMutex로 보호한다. MarkLoggedIn()과 같은 좁은 용도 API.
	//***************************************************************************
	void SetBlockedUsers(std::unordered_set<std::string> blockedHex)
	{
		std::lock_guard<std::mutex> lock(_blockMutex);
		_blockedUsers = std::move(blockedHex);
		_hasBlockedUsers.store(!_blockedUsers.empty(), std::memory_order_release);
	}

	//***************************************************************************
	// @brief 차단 성공 후 차단 목록에 한 명을 추가합니다(친구 기능 핸들러 전용).
	//***************************************************************************
	void AddBlockedUser(const std::string& hex)
	{
		std::lock_guard<std::mutex> lock(_blockMutex);
		_blockedUsers.insert(hex);
		_hasBlockedUsers.store(true, std::memory_order_release);
	}

	//***************************************************************************
	// @brief 차단 해제 후 차단 목록에서 한 명을 제거합니다(친구 기능 핸들러 전용).
	//***************************************************************************
	void RemoveBlockedUser(const std::string& hex)
	{
		std::lock_guard<std::mutex> lock(_blockMutex);
		_blockedUsers.erase(hex);
		_hasBlockedUsers.store(!_blockedUsers.empty(), std::memory_order_release);
	}

	//***************************************************************************
	// @brief 이 계정이 해당 사용자(publicId 16진 소문자 32자)를 차단했는지 반환합니다.
	// @details 채팅 브로드캐스트의 수신자별 필터와 과거 대화 기록 스트리밍에서 호출된다.
	//          차단 목록이 비어있는 대부분의 세션은 락 없이 곧바로 false를 반환한다.
	//***************************************************************************
	bool IsBlockingHex(const std::string& hex) const
	{
		if( !_hasBlockedUsers.load(std::memory_order_acquire) )
			return false;

		std::lock_guard<std::mutex> lock(_blockMutex);
		return _blockedUsers.find(hex) != _blockedUsers.end();
	}

private:
	void	HandlePacket(const PacketHeader* header, size_t bufferSize);

private:
	CChatServerMain* _server = nullptr;	// 뒤로 참조 — 서버 소유 세션이라 세션보다 오래 살아있음이 보장됨
	std::array<BYTE, kPublicIdBytes>	_publicId{};	// 로그인된 계정의 안정 식별자(로그인 전엔 전부 0)
	std::string		_nickname;						// 표시용 닉네임(변경 가능)
	std::string		_profileImageUrl;					// 프로필 이미지 URL(변경 가능, 미설정이면 빈 문자열)
	std::atomic<bool>	_loggedIn{ false };
	std::atomic<int32>	_roomId{ -1 };				// 현재 위치. -1=아직 미배정, 0=로비, 1~kMaxRoomId=특정 룸

	mutable std::mutex	_profileMutex;				// _nickname/_profileImageUrl 보호(DB 콜백 스레드가 갱신)

	static constexpr int32	kMaxMessagesPerSec = 10;	// 세션당 초당 채팅성 메시지 상한
	int64			_rateWindowStartMs = 0;
	int32			_rateCount = 0;

	static constexpr int32	kMaxLoginFailures = 5;			// 로그인 실패 허용 횟수(초과 시 연결 종료)
	std::atomic<bool>	_loginPending{ false };
	std::atomic<int32>	_loginFailures{ 0 };
	std::atomic<int32>	_unknownPacketStreak{ 0 };	// 알 수 없는 패킷 타입 연속 수신 횟수(정상 패킷이 오면 0으로 초기화)

	static constexpr int32	kMaxPacketsPerSec = 200;		// 세션당 초당 전체 패킷 상한
	static constexpr int64	kMinRoomEnterIntervalMs = 500;	// 방 입장 요청 최소 간격
	int64			_pktWindowStartMs = 0;
	int32			_pktCount = 0;
	int64			_lastRoomEnterMs = -kMinRoomEnterIntervalMs;

	static int64 NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	mutable std::mutex					_blockMutex;				// _blockedUsers 보호
	std::unordered_set<std::string>		_blockedUsers;				// 내가 차단한 사용자 publicId(16진). 채팅 수신 필터용
	std::atomic<bool>					_hasBlockedUsers{ false };	// 비어있는 흔한 경우 락 생략용 힌트
};

#endif // ndef CHATSESSION_H