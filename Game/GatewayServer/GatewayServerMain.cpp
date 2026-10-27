
//***************************************************************************
// GatewayServerMain.cpp: implementation of the CGatewayServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "GatewayServerMain.h"

#include <chrono>
#include <cstring>
#include <vector>

//***************************************************************************
// @brief 소멸자 — 아직 실행 중이면 Stop()으로 정리합니다.
//***************************************************************************
CGatewayServerMain::~CGatewayServerMain()
{
	Stop();
}

//***************************************************************************
// @brief 게이트웨이를 구동합니다.
// @details 존 서버용 서비스를 먼저 시작해 존 서버가 등록할 수 있게 한 뒤, 클라이언트용 서비스를 시작한다.
// @param options Gateway 설정
// @return 두 서비스가 모두 시작되면 true
//***************************************************************************
bool CGatewayServerMain::Start(const SGatewayOptions& options)
{
	_options = options;

	// 서버 간/클라이언트 연결 모두 작은 패킷을 자주 주고받으므로 Nagle을 끄고, 조용히 끊긴 연결을 keep-alive로 감지한다.
	CIocpSession::SocketOptions socketOptions;
	socketOptions.noDelay = true;
	socketOptions.keepAlive = true;

	// 1. 존 서버용 서비스
	_zoneCore = MakeShared<CIocpCore>();

	SessionFactory zoneFactory = [this]() -> CSessionRef
		{
			return std::make_shared<CGatewayZoneSession>(this);
		};

	EngineCoreRef zoneEngine = _zoneCore;
	CNetServiceRef zoneService = CNetworkFactory::CreateServerService(
		zoneEngine, CNetAddress(_options.zoneBindIp, _options.zonePort), zoneFactory, _options.maxZoneSessions, _options.zoneWorkerCount);

	_zoneService = std::static_pointer_cast<CIocpServerService>(zoneService);
	if( _zoneService == nullptr )
		return false;

	_zoneService->SetEnforceMaxSessionCount(true);
	_zoneService->SetSessionSocketOptions(socketOptions);

	if( !_zoneService->Start() )
	{
		_zoneService = nullptr;
		return false;
	}

	// 2. 클라이언트용 서비스
	_clientCore = MakeShared<CIocpCore>();

	SessionFactory clientFactory = [this]() -> CSessionRef
		{
			return std::make_shared<CGatewayClientSession>(this);
		};

	EngineCoreRef clientEngine = _clientCore;
	CNetServiceRef clientService = CNetworkFactory::CreateServerService(
		clientEngine, CNetAddress(_options.clientBindIp, _options.clientPort), clientFactory, _options.maxClientSessions, _options.clientWorkerCount);

	_clientService = std::static_pointer_cast<CIocpServerService>(clientService);
	if( _clientService == nullptr )
	{
		Stop();
		return false;
	}

	_clientService->SetEnforceMaxSessionCount(true);
	_clientService->SetSessionSocketOptions(socketOptions);

	if( !_clientService->Start() )
	{
		Stop();
		return false;
	}

	// 3. 존 서버 하트비트 타임아웃 감시 시작
	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		_maintenanceStop = false;
	}
	_maintenanceThread = std::thread([this]() { MaintenanceLoop(); });

	return true;
}

//***************************************************************************
// @brief 게이트웨이를 정지합니다.
// @details 클라이언트 서비스를 먼저 닫는다 — 각 클라이언트 세션의 OnDisconnected()가 존 서버로 퇴장을 통지하므로
//          존 서버 연결이 아직 살아있어야 한다.
//***************************************************************************
void CGatewayServerMain::Stop()
{
	{
		std::lock_guard<std::mutex> lock(_maintenanceMutex);
		_maintenanceStop = true;
	}
	_maintenanceCv.notify_all();

	if( _maintenanceThread.joinable() )
		_maintenanceThread.join();

	if( _clientService )
	{
		_clientService->Close();
		_clientService.reset();
	}

	if( _zoneService )
	{
		_zoneService->Close();
		_zoneService.reset();
	}

	_clientCore.reset();
	_zoneCore.reset();
}

