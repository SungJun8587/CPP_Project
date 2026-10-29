
//***************************************************************************
// ZoneServerMain.cpp: implementation of the CZoneServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "ZoneServerMain.h"
#include "ZoneLinkSession.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace
{
	constexpr size_t kInlinePacketBytes = 32;		// 작업 객체에 인라인으로 담는 클라이언트 패킷 최대 크기 (ZoneJob 인라인 용량 48바이트에 맞춤)
}

//***************************************************************************
// @brief 소멸자 — 아직 실행 중이면 Stop()으로 정리합니다.
//***************************************************************************
CZoneServerMain::~CZoneServerMain()
{
	Stop();
}

//***************************************************************************
// @brief 존 서버를 구동합니다.
// @details 존(틱 스레드)을 먼저 띄운 뒤 Gateway에 접속한다. Gateway가 아직 떠 있지 않아도 Start()는 성공하며,
//          링크 스레드가 연결될 때까지 재접속을 반복한다.
// @param options 존 서버 설정
// @return 존 생성/구동과 클라이언트 서비스 시작이 성공하면 true
//***************************************************************************
bool CZoneServerMain::Start(const SZoneServerOptions& options)
{
	_options = options;
	_stopRequested.store(false);
	_registered.store(false);
	_registerRejected.store(false);

	// 1. 존 생성/구동
	FieldZoneConfig zoneConfig;
	zoneConfig.zone.zoneId = _options.zoneId;
	zoneConfig.zone.tickIntervalMs = _options.tickIntervalMs;
	zoneConfig.minX = _options.minX;
	zoneConfig.minY = _options.minY;
	zoneConfig.maxX = _options.maxX;
	zoneConfig.maxY = _options.maxY;
	zoneConfig.cellSize = _options.cellSize;
	zoneConfig.respawnDelayMs = _options.respawnDelayMs;
	zoneConfig.allowPvp = _options.allowPvp;
	zoneConfig.respawnInvulnMs = _options.respawnInvulnMs;

	SCombatStats playerStats;
	playerStats.maxHp = _options.playerMaxHp;
	playerStats.attack = _options.playerAttack;
	playerStats.defense = _options.playerDefense;
	playerStats.attackRange = _options.attackRange;
	playerStats.attackIntervalMs = _options.attackIntervalMs;

	Vec2 spawnPos;
	spawnPos.x = _options.spawnX;
	spawnPos.y = _options.spawnY;

	_zone = std::make_unique<CBattleZone>(zoneConfig, this, spawnPos, _options.playerSpeed, playerStats);
	if( !_zone->IsValid() )
	{
		LOG_ERROR(_T("[CZoneServerMain] invalid zone configuration. zoneId=%u"), _options.zoneId);
		_zone.reset();
		return false;
	}

	// 사거리가 AOI 시야보다 길면 보이지 않는 대상을 공격할 수 있고 피해 통지도 시야 밖으로 나간다.
	if( !_zone->IsAttackRangeSupported(_options.attackRange, _options.playerSpeed) )
	{
		LOG_ERROR(_T("[CZoneServerMain] attack reach (range + tolerance + latency allowance) exceeds the guaranteed AOI view range. zoneId=%u, attackRange=%.1f, playerSpeed=%.1f, viewRange=%.1f"),
			_options.zoneId, _options.attackRange, _options.playerSpeed, _zone->GetGuaranteedViewRange());
		_zone.reset();
		return false;
	}

	if( !_zone->Start() )
	{
		_zone.reset();
		return false;
	}

	// 2. Gateway 접속용 클라이언트 서비스 구성. 세션은 1개이며, 연결이 끊기면 링크 스레드가 새 세션으로 재접속한다.
	_iocpCore = MakeShared<CIocpCore>();

	SessionFactory factory = [this]() -> CSessionRef
		{
			return std::make_shared<CZoneLinkSession>(this);
		};

	EngineCoreRef engineCore = _iocpCore;
	CNetServiceRef service = CNetworkFactory::CreateClientService(
		engineCore, CNetAddress(_options.gatewayIp, _options.gatewayPort), factory, 1, _options.workerThreadCount);

	_client = std::static_pointer_cast<CIocpClientService>(service);
	if( _client == nullptr )
	{
		Stop();
		return false;
	}

	// 서버 간 연결은 작은 패킷을 자주 주고받으므로 Nagle을 끄고, 조용히 끊긴 연결을 keep-alive로 감지한다.
	CIocpSession::SocketOptions socketOptions;
	socketOptions.noDelay = true;
	socketOptions.keepAlive = true;
	_client->SetSessionSocketOptions(socketOptions);

	// 3. 링크 스레드를 먼저 시작해, Start() 직후 첫 연결이 실패해도 재접속이 이어지게 한다.
	_linkThread = std::thread([this]() { LinkLoop(); });

	if( !_client->Start() )
	{
		Stop();
		return false;
	}

	return true;
}

