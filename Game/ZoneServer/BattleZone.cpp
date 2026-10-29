
//***************************************************************************
// BattleZone.cpp: implementation of the CBattleZone class.
//
//***************************************************************************

#include "pch.h"
#include "../GameCommon/ZoneCommon/ZonePacket.h"
#include "BattleZone.h"
#include "ZoneServerMain.h"

#include <cmath>

//***************************************************************************
// @brief 전투 존을 생성한다. 스레드는 Start() 호출 시 시작된다.
// @param config      필드 존 설정
// @param server      클라이언트 패킷 중계 경로를 가진 존 서버
// @param spawnPos    입장 시 스폰 위치
// @param playerSpeed 플레이어 기본 이동 속도 (units/sec)
// @param playerStats 플레이어 전투 능력치
//***************************************************************************
CBattleZone::CBattleZone(const FieldZoneConfig& config, CZoneServerMain* server, const Vec2& spawnPos, float playerSpeed, const SCombatStats& playerStats)
	: CFieldZone<>(config)
	, _server(server)
	, _spawnPos(spawnPos)
	, _playerSpeed(playerSpeed)
	, _playerStats(playerStats)
	, _playerCount(0)
{
	_recipients.reserve(256);
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
	player->SetCombatStats(_playerStats);

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

	case EZoneClientPacketType::ZcAttackReq:
	{
		if( header->size < sizeof(ZcAttackReqPacket) )
			return;

		const ZcAttackReqPacket* req = static_cast<const ZcAttackReqPacket*>(header);
		RequestAttack(*it->second, req->targetId);
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
	notify.hp = about.GetHp();
	notify.maxHp = about.GetCombatStats().maxHp;
	notify.dead = about.IsAlive() ? 0 : 1;

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
	case MoveResult::Dead:				correct.reason = static_cast<uint8>(ECorrectPosReason::Dead); break;
	default:							correct.reason = static_cast<uint8>(ECorrectPosReason::PositionMismatch); break;
	}

	correct.posX = entity.GetPos().x;
	correct.posY = entity.GetPos().y;
	SendToEntity(entity, &correct, correct.size);
}

//***************************************************************************
// @brief 피해가 적용되면 피해자 주변 전원과 공격자에게 피해 정보를 알린다.
// @param attacker 공격자 (공격자가 없는 피해면 nullptr)
// @param victim   피해자
// @param damage   적용된 피해량
//***************************************************************************
void CBattleZone::OnEntityDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage)
{
	ZcDamageNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcDamageNotify);
	notify.attackerId = attacker != nullptr ? attacker->GetId() : 0;
	notify.targetId = victim.GetId();
	notify.damage = damage;
	notify.hp = victim.GetHp();
	notify.maxHp = victim.GetCombatStats().maxHp;

	BroadcastCombat(victim, attacker, &notify, notify.size);
}

//***************************************************************************
// @brief 엔티티가 사망하면 피해자 주변 전원과 처치자에게 알린다.
// @param victim 사망한 엔티티
// @param killer 처치한 엔티티 (없으면 nullptr)
//***************************************************************************
void CBattleZone::OnEntityDied(CZoneEntity& victim, CZoneEntity* killer)
{
	ZcDeathNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcDeathNotify);
	notify.entityId = victim.GetId();
	notify.killerId = killer != nullptr ? killer->GetId() : 0;

	BroadcastCombat(victim, killer, &notify, notify.size);
}