//***************************************************************************
// @brief 클라이언트 연결 완료 시 호출됩니다. 존 서버 응답 라우팅을 위해 세션 ID로 등록합니다.
// @param session 연결된 클라이언트 세션
//***************************************************************************
void CGatewayServerMain::OnClientConnected(const std::shared_ptr<CGatewayClientSession>& session)
{
	PRWriteLockGuard guard(_clientLock, __FUNCTION__);
	_clients[session->GetSessionId()] = session;
}

//***************************************************************************
// @brief 클라이언트 연결 종료 시 호출됩니다.
// @details 존에 있었다면 존 서버에 퇴장을 통지하고 슬롯을 반환한다(존 입장 진행 중이던 경우 포함).
//          그 뒤 로그인 레지스트리와 클라이언트 레지스트리에서 제거한다.
// @param session 종료된 클라이언트 세션
//***************************************************************************
void CGatewayServerMain::OnClientDisconnected(CGatewayClientSession& session)
{
	DetachFromZone(session);

	const uint64 sessionId = session.GetSessionId();

	if( session.IsLoggedIn() )
	{
		PRWriteLockGuard guard(_charLock, __FUNCTION__);
		auto it = _chars.find(session.GetCharId());
		if( it != _chars.end() && it->second == sessionId )
			_chars.erase(it);
	}

	{
		PRWriteLockGuard guard(_clientLock, __FUNCTION__);
		_clients.erase(sessionId);
	}
}

//***************************************************************************
// @brief 클라이언트 패킷 하나를 처리합니다.
//        2000 이상의 패킷은 해석하지 않고 현재 존으로 중계하고, 그 미만은 Gateway 패킷으로 처리한다.
// @param client 패킷을 보낸 클라이언트 세션
// @param header 완전한 패킷 (크기 범위는 프레이밍 단계에서 이미 검증됨)
// @return 프로토콜 위반(속도 제한 초과, 알 수 없는 패킷, 크기 부족)이면 false(연결 종료)
//***************************************************************************
bool CGatewayServerMain::OnClientPacket(CGatewayClientSession& client, const PacketHeader* header)
{
	if( !client.AllowPacket() )
	{
		LOG_WARNING(_T("[CGatewayServerMain] client packet rate exceeded. sessionId=%llu"), client.GetSessionId());
		return false;
	}

	if( header->type >= kClientRelayTypeMin )
		return RelayClientPacket(client, header);

	switch( static_cast<EGatewayPacketType>(header->type) )
	{
	case EGatewayPacketType::GwLoginReq:		return HandleLogin(client, header);
	case EGatewayPacketType::GwEnterZoneReq:	return HandleEnterZone(client, header);
	case EGatewayPacketType::GwLeaveZoneReq:	return HandleLeaveZone(client, header);
	default:
		LOG_ERROR(_T("[CGatewayServerMain] unknown client packet type: %u"), static_cast<unsigned>(header->type));
		return false;
	}
}

//***************************************************************************
// @brief 존 서버 패킷 하나를 처리합니다. 등록 전에는 등록 요청만 허용한다.
// @param session 패킷을 보낸 존 서버 세션
// @param header  완전한 패킷
// @return 프로토콜 위반이면 false(연결 종료)
//***************************************************************************
bool CGatewayServerMain::OnZonePacket(CGatewayZoneSession& session, const PacketHeader* header)
{
	const EZoneServerPacketType type = static_cast<EZoneServerPacketType>(header->type);

	if( session.GetZoneId() == 0 )
		return type == EZoneServerPacketType::ZoneRegisterReq && HandleZoneRegister(session, header);

	switch( type )
	{
	case EZoneServerPacketType::ZoneHeartbeat:		return HandleZoneHeartbeat(session, header);
	case EZoneServerPacketType::ZoneEnterRes:		return HandleZoneEnterRes(session, header);
	case EZoneServerPacketType::ZoneKickNotify:		return HandleZoneKick(session, header);
	case EZoneServerPacketType::ZoneRelayToClient:	return HandleRelayToClient(session, header);
	default:
		LOG_ERROR(_T("[CGatewayServerMain] unknown zone packet type: %u"), static_cast<unsigned>(header->type));
		return false;
	}
}