//***************************************************************************
// @brief 존 서버를 정지합니다.
// @details 순서: 링크 스레드 -> 클라이언트 서비스(세션 종료까지 대기) -> 존 -> IOCP 코어.
//          서비스를 존보다 먼저 닫아야 Gateway 패킷이 해제된 존으로 Post되지 않는다.
//***************************************************************************
void CZoneServerMain::Stop()
{
	{
		std::lock_guard<std::mutex> lock(_linkMutex);
		_stopRequested.store(true);
	}
	_linkCv.notify_all();

	if( _linkThread.joinable() )
		_linkThread.join();

	if( _client )
	{
		_client->Close();
		_client.reset();
	}

	{
		std::lock_guard<std::mutex> lock(_linkMutex);
		_link.reset();
	}

	if( _zone )
	{
		_zone->Stop();
		_zone.reset();
	}

	_iocpCore.reset();
}

//***************************************************************************
// @brief Gateway 연결이 완료되면 호출됩니다. 연결 세션을 보관하고 존 등록을 요청합니다.
// @param session 연결된 세션
//***************************************************************************
void CZoneServerMain::OnLinkConnected(const std::shared_ptr<CZoneLinkSession>& session)
{
	{
		std::lock_guard<std::mutex> lock(_linkMutex);
		_link = session;
		_needReconnect = false;
		_reconnectBackoffMs = kReconnectMinMs;
	}

	_registered.store(false, std::memory_order_release);
	SendRegisterReq();
}

//***************************************************************************
// @brief Gateway 연결이 끊기거나 연결 시도가 실패하면 호출됩니다.
// @details 현재 연결이 이 세션일 때만 연결을 비운다. 이미 새 연결이 맺어진 뒤에 늦게 도착한
//          이전 세션의 종료 통지는 재접속을 일으키지 않는다(연결이 이중으로 생기는 것을 방지).
// @param session 종료된 세션
//***************************************************************************
void CZoneServerMain::OnLinkDisconnected(CZoneLinkSession* session)
{
	std::lock_guard<std::mutex> lock(_linkMutex);

	if( _link.get() == session )
	{
		_link.reset();
		_registered.store(false, std::memory_order_release);
		LOG_WARNING(_T("[CZoneServerMain] gateway link lost. zoneId=%u"), _options.zoneId);
	}

	if( _link == nullptr && !_stopRequested.load() )
		_needReconnect = true;
}

//***************************************************************************
// @brief Gateway에서 온 패킷 하나를 처리합니다.
// @param header 완전한 패킷 (크기 범위는 프레이밍 단계에서 이미 검증됨)
// @return 프로토콜 위반이면 false(연결 종료), 정상 처리면 true
//***************************************************************************
bool CZoneServerMain::OnGatewayPacket(const PacketHeader* header)
{
	switch( static_cast<EZoneServerPacketType>(header->type) )
	{
	case EZoneServerPacketType::ZoneRegisterRes:	return HandleRegisterRes(header);
	case EZoneServerPacketType::ZoneEnterReq:		return HandleEnterReq(header);
	case EZoneServerPacketType::ZoneLeaveReq:		return HandleLeaveReq(header);
	case EZoneServerPacketType::ZoneRelayToZone:	return HandleRelayToZone(header);
	default:
		LOG_ERROR(_T("[CZoneServerMain] unknown packet type from gateway: %u"), static_cast<unsigned>(header->type));
		return false;
	}
}