//***************************************************************************
// @brief 엔티티가 부활하면 리스폰 위치 주변 전원과 본인에게 알린다.
//        이전 위치의 관찰자에게는 AOI 이탈 통지(디스폰)가, 새 위치의 관찰자에게는 진입 통지(스폰)가 따로 나간다.
// @param entity 부활한 엔티티
//***************************************************************************
void CBattleZone::OnEntityRespawned(CZoneEntity& entity)
{
	ZcRespawnNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcRespawnNotify);
	notify.entityId = entity.GetId();
	notify.posX = entity.GetPos().x;
	notify.posY = entity.GetPos().y;
	notify.hp = entity.GetHp();
	notify.maxHp = entity.GetCombatStats().maxHp;
	notify.invulnerableMs = IsInvulnerable(entity) ? static_cast<uint32>(entity.GetInvulnerableUntilMs() - GetNowMs()) : 0;

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 공격 요청이 거부되면 요청한 클라이언트에게만 사유를 알린다.
// @param attacker 공격 요청 엔티티
// @param targetId 요청이 지목한 대상 ID
// @param reason   거부 사유
//***************************************************************************
void CBattleZone::OnAttackRejected(CZoneEntity& attacker, uint64 targetId, AttackResult reason)
{
	ZcAttackRejectNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcAttackRejectNotify);
	notify.targetId = targetId;

	switch( reason )
	{
	case AttackResult::TargetNotFound:	// 대상 없음과 사거리 밖은 구분하지 않는다(엔티티 존재 여부 비노출)
	case AttackResult::OutOfRange:		notify.reason = static_cast<uint8>(EAttackRejectReason::OutOfRange); break;
	case AttackResult::AttackerDead:	notify.reason = static_cast<uint8>(EAttackRejectReason::AttackerDead); break;
	case AttackResult::TargetDead:		notify.reason = static_cast<uint8>(EAttackRejectReason::TargetDead); break;
	case AttackResult::TargetInvulnerable:	notify.reason = static_cast<uint8>(EAttackRejectReason::TargetInvulnerable); break;
	default:							notify.reason = static_cast<uint8>(EAttackRejectReason::InvalidTarget); break;
	}

	SendToEntity(attacker, &notify, notify.size);
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
//        수신자 세션 ID를 모아 멀티캐스트 중계 패킷 하나로 Gateway에 보낸다.
// @param entity 기준 엔티티
// @param data   패킷 데이터
// @param size   패킷 크기
//***************************************************************************
void CBattleZone::BroadcastAround(CZoneEntity& entity, const void* data, uint16 size)
{
	_recipients.clear();

	AddRecipient(entity);
	ForEachInAoi(entity, [this](CZoneEntity& other)
		{
			AddRecipient(other);
		});

	FlushRecipients(data, size);
}

//***************************************************************************
// @brief 피해자 본인과 시야 안 플레이어 전원에게 보내고, 전투 상대(공격자/처치자)가 그 시야에 없었으면 따로 보낸다.
//        사거리가 시야보다 길거나 AOI 히스테리시스로 서로 보이지 않아도 공격자가 결과를 받도록 보장한다.
// @param victim 기준 엔티티(피해자)
// @param other  전투 상대 (없으면 nullptr)
// @param data   패킷 데이터
// @param size   패킷 크기
//***************************************************************************
void CBattleZone::BroadcastCombat(CZoneEntity& victim, CZoneEntity* other, const void* data, uint16 size)
{
	_recipients.clear();

	AddRecipient(victim);

	bool otherVisited = (other == nullptr);
	ForEachInAoi(victim, [this, other, &otherVisited](CZoneEntity& neighbor)
		{
			if( &neighbor == other )
				otherVisited = true;

			AddRecipient(neighbor);
		});

	if( !otherVisited )
		AddRecipient(*other);

	FlushRecipients(data, size);
}

//***************************************************************************
// @brief 엔티티가 플레이어면 수신자 목록에 세션 ID를 추가한다. 플레이어가 아니면 무시한다.
// @param target 수신 후보 엔티티
//***************************************************************************
void CBattleZone::AddRecipient(CZoneEntity& target)
{
	if( target.GetType() == EntityType::Player )
		_recipients.push_back(static_cast<CZonePlayer&>(target).GetClientSessionId());
}

//***************************************************************************
// @brief 모인 수신자 전원에게 같은 패킷을 멀티캐스트 중계로 한 번에 보낸다.
// @param data 패킷 데이터
// @param size 패킷 크기
//***************************************************************************
void CBattleZone::FlushRecipients(const void* data, uint16 size)
{
	_server->SendToClients(_recipients.data(), _recipients.size(), data, size);
}