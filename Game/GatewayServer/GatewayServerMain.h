
//***************************************************************************
// GatewayServerMain.h : interface for the CGatewayServerMain class.
//
//***************************************************************************

#ifndef GATEWAYSERVERMAIN_H
#define GATEWAYSERVERMAIN_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "GatewayServerOptions.h"

class CGatewayClientSession;
class CGatewayZoneSession;

//***************************************************************************
// @class CGatewayServerMain
// @brief 게이트웨이 서버 파사드. 클라이언트 접속과 존 서버 접속을 각각 받아 중계/라우팅한다.
// @details
// 역할:
//     1. 클라이언트용/존 서버용 두 개의 IOCP 서버 서비스 구동 (각각 별도 IOCP 코어와 워커 풀을 가짐)
//     2. 로그인(임시), 존 입장/퇴장 처리와 존 정원 관리(CZoneRouter의 슬롯)
//     3. 클라이언트 패킷(2000 이상)을 현재 존 서버로 중계, 존 서버가 보낸 패킷을 클라이언트로 중계
//     4. 존 서버 등록/하트비트 처리, 하트비트 타임아웃 감시, 존 서버 연결 종료 시 해당 존의 클라이언트 정리
//
// 두 서비스가 IOCP 코어를 공유하지 않는 이유: 서비스 종료 시 각자의 워커 수만큼 종료 키를 게시하는데,
// 코어를 공유하면 다른 서비스의 워커가 종료 키를 가져가 워커 풀 Join이 끝나지 않을 수 있다.
//
// 종료 순서: 감시 스레드 -> 클라이언트 서비스(클라이언트 퇴장이 존 서버로 전달돼야 하므로 먼저)
//            -> 존 서버 서비스 -> IOCP 코어.
//***************************************************************************
class CGatewayServerMain
{
public:
	CGatewayServerMain() = default;
	~CGatewayServerMain();

	CGatewayServerMain(const CGatewayServerMain&) = delete;
	CGatewayServerMain& operator=(const CGatewayServerMain&) = delete;

	bool Start(const SGatewayOptions& options);
	void Stop();

	// ── CGatewayClientSession 콜백 ───────────────────────
	void OnClientConnected(const std::shared_ptr<CGatewayClientSession>& session);
	void OnClientDisconnected(CGatewayClientSession& session);
	bool OnClientPacket(CGatewayClientSession& client, const PacketHeader* header);

	// ── CGatewayZoneSession 콜백 ─────────────────────────
	bool OnZonePacket(CGatewayZoneSession& session, const PacketHeader* header);
	void OnZoneSessionClosed(CGatewayZoneSession* session);

	//***************************************************************************
	// @brief 라우팅 테이블을 반환합니다.
	// @return 존 라우터 참조
	//***************************************************************************
	CZoneRouter& GetRouter() { return _router; }

private:
	// ── 클라이언트 패킷 핸들러 (false = 프로토콜 위반) ───
	bool HandleLogin(CGatewayClientSession& client, const PacketHeader* header);
	bool HandleEnterZone(CGatewayClientSession& client, const PacketHeader* header);
	bool HandleLeaveZone(CGatewayClientSession& client, const PacketHeader* header);
	bool RelayClientPacket(CGatewayClientSession& client, const PacketHeader* header);

	// ── 존 서버 패킷 핸들러 (false = 프로토콜 위반) ──────
	bool HandleZoneRegister(CGatewayZoneSession& session, const PacketHeader* header);
	bool HandleZoneHeartbeat(CGatewayZoneSession& session, const PacketHeader* header);
	bool HandleZoneEnterRes(CGatewayZoneSession& session, const PacketHeader* header);
	bool HandleZoneKick(CGatewayZoneSession& session, const PacketHeader* header);
	bool HandleRelayToClient(CGatewayZoneSession& session, const PacketHeader* header);

	bool AuthenticateClient(CGatewayClientSession& client, uint64 charId);
	uint32 DetachFromZone(CGatewayClientSession& client);
	std::shared_ptr<CGatewayClientSession> FindClient(uint64 clientSessionId) const;

	static void SendEnterZoneRes(CGatewayClientSession& client, EGatewayResult reason, uint32 zoneId, float spawnX, float spawnY);

	void MaintenanceLoop();
	void CheckZoneHeartbeats();

	static constexpr int32 kMaintenanceTickMs = 500;	// 감시 스레드 틱 간격

private:
	SGatewayOptions							_options;

	CIocpCoreRef							_clientCore;		// 클라이언트용 IOCP 코어
	CIocpCoreRef							_zoneCore;			// 존 서버용 IOCP 코어
	CIocpServerServiceRef					_clientService;
	CIocpServerServiceRef					_zoneService;

	CZoneRouter								_router;			// zoneId -> 존 서버 노드

	mutable PRWLock							_clientLock;		// _clients 보호
	std::unordered_map<uint64, std::weak_ptr<CGatewayClientSession>>	_clients;	// 클라이언트 세션 ID -> 세션 (존 서버 응답 라우팅용)

	PRWLock									_charLock;			// _chars 보호
	std::unordered_map<uint64, uint64>		_chars;				// 로그인 중인 캐릭터 ID -> 클라이언트 세션 ID (중복 로그인 차단)

	std::thread								_maintenanceThread;	// 존 서버 하트비트 타임아웃 감시 스레드
	std::mutex								_maintenanceMutex;
	std::condition_variable					_maintenanceCv;
	bool									_maintenanceStop = false;
};

#endif // ndef GATEWAYSERVERMAIN_H