//***************************************************************************
// @brief Gateway로 패킷을 보냅니다. 연결이 없으면 false.
// @param data 전송할 데이터
// @param size 전송할 바이트 수
// @return 송신 큐에 넣었으면 true
//***************************************************************************
bool CZoneServerMain::SendToGateway(const void* data, uint16 size)
{
	std::shared_ptr<CZoneLinkSession> link;
	{
		std::lock_guard<std::mutex> lock(_linkMutex);
		link = _link;
	}

	return link != nullptr && link->Send(data, size);
}

//***************************************************************************
// @brief 클라이언트로 보낼 패킷을 중계 패킷으로 감싸 Gateway로 전송합니다.
// @param clientSessionId 받을 클라이언트의 Gateway 세션 ID
// @param payload         클라이언트에게 전달할 패킷 원문(자체 PacketHeader 포함)
// @param payloadSize     원문 크기
// @return 송신 큐에 넣었으면 true
//***************************************************************************
bool CZoneServerMain::SendToClient(uint64 clientSessionId, const void* payload, uint16 payloadSize)
{
	thread_local BYTE buffer[kMaxServerPacketBytes];

	uint16 totalSize = 0;
	if( !BuildRelayPacket<ZoneRelayToClientPacket>(EZoneServerPacketType::ZoneRelayToClient, clientSessionId,
		payload, payloadSize, buffer, sizeof(buffer), totalSize) )
		return false;

	return SendToGateway(buffer, totalSize);
}

//***************************************************************************
// @brief 같은 패킷을 여러 클라이언트로 보냅니다. 수신자가 한 명이면 일반 중계를, 여러 명이면 멀티캐스트 중계를 쓰며,
//        한 패킷에 담을 수 없을 만큼 많으면 나눠서 보낸다. 수신자마다 중계 패킷을 만들던 것보다 Zone->Gateway 구간의
//        복사량과 송신 횟수가 크게 줄어든다.
// @param clientSessionIds 받을 클라이언트의 Gateway 세션 ID 배열 (중복 없음)
// @param count            수신자 수
// @param payload          클라이언트에게 전달할 패킷 원문(자체 PacketHeader 포함)
// @param payloadSize      원문 크기
// @return 모든 조각을 송신 큐에 넣었으면 true (수신자가 없으면 true)
//***************************************************************************
bool CZoneServerMain::SendToClients(const uint64* clientSessionIds, size_t count, const void* payload, uint16 payloadSize)
{
	if( count == 0 )
		return true;

	if( count == 1 )
		return SendToClient(clientSessionIds[0], payload, payloadSize);

	const size_t perPacket = MaxMulticastRecipients(payloadSize, kMaxServerPacketBytes);
	if( perPacket == 0 )
		return false;

	thread_local BYTE buffer[kMaxServerPacketBytes];

	bool allSent = true;
	for( size_t offset = 0; offset < count; )
	{
		const size_t n = (count - offset) < perPacket ? (count - offset) : perPacket;

		uint16 totalSize = 0;
		if( !BuildMulticastRelayPacket(clientSessionIds + offset, n, payload, payloadSize, buffer, sizeof(buffer), totalSize) )
			return false;

		allSent &= SendToGateway(buffer, totalSize);
		offset += n;
	}
	return allSent;
}

//***************************************************************************
// @brief 등록 응답을 처리합니다. 성공하면 하트비트를 시작하고, 거부되면 프로세스 종료를 요청합니다.
// @param header ZoneRegisterResPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CZoneServerMain::HandleRegisterRes(const PacketHeader* header)
{
	if( header->size < sizeof(ZoneRegisterResPacket) )
		return false;

	const ZoneRegisterResPacket* res = static_cast<const ZoneRegisterResPacket*>(header);

	if( !res->success )
	{
		LOG_ERROR(_T("[CZoneServerMain] zone registration rejected. zoneId=%u, reason=%u"),
			_options.zoneId, static_cast<unsigned>(res->reason));
		_registerRejected.store(true, std::memory_order_release);
		return true;
	}

	const uint32 intervalMs = (std::min)((std::max)(res->heartbeatIntervalMs, 200u), 30000u);
	_heartbeatIntervalMs.store(intervalMs);
	_registered.store(true, std::memory_order_release);

	LOG_INFO(_T("[CZoneServerMain] zone registered. zoneId=%u, heartbeatIntervalMs=%u"), _options.zoneId, intervalMs);
	return true;
}