//***************************************************************************
// @brief 존 서버 연결이 종료되면 호출됩니다. 라우팅 테이블에서 제거하고, 그 존에 있던 클라이언트에 소실을 알립니다.
// @details 이미 새 세션으로 교체된 노드라면(존 서버가 빠르게 재시작) 아무것도 하지 않는다.
// @param session 종료된 존 서버 세션 (동일성 비교에만 사용)
//***************************************************************************
void CGatewayServerMain::OnZoneSessionClosed(CGatewayZoneSession* session)
{
	const uint32 zoneId = session->GetZoneId();
	if( zoneId == 0 || !_router.Unregister(zoneId, session) )
		return;

	LOG_WARNING(_T("[CGatewayServerMain] zone server lost. zoneId=%u"), zoneId);

	// 해당 존에 있던 클라이언트를 락 안에서 수집하고, 락 밖에서 통지한다.
	// 존 서버가 죽는 드문 이벤트라 전체 순회(O(N))를 허용한다.
	std::vector<std::shared_ptr<CGatewayClientSession>> affected;
	{
		PRReadLockGuard guard(_clientLock, __FUNCTION__);
		for( const auto& kv : _clients )
		{
			std::shared_ptr<CGatewayClientSession> client = kv.second.lock();
			if( client != nullptr && client->GetZoneId() == zoneId )
				affected.push_back(std::move(client));
		}
	}

	for( const std::shared_ptr<CGatewayClientSession>& client : affected )
	{
		if( !client->ClearZoneIf(zoneId) )
			continue;

		GwZoneLostNotifyPacket notify{};
		InitZonePacket(notify, EGatewayPacketType::GwZoneLostNotify);
		notify.zoneId = zoneId;
		client->Send(&notify, notify.size);
	}
}

//***************************************************************************
// @brief 로그인 요청을 처리합니다.
// @param client 요청한 클라이언트 세션
// @param header GwLoginReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleLogin(CGatewayClientSession& client, const PacketHeader* header)
{
	if( header->size < sizeof(GwLoginReqPacket) )
		return false;

	const GwLoginReqPacket* req = static_cast<const GwLoginReqPacket*>(header);

	GwLoginResPacket res{};
	InitZonePacket(res, EGatewayPacketType::GwLoginRes);
	res.clientSessionId = client.GetSessionId();

	EGatewayResult result = EGatewayResult::Ok;

	if( client.IsLoggedIn() )
	{
		result = EGatewayResult::AlreadyLoggedIn;
	}
	else if( req->charId == 0 || !AuthenticateClient(client, req->charId) )
	{
		result = EGatewayResult::InvalidCharId;
	}
	else
	{
		// 같은 캐릭터가 다른 세션으로 이미 접속 중이면 거부한다(존 엔티티 ID 충돌 방지).
		PRWriteLockGuard guard(_charLock, __FUNCTION__);
		if( !_chars.emplace(req->charId, client.GetSessionId()).second )
			result = EGatewayResult::AlreadyLoggedIn;
	}

	if( result == EGatewayResult::Ok )
		client.MarkLoggedIn(req->charId);

	res.success = (result == EGatewayResult::Ok) ? 1 : 0;
	res.reason = static_cast<uint8>(result);
	client.Send(&res, res.size);
	return true;
}

