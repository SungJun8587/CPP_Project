
//***************************************************************************
// BattleZone.cpp: implementation of the CBattleZone class.
//
//***************************************************************************

#include "pch.h"
#include "BattleZone.h"

#include <cmath>

//***************************************************************************
// @brief 전투 존을 생성한다. 스레드는 Start() 호출 시 시작된다.
// @param config      필드 존 설정
// @param server      클라이언트 패킷 중계 경로를 가진 존 서버
// @param spawnPos    입장 시 스폰 위치
// @param playerSpeed 플레이어 기본 이동 속도 (units/sec)
//***************************************************************************
CBattleZone::CBattleZone(const FieldZoneConfig& config, CZoneServerMain* server, const Vec2& spawnPos, float playerSpeed)
	: CFieldZone<>(config)
	, _server(server)
	, _spawnPos(spawnPos)
	, _playerSpeed(playerSpeed)
	, _playerCount(0)
{
}

//***************************************************************************
// @brief 소멸자. 틱 스레드가 이 객체의 멤버를 사용하지 않도록 먼저 정지시킨다.
//***************************************************************************
CBattleZone::~CBattleZone()
{
	Stop();
}

//***************************************************************************
// @brief Gateway가 중계한 클라이언트 입장 요청을 처리한다.
// @details 입장 응답을 AddEntity()보다 먼저 보낸다. AddEntity()가 AOI 진입 통지로 스폰 패킷을 보내기 때문에,
//          순서를 바꾸면 클라이언트가 입장 성공 응답보다 스폰 패킷을 먼저 받는다.
//          같은 연결로 나가는 패킷은 순서가 보장된다.
// @param clientSessionId Gateway 클라이언트 세션 ID
// @param charId          입장하는 캐릭터 ID
//***************************************************************************
void CBattleZone::HandleEnter(uint64 clientSessionId, uint64 charId)
{
	ZoneEnterResPacket res{};
	InitZonePacket(res, EZoneServerPacketType::ZoneEnterRes);
	res.clientSessionId = clientSessionId;
	res.charId = charId;
	res.spawnX = _spawnPos.x;
	res.spawnY = _spawnPos.y;

	if( FindEntity(charId) != nullptr || _sessionToEntity.find(clientSessionId) != _sessionToEntity.end() )
	{
		res.success = 0;
		res.reason = static_cast<uint8>(EZoneEnterResult::DuplicateEntity);
		_server->SendToGateway(&res, res.size);
		return;
	}

	res.success = 1;
	res.reason = static_cast<uint8>(EZoneEnterResult::Ok);
	_server->SendToGateway(&res, res.size);

	std::unique_ptr<CZonePlayer> player = std::make_unique<CZonePlayer>(charId, clientSessionId, this);
	player->SetSpawnPos(_spawnPos);
	player->SetSpeed(_playerSpeed);

	CZoneEntity* added = AddEntity(std::move(player));
	if( added == nullptr )
	{
		// 사전 검사를 통과했는데도 실패한 비정상 경로 — 이미 성공 응답을 보냈으므로 추방 통지로 정정한다.
		ZoneKickNotifyPacket kick{};
		InitZonePacket(kick, EZoneServerPacketType::ZoneKickNotify);
		kick.clientSessionId = clientSessionId;
		kick.reason = static_cast<uint8>(EZoneKickReason::ZoneError);
		_server->SendToGateway(&kick, kick.size);
		return;
	}

	_sessionToEntity.emplace(clientSessionId, added);
	_playerCount.fetch_add(1, std::memory_order_relaxed);
}

//***************************************************************************
// @brief Gateway가 중계한 클라이언트 퇴장 요청을 처리한다(연결 종료 포함).
//        엔티티 제거 시 AOI 이탈 통지로 주변 플레이어에게 디스폰 패킷이 나간다.
// @param clientSessionId Gateway 클라이언트 세션 ID
// @param charId          퇴장하는 캐릭터 ID
//***************************************************************************
void CBattleZone::HandleLeave(uint64 clientSessionId, uint64 charId)
{
	auto it = _sessionToEntity.find(clientSessionId);
	if( it == _sessionToEntity.end() || it->second->GetId() != charId )
		return;

	_sessionToEntity.erase(it);
	RemoveEntity(charId);
	_playerCount.fetch_sub(1, std::memory_order_relaxed);
}

//***************************************************************************
// @brief Gateway가 중계한 클라이언트 패킷을 처리한다. 이후 단계(공격/스킬)의 패킷도 여기서 분기한다.
// @param clientSessionId 패킷을 보낸 클라이언트 세션 ID
// @param header          클라이언트 패킷 (크기는 중계 단계에서 이미 검증됨)
//***************************************************************************
void CBattleZone::HandleClientPacket(uint64 clientSessionId, const PacketHeader* header)
{
	auto it = _sessionToEntity.find(clientSessionId);
	if( it == _sessionToEntity.end() )
		return;

	switch( static_cast<EZoneClientPacketType>(header->type) )
	{
	case EZoneClientPacketType::ZcMoveReq:
	{
		if( header->size < sizeof(ZcMoveReqPacket) )
			return;

		const ZcMoveReqPacket* req = static_cast<const ZcMoveReqPacket*>(header);

		// NaN/Inf는 위치 검증과 AOI 셀 계산을 오염시키므로 입구에서 거른다.
		if( !std::isfinite(req->posX) || !std::isfinite(req->posY) ||
			!std::isfinite(req->destX) || !std::isfinite(req->destY) )
			return;

		Vec2 clientPos;
		clientPos.x = req->posX;
		clientPos.y = req->posY;

		Vec2 dest;
		dest.x = req->destX;
		dest.y = req->destY;

		RequestMove(*it->second, clientPos, dest);
		break;
	}

	default:
		break;
	}
}

