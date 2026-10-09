
//***************************************************************************
// ChatStressSession.h : interface for the CChatStressSession class.
//
//***************************************************************************

#ifndef UC_CHATSTRESSSESSION_H
#define UC_CHATSTRESSSESSION_H

#include <Network/IOCP/IocpSession.h>
#include <Util/DateTimeUtil.h>
#include "ChatPacket.h"
#include "ChatStressConfig.h"
#include "ChatStressStats.h"
#include "ChatStressAccounts.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>

//***************************************************************************
// @brief 스트레스 테스트용 가상 클라이언트 세션
// @details CChatClientSession과 같은 방식으로 CIocpSession을 상속한다(1개 연결 =
//          1개 인스턴스, OnConnected에서 LoginReq 전송, OnRecv에서 헤더 기반
//          프레이밍). 다른 점: 계정 파일/콜백 파사드(CChatClientMain) 없이
//          통계(Stats)에 직접 기록하고, 로그인 후 주기 송신(Tick)을 스케줄러
//          스레드가 구동한다.
//***************************************************************************
class CChatStressSession : public CIocpSession
{
public:
	//***************************************************************************
	// @brief 세션의 연결/로그인 진행 상태
	// @details Connecting -> LoggingIn -> Active 순으로 진행하며, 어느 단계에서든
	//          연결이 끊기면 Closed가 된다. Closed에서는 되돌아가지 않는다.
	//***************************************************************************
	enum class State : int
	{
		Connecting = 0,	// ConnectEx 게시 후 연결 완료 대기
		LoggingIn,		// 연결 완료, LoginReq 전송 후 LoginRes 대기
		Active,			// 로그인 완료, 주기 송신 중
		Closed			// 연결 실패 또는 종료
	};

	CChatStressSession(int index, bool hasToken, const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& token);
	virtual ~CChatStressSession();

	//***************************************************************************
	// @brief 가상 클라이언트 번호를 반환한다.
	// @return 클라이언트 번호(0부터)
	//***************************************************************************
	int		GetIndex() const { return _index; }

	//***************************************************************************
	// @brief 현재 세션 상태를 반환한다.
	// @return 현재 State
	//***************************************************************************
	State	GetState() const { return _state.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 세션이 닫혔는지 확인한다.
	// @return Closed 상태이면 true
	//***************************************************************************
	bool	IsClosed() const { return GetState() == State::Closed; }

	void	Tick(int64_t nowUs);

protected:
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

private:
	void	HandlePacket(const PacketHeader* header);
	void	OnLoginRes(const LoginResPacket* packet);
	void	OnLoggedIn();
	int		RoomFor() const;

private:
	const int	_index;		// 가상 클라이언트 번호
	const bool	_hasToken;	// true=저장된 public_id/token으로 재접속, false=신규 가입
	std::array<BYTE, kPublicIdBytes>	_publicId{};	// 재접속에 쓰는 계정 식별자
	std::array<BYTE, kTokenBytes>		_token{};		// 재접속에 쓰는 토큰

	std::atomic<State>	_state{ State::Connecting };	// 현재 상태
	int64_t				_tCreatedUs = 0;				// ConnectEx 게시 시각(연결 지연 측정 기준)
	int64_t				_tLoginSentUs = 0;				// LoginReq 전송 시각(로그인 지연 측정 기준)

	// 워커 스레드(수신)와 스케줄러 스레드(송신)가 함께 만지는 값은 atomic
	std::atomic<int64_t>	_nextChat{ INT64_MAX };		// 다음 채팅 송신 시각(마이크로초), INT64_MAX=송신 안 함
	std::atomic<int64_t>	_nextList{ INT64_MAX };		// 다음 방 목록 요청 시각
	std::atomic<int64_t>	_nextMember{ INT64_MAX };	// 다음 방 멤버 목록 요청 시각
	std::atomic<int64_t>	_nextCount{ INT64_MAX };	// 다음 동접자 조회 시각
	std::atomic<int32_t>	_reqId{ 0 };				// 요청 ID 발급용 증가 카운터
	std::atomic<int32_t>	_pendingListId{ -1 };		// 응답 대기 중인 방 목록 요청 ID, -1=없음
	std::atomic<int32_t>	_pendingMemberId{ -1 };		// 응답 대기 중인 방 멤버 목록 요청 ID, -1=없음
	std::atomic<int64_t>	_pendingListT{ 0 };			// 방 목록 요청 전송 시각(응답 지연 측정 기준)
	std::atomic<int64_t>	_pendingMemberT{ 0 };		// 방 멤버 목록 요청 전송 시각
	std::atomic<uint32_t>	_chatSeq{ 0 };				// 채팅 RTT 마커의 순번
	int						_myRoom = 0;				// 로그인 후 입장할 방 번호, 0=로비
};

#endif // ndef UC_CHATSTRESSSESSION_H