//***************************************************************************
// @brief 존 입장 요청을 처리합니다.
// @details 세션이 존 슬롯을 먼저 점유(TryAssignZone)해 중복 입장을 막고, 존 정원 슬롯을 예약한 뒤
//          존 서버에 입장을 요청한다. 최종 결과는 존 서버의 ZoneEnterRes로 비동기 통지된다(HandleZoneEnterRes).
// @param client 요청한 클라이언트 세션
// @param header GwEnterZoneReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleEnterZone(CGatewayClientSession& client, const PacketHeader* header)
{
	if( header->size < sizeof(GwEnterZoneReqPacket) )
		return false;

	const uint32 zoneId = static_cast<const GwEnterZoneReqPacket*>(header)->zoneId;

	if( !client.IsLoggedIn() )
	{
		SendEnterZoneRes(client, EGatewayResult::NotLoggedIn, zoneId, 0.0f, 0.0f);
		return true;
	}

	if( zoneId == 0 )
	{
		SendEnterZoneRes(client, EGatewayResult::ZoneNotFound, zoneId, 0.0f, 0.0f);
		return true;
	}

	if( !client.TryAssignZone(zoneId) )
	{
		SendEnterZoneRes(client, EGatewayResult::AlreadyInZone, zoneId, 0.0f, 0.0f);
		return true;
	}

	std::shared_ptr<SZoneNode> node = _router.Find(zoneId);
	std::shared_ptr<CGatewayZoneSession> zoneSession = node != nullptr ? node->session.lock() : nullptr;
	if( zoneSession == nullptr )
	{
		client.ClearZoneIf(zoneId);
		SendEnterZoneRes(client, EGatewayResult::ZoneNotFound, zoneId, 0.0f, 0.0f);
		return true;
	}

	if( !CZoneRouter::TryReserveSlot(*node) )
	{
		client.ClearZoneIf(zoneId);
		SendEnterZoneRes(client, EGatewayResult::ZoneFull, zoneId, 0.0f, 0.0f);
		return true;
	}

	ZoneEnterReqPacket enter{};
	InitZonePacket(enter, EZoneServerPacketType::ZoneEnterReq);
	enter.clientSessionId = client.GetSessionId();
	enter.charId = client.GetCharId();

	if( !zoneSession->Send(&enter, enter.size) )
	{
		CZoneRouter::ReleaseSlot(*node);
		client.ClearZoneIf(zoneId);
		SendEnterZoneRes(client, EGatewayResult::ZoneError, zoneId, 0.0f, 0.0f);
	}

	return true;
}

//***************************************************************************
// @brief 존 퇴장 요청을 처리합니다.
// @param client 요청한 클라이언트 세션
// @param header GwLeaveZoneReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleLeaveZone(CGatewayClientSession& client, const PacketHeader* header)
{
	(void)header;

	const uint32 zoneId = DetachFromZone(client);

	GwLeaveZoneResPacket res{};
	InitZonePacket(res, EGatewayPacketType::GwLeaveZoneRes);
	res.success = zoneId != 0 ? 1 : 0;
	res.reason = static_cast<uint8>(zoneId != 0 ? EGatewayResult::Ok : EGatewayResult::NotInZone);
	client.Send(&res, res.size);
	return true;
}

//***************************************************************************
// @brief 클라이언트 패킷(2000 이상)을 해석하지 않고 현재 존 서버로 중계합니다.
//        로그인하지 않았거나 입장이 확정된 존이 없으면 조용히 버린다.
// @param client 패킷을 보낸 클라이언트 세션
// @param header 클라이언트 패킷 원문
// @return 항상 true (중계 실패는 위반이 아님)
//***************************************************************************
bool CGatewayServerMain::RelayClientPacket(CGatewayClientSession& client, const PacketHeader* header)
{
	if( !client.IsLoggedIn() || !client.IsEnterConfirmed() )
		return true;

	const uint32 zoneId = client.GetZoneId();
	if( zoneId == 0 )
		return true;

	std::shared_ptr<CGatewayZoneSession> zoneSession = _router.FindSession(zoneId);
	if( zoneSession == nullptr )
		return true;

	thread_local BYTE buffer[kMaxServerPacketBytes];

	uint16 totalSize = 0;
	if( !BuildRelayPacket<ZoneRelayToZonePacket>(EZoneServerPacketType::ZoneRelayToZone, client.GetSessionId(),
		header, header->size, buffer, sizeof(buffer), totalSize) )
		return true;

	zoneSession->Send(buffer, totalSize);
	return true;
}