//***************************************************************************
// @brief 입장 요청을 존 입력 큐로 넘깁니다. 큐가 가득 차 넘기지 못하면 즉시 실패 응답을 보냅니다.
// @param header ZoneEnterReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CZoneServerMain::HandleEnterReq(const PacketHeader* header)
{
	if( header->size < sizeof(ZoneEnterReqPacket) )
		return false;

	const ZoneEnterReqPacket* req = static_cast<const ZoneEnterReqPacket*>(header);
	const uint64 clientSessionId = req->clientSessionId;
	const uint64 charId = req->charId;

	const bool posted = _zone != nullptr && _zone->Post([clientSessionId, charId](CZone& zone)
		{
			static_cast<CBattleZone&>(zone).HandleEnter(clientSessionId, charId);
		});

	if( !posted )
	{
		ZoneEnterResPacket res{};
		InitZonePacket(res, EZoneServerPacketType::ZoneEnterRes);
		res.clientSessionId = clientSessionId;
		res.charId = charId;
		res.success = 0;
		res.reason = static_cast<uint8>(EZoneEnterResult::ZoneError);
		SendToGateway(&res, res.size);
	}

	return true;
}

//***************************************************************************
// @brief 퇴장 요청을 존 입력 큐로 넘깁니다.
// @details 큐가 가득 차 넘기지 못하면 엔티티가 존에 남을 수 있으므로 경고를 남긴다
//          (입력 큐 상한은 정상 부하에서 도달하지 않는 값으로 잡아야 한다).
// @param header ZoneLeaveReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CZoneServerMain::HandleLeaveReq(const PacketHeader* header)
{
	if( header->size < sizeof(ZoneLeaveReqPacket) )
		return false;

	const ZoneLeaveReqPacket* req = static_cast<const ZoneLeaveReqPacket*>(header);
	const uint64 clientSessionId = req->clientSessionId;
	const uint64 charId = req->charId;

	const bool posted = _zone != nullptr && _zone->Post([clientSessionId, charId](CZone& zone)
		{
			static_cast<CBattleZone&>(zone).HandleLeave(clientSessionId, charId);
		});

	if( !posted )
		LOG_WARNING(_T("[CZoneServerMain] leave request dropped (zone inbox full or stopped). charId=%llu"), charId);

	return true;
}

//***************************************************************************
// @brief 클라이언트 패킷 중계를 존 입력 큐로 넘깁니다. 원문은 복사해서 넘긴다(수신 버퍼는 이 함수 반환 후 재사용됨).
// @param header ZoneRelayToZonePacket + 클라이언트 패킷 원문
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CZoneServerMain::HandleRelayToZone(const PacketHeader* header)
{
	if( header->size < sizeof(ZoneRelayToZonePacket) + sizeof(PacketHeader) )
		return false;

	const ZoneRelayToZonePacket* relay = static_cast<const ZoneRelayToZonePacket*>(header);
	const BYTE* payload = reinterpret_cast<const BYTE*>(relay) + sizeof(ZoneRelayToZonePacket);
	const size_t payloadSize = static_cast<size_t>(header->size) - sizeof(ZoneRelayToZonePacket);

	// 원문 헤더의 size가 실제 페이로드 크기와 맞아야 한다.
	const PacketHeader* inner = reinterpret_cast<const PacketHeader*>(payload);
	if( inner->size != payloadSize )
		return false;

	if( _zone == nullptr )
		return true;

	const uint64 clientSessionId = relay->clientSessionId;

	// 작은 패킷(이동 요청 등 대부분)은 작업 객체 안에 인라인으로 복사해 힙 할당 없이 넘긴다.
	if( payloadSize <= kInlinePacketBytes )
	{
		std::array<BYTE, kInlinePacketBytes> inlineBuf{};
		std::memcpy(inlineBuf.data(), payload, payloadSize);

		_zone->Post([clientSessionId, inlineBuf](CZone& zone)
			{
				static_cast<CBattleZone&>(zone).HandleClientPacket(
					clientSessionId, reinterpret_cast<const PacketHeader*>(inlineBuf.data()));
			});

		return true;
	}

	std::vector<BYTE> copy(payload, payload + payloadSize);

	_zone->Post([clientSessionId, copy = std::move(copy)](CZone& zone)
		{
			static_cast<CBattleZone&>(zone).HandleClientPacket(
				clientSessionId, reinterpret_cast<const PacketHeader*>(copy.data()));
		});

	return true;
}

