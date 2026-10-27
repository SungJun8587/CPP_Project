
//***************************************************************************
// GatewayClientSession.h : interface for the CGatewayClientSession class.
//
//***************************************************************************

#ifndef GATEWAYCLIENTSESSION_H
#define GATEWAYCLIENTSESSION_H

#include <Network/IOCP/IocpSession.h>

#include "../GameCommon/ZoneCommon/ZonePacket.h"

#include <atomic>
#include <chrono>

class CGatewayServerMain;

//***************************************************************************
// @class CGatewayClientSession
// @brief 게임 클라이언트가 Gateway로 맺는 연결 세션(서버 측 세션).
// @details
// 로그인 상태(_loggedIn/_charId)와 현재 존(_zoneId) 정보를 이 세션 안에서만 관리하고,
// 로그인/입장/퇴장/중계 처리는 CGatewayServerMain에 위임한다 — 세션은 프레이밍과 자기 상태만 안다.
//
// 이 세션의 수신 처리는 세션당 직렬이지만, 존 서버 세션의 스레드가 입장 응답을 처리하며 존 상태를 바꿀 수 있으므로
// 존 관련 상태는 모두 atomic으로 둔다.
//
// _zoneId의 의미: 0이면 존 없음, 0이 아니면 "그 존의 정원 슬롯을 점유 중"(입장 진행 중 포함).
//                 _enterConfirmed가 true가 되어야 입장이 확정된 것이다.
//***************************************************************************
class CGatewayClientSession : public CIocpSession
{
public:
	explicit CGatewayClientSession(CGatewayServerMain* server);
	virtual ~CGatewayClientSession() = default;

	//***************************************************************************
	// @brief 로그인 상태를 반환합니다.
	// @return 로그인했으면 true
	//***************************************************************************
	bool IsLoggedIn() const { return _loggedIn.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 로그인한 캐릭터 ID를 반환합니다. 로그인 전이면 0.
	// @return 캐릭터 ID
	//***************************************************************************
	uint64 GetCharId() const { return _charId.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 로그인 완료 상태를 세션에 반영합니다. 로그인 핸들러(CGatewayServerMain)만 호출한다.
	// @param charId 로그인한 캐릭터 ID
	//***************************************************************************
	void MarkLoggedIn(uint64 charId)
	{
		_charId.store(charId, std::memory_order_release);
		_loggedIn.store(true, std::memory_order_release);
	}

	//***************************************************************************
	// @brief 현재 존 ID를 반환합니다. 0이면 존에 없음.
	// @return 점유 중인 존 ID
	//***************************************************************************
	uint32 GetZoneId() const { return _zoneId.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 존 슬롯 점유를 시작합니다. 이미 존에 있거나 입장이 진행 중이면 실패합니다.
	// @param zoneId 입장하려는 존 ID
	// @return 점유에 성공했으면 true
	//***************************************************************************
	bool TryAssignZone(uint32 zoneId)
	{
		uint32 expected = 0;
		if( !_zoneId.compare_exchange_strong(expected, zoneId, std::memory_order_acq_rel) )
			return false;

		_enterConfirmed.store(false, std::memory_order_release);
		return true;
	}

	//***************************************************************************
	// @brief 존 점유를 해제하고, 해제 전 존 ID를 반환합니다. (0이면 점유 중이 아니었음)
	// @return 해제 전 존 ID
	//***************************************************************************
	uint32 ClearZone()
	{
		const uint32 zoneId = _zoneId.exchange(0, std::memory_order_acq_rel);
		_enterConfirmed.store(false, std::memory_order_release);
		return zoneId;
	}

	//***************************************************************************
	// @brief 점유 중인 존이 zoneId와 같을 때만 점유를 해제합니다.
	// @param zoneId 해제하려는 존 ID
	// @return 해제했으면 true
	//***************************************************************************
	bool ClearZoneIf(uint32 zoneId)
	{
		uint32 expected = zoneId;
		if( !_zoneId.compare_exchange_strong(expected, 0, std::memory_order_acq_rel) )
			return false;

		_enterConfirmed.store(false, std::memory_order_release);
		return true;
	}

	//***************************************************************************
	// @brief 존 입장이 확정됐는지 반환합니다.
	// @return 존 서버가 입장 성공을 응답했으면 true
	//***************************************************************************
	bool IsEnterConfirmed() const { return _enterConfirmed.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 존 입장 확정 상태를 설정합니다.
	// @param confirmed 확정 여부
	//***************************************************************************
	void SetEnterConfirmed(bool confirmed) { _enterConfirmed.store(confirmed, std::memory_order_release); }

	//***************************************************************************
	// @brief 패킷 한 건을 처리해도 되는지 판정합니다(초당 kMaxPacketsPerSec건 고정 윈도우).
	// @details 세션의 수신 처리는 세션당 직렬이라 별도 동기화가 필요 없다. 초과하면 호출부가 연결을 끊는다.
	// @return 허용 범위 안이면 true
	//***************************************************************************
	bool AllowPacket()
	{
		const int64 nowMs = SteadyNowMs();
		if( nowMs - _pktWindowStartMs >= 1000 )
		{
			_pktWindowStartMs = nowMs;
			_pktCount = 0;
		}
		return ++_pktCount <= kMaxPacketsPerSec;
	}

protected:
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

private:
	static constexpr int32	kMaxPacketsPerSec = 120;	// 세션당 초당 전체 패킷 상한

	CGatewayServerMain*		_server;						// 이 세션이 속한 Gateway (서버가 세션보다 오래 살아있음이 보장됨)
	std::atomic<bool>		_loggedIn{ false };				// 로그인 여부
	std::atomic<uint64>		_charId{ 0 };					// 로그인한 캐릭터 ID
	std::atomic<uint32>		_zoneId{ 0 };					// 점유 중인 존 ID (0 = 없음)
	std::atomic<bool>		_enterConfirmed{ false };		// 존 서버가 입장을 확정했는지

	int64					_pktWindowStartMs = 0;			// 수신 속도 제한 윈도우 시작 시각 (수신 처리 스레드 전용)
	int32					_pktCount = 0;					// 윈도우 내 수신 패킷 수 (수신 처리 스레드 전용)
};

#endif // ndef GATEWAYCLIENTSESSION_H