//***************************************************************************
// @brief 존 서버 등록 요청을 처리합니다. 거부된 존 서버는 스스로 종료해야 하므로 연결은 끊지 않고 응답만 보낸다.
// @param session 등록을 요청한 존 서버 세션
// @param header  ZoneRegisterReqPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleZoneRegister(CGatewayZoneSession& session, const PacketHeader* header)
{
	if( header->size < sizeof(ZoneRegisterReqPacket) )
		return false;

	const ZoneRegisterReqPacket* req = static_cast<const ZoneRegisterReqPacket*>(header);

	ZoneRegisterResPacket res{};
	InitZonePacket(res, EZoneServerPacketType::ZoneRegisterRes);
	res.heartbeatIntervalMs = _options.heartbeatIntervalMs;

	EZoneRegisterResult result = EZoneRegisterResult::Ok;

	if( req->protocolVersion != kZoneProtocolVersion )
	{
		result = EZoneRegisterResult::VersionMismatch;
	}
	else
	{
		// 신뢰할 수 없는 입력이므로 NUL 종단을 강제한다.
		char serverName[kServerNameBytes + 1];
		std::memcpy(serverName, req->serverName, kServerNameBytes);
		serverName[kServerNameBytes] = '\0';

		std::shared_ptr<SZoneNode> node;
		result = _router.Register(std::static_pointer_cast<CGatewayZoneSession>(session.shared_from_this()),
			req->zoneId, static_cast<EZoneKind>(req->kind), req->capacity, serverName, SteadyNowMs(), node);

		if( result == EZoneRegisterResult::Ok )
		{
			session.BindZone(req->zoneId, std::move(node));
			LOG_INFO(_T("[CGatewayServerMain] zone registered. zoneId=%u, kind=%u, capacity=%u"),
				req->zoneId, static_cast<unsigned>(req->kind), req->capacity);
		}
		else
		{
			LOG_WARNING(_T("[CGatewayServerMain] zone registration rejected. zoneId=%u, reason=%u"),
				req->zoneId, static_cast<unsigned>(result));
		}
	}

	res.success = (result == EZoneRegisterResult::Ok) ? 1 : 0;
	res.reason = static_cast<uint8>(result);
	session.Send(&res, res.size);
	return true;
}

//***************************************************************************
// @brief 존 서버 하트비트를 처리합니다. 생존 시각과 부하 정보를 노드에 기록한다.
// @param session 하트비트를 보낸 존 서버 세션
// @param header  ZoneHeartbeatPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleZoneHeartbeat(CGatewayZoneSession& session, const PacketHeader* header)
{
	if( header->size < sizeof(ZoneHeartbeatPacket) )
		return false;

	const ZoneHeartbeatPacket* heartbeat = static_cast<const ZoneHeartbeatPacket*>(header);
	const std::shared_ptr<SZoneNode>& node = session.GetNode();
	if( node == nullptr )
		return false;

	node->lastHeartbeatMs.store(SteadyNowMs(), std::memory_order_relaxed);
	node->reportedUsers.store(heartbeat->userCount, std::memory_order_relaxed);
	node->lastTickUs.store(heartbeat->lastTickUs, std::memory_order_relaxed);
	node->maxTickUs.store(heartbeat->maxTickUs, std::memory_order_relaxed);
	return true;
}

//***************************************************************************
// @brief 존 서버의 입장 응답을 클라이언트에 전달합니다.
// @details 클라이언트가 이미 떠났거나 다른 존으로 옮겨 점유 존이 다르면 응답을 무시한다.
//          (떠나는 경우의 퇴장 통지는 이미 존 서버로 전달돼 있다.)
// @param session 응답을 보낸 존 서버 세션
// @param header  ZoneEnterResPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleZoneEnterRes(CGatewayZoneSession& session, const PacketHeader* header)
{
	if( header->size < sizeof(ZoneEnterResPacket) )
		return false;

	const ZoneEnterResPacket* res = static_cast<const ZoneEnterResPacket*>(header);
	const uint32 zoneId = session.GetZoneId();

	std::shared_ptr<CGatewayClientSession> client = FindClient(res->clientSessionId);
	if( client == nullptr || client->GetZoneId() != zoneId || client->GetCharId() != res->charId )
		return true;

	if( res->success )
	{
		client->SetEnterConfirmed(true);
		SendEnterZoneRes(*client, EGatewayResult::Ok, zoneId, res->spawnX, res->spawnY);
		return true;
	}

	if( client->ClearZoneIf(zoneId) )
	{
		if( const std::shared_ptr<SZoneNode>& node = session.GetNode() )
			CZoneRouter::ReleaseSlot(*node);
	}

	SendEnterZoneRes(*client, EGatewayResult::ZoneError, zoneId, 0.0f, 0.0f);
	return true;
}