//***************************************************************************
// @brief 시야에 들어온 엔티티의 현재 상태를 해당 플레이어에게 보낸다.
// @param to    받을 플레이어
// @param about 시야에 들어온 엔티티
//***************************************************************************
void CBattleZone::SendSpawn(CZonePlayer& to, CZoneEntity& about)
{
	ZcSpawnNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSpawnNotify);
	notify.entityId = about.GetId();
	notify.entityType = static_cast<uint8>(about.GetType());
	notify.posX = about.GetPos().x;
	notify.posY = about.GetPos().y;
	notify.destX = about.GetDest().x;
	notify.destY = about.GetDest().y;
	notify.speed = about.GetSpeed();
	notify.moving = about.IsMoving() ? 1 : 0;

	SendToEntity(to, &notify, notify.size);
}

//***************************************************************************
// @brief 시야에서 벗어난 엔티티를 해당 플레이어에게 알린다.
// @param to    받을 플레이어
// @param about 시야에서 벗어난 엔티티
//***************************************************************************
void CBattleZone::SendDespawn(CZonePlayer& to, CZoneEntity& about)
{
	ZcDespawnNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcDespawnNotify);
	notify.entityId = about.GetId();

	SendToEntity(to, &notify, notify.size);
}

//***************************************************************************
// @brief 이동이 시작(또는 목적지가 갱신)되면 본인과 주변 플레이어에게 이동 정보를 알린다.
// @param entity 이동을 시작한 엔티티
//***************************************************************************
void CBattleZone::OnEntityMoveStarted(CZoneEntity& entity)
{
	ZcMoveNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcMoveNotify);
	notify.entityId = entity.GetId();
	notify.startX = entity.GetPos().x;
	notify.startY = entity.GetPos().y;
	notify.destX = entity.GetDest().x;
	notify.destY = entity.GetDest().y;
	notify.speed = entity.GetSpeed();
	notify.serverTimeMs = GetNowMs();

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 이동이 끝나면 본인과 주변 플레이어에게 최종 위치를 알린다.
// @param entity  이동을 마친 엔티티
// @param arrived 목적지 도착이면 true, 중단이면 false
//***************************************************************************
void CBattleZone::OnEntityMoveStopped(CZoneEntity& entity, bool arrived)
{
	ZcStopNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcStopNotify);
	notify.entityId = entity.GetId();
	notify.posX = entity.GetPos().x;
	notify.posY = entity.GetPos().y;
	notify.arrived = arrived ? 1 : 0;

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 순간이동이 끝나면 주변에 새 위치를 알리고, 본인에게는 위치 보정 패킷을 보낸다.
// @param entity 순간이동한 엔티티
//***************************************************************************
void CBattleZone::OnEntityTeleported(CZoneEntity& entity)
{
	ZcStopNotifyPacket stop{};
	InitZonePacket(stop, EZoneClientPacketType::ZcStopNotify);
	stop.entityId = entity.GetId();
	stop.posX = entity.GetPos().x;
	stop.posY = entity.GetPos().y;
	stop.arrived = 0;
	BroadcastAround(entity, &stop, stop.size);

	ZcCorrectPosNotifyPacket correct{};
	InitZonePacket(correct, EZoneClientPacketType::ZcCorrectPosNotify);
	correct.reason = static_cast<uint8>(ECorrectPosReason::Teleported);
	correct.posX = entity.GetPos().x;
	correct.posY = entity.GetPos().y;
	SendToEntity(entity, &correct, correct.size);
}

//***************************************************************************
// @brief 이동 요청이 거부되면 해당 클라이언트에 서버 위치로 보정하라고 알린다.
// @param entity 요청한 엔티티
// @param reason 거부 사유
//***************************************************************************
void CBattleZone::OnMoveRejected(CZoneEntity& entity, MoveResult reason)
{
	ZcCorrectPosNotifyPacket correct{};
	InitZonePacket(correct, EZoneClientPacketType::ZcCorrectPosNotify);

	switch( reason )
	{
	case MoveResult::OutOfBounds:		correct.reason = static_cast<uint8>(ECorrectPosReason::OutOfBounds); break;
	case MoveResult::Immobile:			correct.reason = static_cast<uint8>(ECorrectPosReason::Immobile); break;
	default:							correct.reason = static_cast<uint8>(ECorrectPosReason::PositionMismatch); break;
	}

	correct.posX = entity.GetPos().x;
	correct.posY = entity.GetPos().y;
	SendToEntity(entity, &correct, correct.size);
}

//***************************************************************************
// @brief 엔티티가 플레이어면 해당 클라이언트로 패킷을 보낸다. 플레이어가 아니면 무시한다.
// @param target 받을 엔티티
// @param data   패킷 데이터
// @param size   패킷 크기
//***************************************************************************
void CBattleZone::SendToEntity(CZoneEntity& target, const void* data, uint16 size)
{
	if( target.GetType() != EntityType::Player )
		return;

	_server->SendToClient(static_cast<CZonePlayer&>(target).GetClientSessionId(), data, size);
}

//***************************************************************************
// @brief 엔티티 본인과 시야(AOI) 안의 플레이어 전원에게 같은 패킷을 보낸다.
//        현재는 수신자마다 중계 패킷을 따로 만든다(수신자 목록을 한 패킷에 담는 멀티캐스트 중계는 이후 최적화 대상).
// @param entity 기준 엔티티
// @param data   패킷 데이터
// @param size   패킷 크기
//***************************************************************************
void CBattleZone::BroadcastAround(CZoneEntity& entity, const void* data, uint16 size)
{
	SendToEntity(entity, data, size);

	ForEachInAoi(entity, [this, data, size](CZoneEntity& other)
		{
			SendToEntity(other, data, size);
		});
}