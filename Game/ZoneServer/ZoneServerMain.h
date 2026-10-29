
//***************************************************************************
// ZoneServerMain.h : interface for the CZoneServerMain class.
//
//***************************************************************************

#ifndef UC_ZONESERVERMAIN_H
#define UC_ZONESERVERMAIN_H

#include <Network/NetworkCommon.h>
#include <Network/IOCP/IocpService.h>

#include "../GameCommon/ZoneCommon/ZonePacket.h"
#include "BattleZone.h"
#include "ZoneServerOptions.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class CZoneLinkSession;

//***************************************************************************
// @class CZoneServerMain
// @brief 존 서버 파사드. CBattleZone(틱 스레드) 하나를 구동하고, Gateway와의 서버 간 연결을 유지한다.
// @details
// 역할:
//     1. CBattleZone 생성/구동
//     2. Gateway로 클라이언트 서비스(CIocpClientService)를 통해 접속하고, 연결이 끊기면 지수 백오프로 재접속
//     3. 접속 직후 존 등록(ZoneRegisterReq), 등록 성공 후 주기적 하트비트(부하 정보 포함) 전송
//     4. Gateway가 보낸 입장/퇴장/클라이언트 패킷을 존 입력 큐(Post)로 넘김 — 존 상태는 존 스레드만 만진다
//     5. 존이 클라이언트로 보낼 패킷을 중계 패킷으로 감싸 Gateway로 전송
//
// 소유/정리 순서: 링크 스레드 정지 -> 클라이언트 서비스 Close(더 이상 패킷이 존으로 들어오지 않음)
//                 -> 존 정지/해제 -> IOCP 코어 해제.
// 등록이 거부되면(중복 zoneId 등) 재시도해도 같은 결과이므로 IsRegisterRejected()가 true가 되며,
// 프로세스(main)가 이를 보고 종료해야 한다.
//***************************************************************************
class CZoneServerMain
{
public:
	CZoneServerMain() = default;
	~CZoneServerMain();

	CZoneServerMain(const CZoneServerMain&) = delete;
	CZoneServerMain& operator=(const CZoneServerMain&) = delete;

	bool Start(const SZoneServerOptions& options);
	void Stop();

	// ── CZoneLinkSession 콜백 (IOCP 워커 스레드에서 호출) ──
	void OnLinkConnected(const std::shared_ptr<CZoneLinkSession>& session);
	void OnLinkDisconnected(CZoneLinkSession* session);
	bool OnGatewayPacket(const PacketHeader* header);

	// ── 전송 (어느 스레드에서나 호출 가능) ────────────────
	bool SendToGateway(const void* data, uint16 size);
	bool SendToClient(uint64 clientSessionId, const void* payload, uint16 payloadSize);
	bool SendToClients(const uint64* clientSessionIds, size_t count, const void* payload, uint16 payloadSize);

	//***************************************************************************
	// @brief Gateway가 존 등록을 거부했는지 반환합니다. true면 프로세스를 종료해야 합니다.
	// @return 등록이 거부됐으면 true
	//***************************************************************************
	bool IsRegisterRejected() const { return _registerRejected.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief Gateway에 등록이 완료된 상태인지 반환합니다.
	// @return 등록 완료 상태면 true
	//***************************************************************************
	bool IsRegistered() const { return _registered.load(std::memory_order_acquire); }

private:
	bool HandleRegisterRes(const PacketHeader* header);
	bool HandleEnterReq(const PacketHeader* header);
	bool HandleLeaveReq(const PacketHeader* header);
	bool HandleRelayToZone(const PacketHeader* header);

	void SendRegisterReq();
	void SendHeartbeat();
	void LinkLoop();

	static constexpr int32 kLinkTickMs = 200;			// 링크 스레드 틱 간격
	static constexpr int32 kReconnectMinMs = 500;		// 재접속 최소 대기
	static constexpr int32 kReconnectMaxMs = 5000;		// 재접속 최대 대기(지수 백오프 상한)

private:
	SZoneServerOptions						_options;

	CIocpCoreRef							_iocpCore;
	std::shared_ptr<CIocpClientService>		_client;		// Gateway 접속용 클라이언트 서비스
	std::unique_ptr<CBattleZone>			_zone;

	std::mutex								_linkMutex;		// _link/_needReconnect/_reconnectBackoffMs 보호
	std::condition_variable					_linkCv;
	std::shared_ptr<CZoneLinkSession>		_link;			// 현재 Gateway 연결 세션 (없으면 nullptr)
	bool									_needReconnect = false;
	int32									_reconnectBackoffMs = kReconnectMinMs;
	std::thread								_linkThread;	// 재접속/하트비트 스레드

	std::atomic<bool>						_stopRequested{ false };
	std::atomic<bool>						_registered{ false };
	std::atomic<bool>						_registerRejected{ false };
	std::atomic<uint32>						_heartbeatIntervalMs{ kDefaultHeartbeatIntervalMs };
};

#endif // ndef UC_ZONESERVERMAIN_H