//***************************************************************************
// @brief 존 서버가 플레이어를 내보냈을 때 클라이언트의 존 점유를 해제하고 알립니다.
// @param session 통지를 보낸 존 서버 세션
// @param header  ZoneKickNotifyPacket
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleZoneKick(CGatewayZoneSession& session, const PacketHeader* header)
{
	if( header->size < sizeof(ZoneKickNotifyPacket) )
		return false;

	const ZoneKickNotifyPacket* kick = static_cast<const ZoneKickNotifyPacket*>(header);
	const uint32 zoneId = session.GetZoneId();

	std::shared_ptr<CGatewayClientSession> client = FindClient(kick->clientSessionId);
	if( client == nullptr || !client->ClearZoneIf(zoneId) )
		return true;

	if( const std::shared_ptr<SZoneNode>& node = session.GetNode() )
		CZoneRouter::ReleaseSlot(*node);

	GwZoneLostNotifyPacket notify{};
	InitZonePacket(notify, EGatewayPacketType::GwZoneLostNotify);
	notify.zoneId = zoneId;
	client->Send(&notify, notify.size);
	return true;
}

//***************************************************************************
// @brief 존 서버가 보낸 클라이언트용 패킷을 해당 클라이언트로 전달합니다.
//        그 클라이언트가 지금 이 존에 있을 때만 전달한다(다른 존으로 옮긴 뒤 늦게 도착한 패킷 차단).
// @param session 중계를 요청한 존 서버 세션
// @param header  ZoneRelayToClientPacket + 클라이언트 패킷 원문
// @return 프로토콜 위반이면 false
//***************************************************************************
bool CGatewayServerMain::HandleRelayToClient(CGatewayZoneSession& session, const PacketHeader* header)
{
	if( header->size < sizeof(ZoneRelayToClientPacket) + sizeof(PacketHeader) )
		return false;

	const ZoneRelayToClientPacket* relay = static_cast<const ZoneRelayToClientPacket*>(header);
	const BYTE* payload = reinterpret_cast<const BYTE*>(relay) + sizeof(ZoneRelayToClientPacket);
	const size_t payloadSize = static_cast<size_t>(header->size) - sizeof(ZoneRelayToClientPacket);

	// 원문 헤더의 size가 실제 페이로드 크기와 맞아야 한다.
	const PacketHeader* inner = reinterpret_cast<const PacketHeader*>(payload);
	if( inner->size != payloadSize )
		return false;

	std::shared_ptr<CGatewayClientSession> client = FindClient(relay->clientSessionId);
	if( client != nullptr && client->GetZoneId() == session.GetZoneId() )
		client->Send(payload, static_cast<uint16>(payloadSize));

	return true;
}

//***************************************************************************
// @brief 클라이언트 인증을 수행합니다.
// @details [임시] 현재는 항상 통과시킨다. 실제 서비스에서는 로그인 서버가 발급한 인증 토큰
//          (예: Redis의 단기 토큰)을 검증하고, 토큰에 묶인 캐릭터 ID와 charId가 일치하는지 확인해야 한다.
//          이 함수를 구현하기 전까지 Gateway의 클라이언트 포트를 외부에 공개하면 안 된다.
// @param client 인증할 클라이언트 세션
// @param charId 클라이언트가 주장하는 캐릭터 ID
// @return 인증 성공 시 true
//***************************************************************************
bool CGatewayServerMain::AuthenticateClient(CGatewayClientSession& client, uint64 charId)
{
	(void)client;
	(void)charId;
	return true;
}