//***************************************************************************
// @brief Gateway에 존 등록을 요청합니다. 연결 직후 첫 패킷으로 전송한다.
//***************************************************************************
void CZoneServerMain::SendRegisterReq()
{
	ZoneRegisterReqPacket req{};
	InitZonePacket(req, EZoneServerPacketType::ZoneRegisterReq);
	req.protocolVersion = kZoneProtocolVersion;
	req.zoneId = _options.zoneId;
	req.kind = static_cast<uint8>(_options.kind);
	req.capacity = _options.capacity;
	strncpy_s(req.serverName, kServerNameBytes, _options.serverName.c_str(), _TRUNCATE);

	SendToGateway(&req, req.size);
}

//***************************************************************************
// @brief Gateway에 하트비트(현재 인원, 틱 처리 시간)를 전송합니다.
//***************************************************************************
void CZoneServerMain::SendHeartbeat()
{
	if( _zone == nullptr )
		return;

	const ZoneStats stats = _zone->GetStats();

	ZoneHeartbeatPacket heartbeat{};
	InitZonePacket(heartbeat, EZoneServerPacketType::ZoneHeartbeat);
	heartbeat.zoneId = _options.zoneId;
	heartbeat.userCount = _zone->GetPlayerCount();
	heartbeat.lastTickUs = static_cast<uint32>((std::min)(stats.lastTickUs, static_cast<uint64>(0xFFFFFFFFu)));
	// 시작 후 누적 최대가 아니라 직전 하트비트 이후 구간의 최대값을 보고한다.
	heartbeat.maxTickUs = static_cast<uint32>((std::min)(_zone->ConsumeMaxTickUs(), static_cast<uint64>(0xFFFFFFFFu)));

	SendToGateway(&heartbeat, heartbeat.size);
}

//***************************************************************************
// @brief 링크 스레드 본체. 짧은 틱마다 (1) 연결이 없으면 지수 백오프로 재접속하고,
//        (2) 등록이 끝났으면 주기적으로 하트비트를 보낸다.
// @details ConnectOneMoreSession()/SendHeartbeat()는 내부에서 _linkMutex를 잡거나 연결 실패 통지로
//          OnLinkDisconnected()를 부를 수 있으므로 반드시 락을 푼 상태에서 호출한다.
//***************************************************************************
void CZoneServerMain::LinkLoop()
{
	using Clock = std::chrono::steady_clock;

	auto nextReconnect = Clock::now();
	auto nextHeartbeat = Clock::now();

	std::unique_lock<std::mutex> lock(_linkMutex);
	while( !_stopRequested.load() )
	{
		_linkCv.wait_for(lock, std::chrono::milliseconds(kLinkTickMs), [this]() { return _stopRequested.load(); });
		if( _stopRequested.load() )
			break;

		const auto now = Clock::now();

		if( _needReconnect && now >= nextReconnect )
		{
			_needReconnect = false;
			const int32 backoffMs = _reconnectBackoffMs;
			_reconnectBackoffMs = (std::min)(_reconnectBackoffMs * 2, kReconnectMaxMs);

			lock.unlock();
			const bool posted = _client != nullptr && _client->ConnectOneMoreSession() != nullptr;
			lock.lock();

			// 세션 생성/게시 자체가 실패한 경우에만 여기서 다시 예약한다.
			// 게시는 됐지만 연결이 실패한 경우는 OnLinkDisconnected()가 예약한다.
			if( !posted && !_stopRequested.load() )
				_needReconnect = true;

			nextReconnect = Clock::now() + std::chrono::milliseconds(backoffMs);
		}

		if( _registered.load(std::memory_order_acquire) && now >= nextHeartbeat )
		{
			nextHeartbeat = now + std::chrono::milliseconds(_heartbeatIntervalMs.load());

			lock.unlock();
			SendHeartbeat();
			lock.lock();
		}
	}
}