//***************************************************************************
// @brief 클라이언트의 존 점유를 해제하고, 존 서버에 퇴장을 통지하며, 정원 슬롯을 반환합니다.
// @param client 대상 클라이언트 세션
// @return 해제 전 존 ID (존에 없었으면 0)
//***************************************************************************
uint32 CGatewayServerMain::DetachFromZone(CGatewayClientSession& client)
{
	const uint32 zoneId = client.ClearZone();
	if( zoneId == 0 )
		return 0;

	std::shared_ptr<SZoneNode> node = _router.Find(zoneId);
	if( node == nullptr )
		return zoneId;

	CZoneRouter::ReleaseSlot(*node);

	if( std::shared_ptr<CGatewayZoneSession> zoneSession = node->session.lock() )
	{
		ZoneLeaveReqPacket leave{};
		InitZonePacket(leave, EZoneServerPacketType::ZoneLeaveReq);
		leave.clientSessionId = client.GetSessionId();
		leave.charId = client.GetCharId();
		zoneSession->Send(&leave, leave.size);
	}

	return zoneId;
}

//***************************************************************************
// @brief 클라이언트 세션 ID로 세션을 찾습니다.
// @param clientSessionId 클라이언트 세션 ID
// @return 세션, 없거나 이미 사라졌으면 nullptr
//***************************************************************************
std::shared_ptr<CGatewayClientSession> CGatewayServerMain::FindClient(uint64 clientSessionId) const
{
	PRReadLockGuard guard(_clientLock, __FUNCTION__);

	auto it = _clients.find(clientSessionId);
	return it != _clients.end() ? it->second.lock() : nullptr;
}

//***************************************************************************
// @brief 존 입장 응답을 클라이언트에 전송합니다.
// @param client  받을 클라이언트 세션
// @param reason  처리 결과 (Ok면 성공)
// @param zoneId  대상 존 ID
// @param spawnX  성공 시 서버 확정 스폰 X
// @param spawnY  성공 시 서버 확정 스폰 Y
//***************************************************************************
void CGatewayServerMain::SendEnterZoneRes(CGatewayClientSession& client, EGatewayResult reason, uint32 zoneId, float spawnX, float spawnY)
{
	GwEnterZoneResPacket res{};
	InitZonePacket(res, EGatewayPacketType::GwEnterZoneRes);
	res.success = (reason == EGatewayResult::Ok) ? 1 : 0;
	res.reason = static_cast<uint8>(reason);
	res.zoneId = zoneId;
	res.spawnX = spawnX;
	res.spawnY = spawnY;
	client.Send(&res, res.size);
}

//***************************************************************************
// @brief 감시 스레드 본체. 짧은 틱마다 존 서버 하트비트 타임아웃을 점검합니다.
//***************************************************************************
void CGatewayServerMain::MaintenanceLoop()
{
	std::unique_lock<std::mutex> lock(_maintenanceMutex);
	while( !_maintenanceStop )
	{
		if( _maintenanceCv.wait_for(lock, std::chrono::milliseconds(kMaintenanceTickMs), [this]() { return _maintenanceStop; }) )
			break;

		lock.unlock();
		CheckZoneHeartbeats();
		lock.lock();
	}
}

//***************************************************************************
// @brief 하트비트가 heartbeatTimeoutMs 이상 끊긴 존 서버의 연결을 끊습니다.
// @details 연결을 끊으면 OnDisconnected -> OnZoneSessionClosed 경로로 라우팅 제거와 클라이언트 정리가 이어진다.
//          종료 처리가 끝나기 전에 같은 세션을 반복해서 끊지 않도록, 끊은 노드의 기준 시각을 갱신한다.
//***************************************************************************
void CGatewayServerMain::CheckZoneHeartbeats()
{
	const int64 now = SteadyNowMs();

	for( const std::shared_ptr<SZoneNode>& node : _router.Snapshot() )
	{
		if( now - node->lastHeartbeatMs.load(std::memory_order_relaxed) <= static_cast<int64>(_options.heartbeatTimeoutMs) )
			continue;

		LOG_WARNING(_T("[CGatewayServerMain] zone heartbeat timeout. zoneId=%u"), node->zoneId);
		node->lastHeartbeatMs.store(now, std::memory_order_relaxed);

		if( std::shared_ptr<CGatewayZoneSession> session = node->session.lock() )
			session->Disconnect(Iocp::CloseReason::ForcedClose);
	}
}