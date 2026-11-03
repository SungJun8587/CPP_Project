
//***************************************************************************
// BattleZone.cpp: implementation of the CBattleZone class.
//
//***************************************************************************

#include "pch.h"
#include "BattleZone.h"
#include "ZoneServerMain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

//***************************************************************************
// @brief 전투 존을 생성한다. 스레드는 Start() 호출 시 시작된다.
// @param config      필드 존 설정
// @param server      클라이언트 패킷 중계 경로를 가진 존 서버
// @param spawnPos    입장 시 스폰 위치
// @param heroData    선택 가능한 영웅 데이터 (nullptr이면 내장 기본 영웅 하나)
// @param monsterData   몬스터 데이터 (nullptr이면 몬스터 없음)
// @param monsterConfig 몬스터 AI 튜닝값
//***************************************************************************
CBattleZone::CBattleZone(const FieldZoneConfig& config, CZoneServerMain* server, const Vec2& spawnPos, std::shared_ptr<const CHeroData> heroData,
	std::shared_ptr<const CMonsterData> monsterData, const SMonsterSystemConfig& monsterConfig)
	: CFieldZone<>(config)
	, _server(server)
	, _spawnPos(spawnPos)
	, _heroes(heroData != nullptr ? std::move(heroData) : CHeroData::MakeDefault())
	, _monsters(std::make_unique<CMonsterSystem>(*this, std::move(monsterData), monsterConfig))
	, _playerCount(0)
{
	_recipients.reserve(256);
}

//***************************************************************************
// @brief 영웅/몬스터 데이터가 이 존 설정(영역, AOI 시야, 사거리)과 맞는지 검사합니다. Start() 전에 호출합니다.
//        - 영웅 데이터의 값 범위(CHeroData::Validate), 입장 위치가 존 영역 안
//          (벗어나면 입장 응답은 성공으로 보낸 뒤 AddEntity가 실패해 추방 통지로 정정하는 경로를 매번 타게 된다)
//        - 모든 영웅의 공격 판정 거리 <= AOI 최소 보장 시야. 상대(몬스터)의 최고 속도를 반영한다
//        - 몬스터 데이터 검사(CMonsterSystem::Validate). 상대(영웅)의 최고 속도를 반영한다
// @param error [out] 오류 설명
// @return 문제가 없으면 true
//***************************************************************************
bool CBattleZone::ValidateMonsters(std::string& error) const
{
	if( !_heroes->Validate(error) )
		return false;

	if( !IsInBounds(_spawnPos) )
	{
		error = "player spawn position is outside the zone bounds";
		return false;
	}

	if( IsBlocked(_spawnPos) )
	{
		error = "player spawn position is inside an obstacle";
		return false;
	}

	const float monsterSpeed = _monsters->GetMaxSpawnSpeed();
	// 이동속도 버프를 받은 영웅이 낼 수 있는 최고 속도로 검사한다(위치 허용 오차가 속도에 비례해 늘어난다).
	const float buffMul = 1.0f + static_cast<float>(_heroes->GetMaxSpeedBuffPercent()) * 0.01f;
	for( const auto& pair : _heroes->GetHeroes() )
	{
		const SHeroTemplate& hero = pair.second;
		if( !IsAttackRangeSupported(hero.stats.attackRange, (std::max)(hero.speed * buffMul, monsterSpeed)) )
		{
			char buf[160];
			std::snprintf(buf, sizeof(buf), "hero %u: attack reach exceeds the guaranteed AOI view range", hero.id);
			error = buf;
			return false;
		}
	}

	// 스킬은 시전 거리와 효과 반경을 더한 범위까지 영향을 주므로, 그 전체가 AOI 보장 시야 안이어야 한다.
	for( const auto& pair : _heroes->GetHeroes() )
	{
		const SHeroTemplate& hero = pair.second;
		for( const uint32 skillId : hero.skills )
		{
			const SSkillTemplate* skill = _heroes->FindSkill(skillId);
			if( skill != nullptr && !IsAttackRangeSupported(skill->range + skill->radius, (std::max)(hero.speed * buffMul, monsterSpeed)) )
			{
				char buf[160];
				std::snprintf(buf, sizeof(buf), "hero %u skill %u: range + radius exceeds the guaranteed AOI view range", hero.id, skill->id);
				error = buf;
				return false;
			}
		}
	}

	return _monsters->Validate(error, _heroes->GetMaxSpeed() * buffMul);
}

//***************************************************************************
// @brief 이 존의 스폰 테이블대로 몬스터를 배치합니다. Start() 전에 한 번 호출합니다.
// @return 배치된 몬스터 수
//***************************************************************************
uint32 CBattleZone::SpawnMonsters()
{
	return _monsters->SpawnAll();
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
// @param heroId          선택한 영웅 ID. 0이면 기본 영웅(ID가 가장 작은 영웅). 없는 ID면 입장을 거부한다
//***************************************************************************
void CBattleZone::HandleEnter(uint64 clientSessionId, uint64 charId, uint32 heroId)
{
	ZoneEnterResPacket res{};
	InitZonePacket(res, EZoneServerPacketType::ZoneEnterRes);
	res.clientSessionId = clientSessionId;
	res.charId = charId;
	res.spawnX = _spawnPos.x;
	res.spawnY = _spawnPos.y;

	if( charId == 0 || (charId & kMonsterIdBase) != 0 )
	{
		// 0은 "공격자/대상 없음"을 뜻하는 값이라 엔티티 ID로 쓸 수 없다.
		// 최상위 비트는 몬스터 ID 대역이다. 플레이어가 침범하면 몬스터로 오인되어 static_cast가 깨지므로 입구에서 막는다.
		res.success = 0;
		res.reason = static_cast<uint8>(EZoneEnterResult::InvalidCharId);
		_server->SendToGateway(&res, res.size);
		return;
	}

	const SHeroTemplate* hero = _heroes->Resolve(heroId);
	if( hero == nullptr )
	{
		res.success = 0;
		res.reason = static_cast<uint8>(EZoneEnterResult::InvalidHero);
		_server->SendToGateway(&res, res.size);
		return;
	}

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

	std::unique_ptr<CZonePlayer> player = std::make_unique<CZonePlayer>(charId, clientSessionId, this, hero->id);
	player->SetSpawnPos(_spawnPos);
	player->SetSpeed(hero->speed);
	player->SetCombatStats(hero->stats);

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

	// 제거 중 AOI 이탈 통지로 이 플레이어 본인에게 이웃 수만큼 디스폰이 나가지 않도록 먼저 표시한다.
	static_cast<CZonePlayer*>(it->second)->MarkLeaving();

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

	case EZoneClientPacketType::ZcSkillReq:
		HandleSkillReq(*it->second, header);
		break;

	default:
		break;
	}
}

//***************************************************************************
// @brief 스킬 시전 요청을 처리합니다. 영웅이 가진 스킬인지 확인한 뒤 CastSkill에 맡긴다.
//        요청의 좌표/ID는 모두 클라이언트 값이므로 서버가 사거리/시선/대상 상태를 검증한다.
// @param entity 시전자 (세션으로 찾은 플레이어)
// @param header ZcSkillReqPacket
//***************************************************************************
void CBattleZone::HandleSkillReq(CZoneEntity& entity, const PacketHeader* header)
{
	if( header->size < sizeof(ZcSkillReqPacket) )
		return;

	const ZcSkillReqPacket* req = static_cast<const ZcSkillReqPacket*>(header);
	const uint32 skillId = req->skillId;
	const uint64 targetId = req->targetId;
	Vec2 center;
	center.x = req->x;
	center.y = req->y;

	CZonePlayer& player = static_cast<CZonePlayer&>(entity);
	const SHeroTemplate* hero = _heroes->Resolve(player.GetHeroId());
	const SSkillTemplate* skill = hero != nullptr ? _heroes->FindHeroSkill(*hero, skillId) : nullptr;
	if( skill == nullptr )
	{
		SendSkillReject(player, skillId, ESkillRejectReason::UnknownSkill);
		return;
	}

	CastSkill(player, *skill, targetId, center, player.SkillReadyAt(skillId));
}

//***************************************************************************
// @brief 스킬 시전 성공을 시전자 주변 전원에게 알린다. 피해는 ZcDamageNotify로 이어진다.
//***************************************************************************
void CBattleZone::OnSkillCast(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& center, uint32 hitCount)
{
	ZcSkillNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSkillNotify);
	notify.casterId = caster.GetId();
	notify.skillId = skill.id;
	notify.targetId = targetId;
	notify.x = center.x;
	notify.y = center.y;
	notify.hitCount = hitCount;

	BroadcastAround(caster, &notify, notify.size);
}

//***************************************************************************
// @brief 시전 시간이 있는 스킬의 시전 시작을 시전자 주변 전원에게 알린다. 끝나면 ZcSkillNotify/ZcProjectileNotify가 이어진다.
//***************************************************************************
void CBattleZone::OnSkillCastStarted(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& center)
{
	ZcSkillCastNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSkillCastNotify);
	notify.casterId = caster.GetId();
	notify.skillId = skill.id;
	notify.targetId = targetId;
	notify.x = center.x;
	notify.y = center.y;
	notify.castTimeMs = skill.castTimeMs;

	BroadcastAround(caster, &notify, notify.size);
}

//***************************************************************************
// @brief 시전 취소를 시전자 주변 전원에게 알린다.
//***************************************************************************
void CBattleZone::OnSkillCastCancelled(CZoneEntity& caster, const SSkillTemplate& skill, SkillCancelReason reason)
{
	ZcSkillCancelNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSkillCancelNotify);
	notify.casterId = caster.GetId();
	notify.skillId = skill.id;

	ESkillCancelReason code = ESkillCancelReason::InvalidTarget;
	switch( reason )
	{
	case SkillCancelReason::Moved:			code = ESkillCancelReason::Moved; break;
	case SkillCancelReason::Died:			code = ESkillCancelReason::Died; break;
	case SkillCancelReason::OutOfRange:		code = ESkillCancelReason::OutOfRange; break;
	case SkillCancelReason::NoLineOfSight:	code = ESkillCancelReason::NoLineOfSight; break;
	default:								break;
	}
	notify.reason = static_cast<uint8>(code);

	BroadcastAround(caster, &notify, notify.size);
}

//***************************************************************************
// @brief 투사체 발사를 시전자 주변 전원에게 알린다.
//***************************************************************************
void CBattleZone::OnProjectileLaunched(CZoneEntity& caster, const SSkillTemplate& skill, uint32 projectileId, uint64 targetId, const Vec2& from, const Vec2& to, uint32 flightMs)
{
	ZcProjectileNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcProjectileNotify);
	notify.projectileId = projectileId;
	notify.casterId = caster.GetId();
	notify.skillId = skill.id;
	notify.targetId = targetId;
	notify.x0 = from.x;
	notify.y0 = from.y;
	notify.x1 = to.x;
	notify.y1 = to.y;
	notify.flightMs = flightMs;

	BroadcastAround(caster, &notify, notify.size);
}

//***************************************************************************
// @brief 투사체 착탄을 시전자 주변 전원에게 알린다. 피해는 ZcDamageNotify로 이어진다.
//***************************************************************************
void CBattleZone::OnProjectileImpact(CZoneEntity& caster, const SSkillTemplate& skill, uint32 projectileId, const Vec2& center, uint32 hitCount)
{
	ZcSkillImpactNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSkillImpactNotify);
	notify.projectileId = projectileId;
	notify.casterId = caster.GetId();
	notify.skillId = skill.id;
	notify.x = center.x;
	notify.y = center.y;
	notify.hitCount = hitCount;

	BroadcastAround(caster, &notify, notify.size);
}

//***************************************************************************
// @brief 상태 효과가 걸렸거나 갱신된 것을 엔티티 주변 전원에게 알린다.
//***************************************************************************
void CBattleZone::OnStatusApplied(CZoneEntity& entity, const SStatusTemplate& status, uint64 casterId)
{
	ZcStatusNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcStatusNotify);
	notify.entityId = entity.GetId();
	notify.statusId = status.id;
	notify.casterId = casterId;
	notify.durationMs = status.durationMs;
	notify.applied = 1;

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 상태 효과가 지속시간이 다해 풀린 것을 알린다. 사망으로 풀린 경우는 사망 통지가 대신하므로 보내지 않는다.
//***************************************************************************
void CBattleZone::OnStatusEnded(CZoneEntity& entity, uint32 statusId, StatusEndReason reason)
{
	if( reason != StatusEndReason::Expired )
		return;

	ZcStatusNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcStatusNotify);
	notify.entityId = entity.GetId();
	notify.statusId = statusId;
	notify.applied = 0;

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 스킬이 거는 상태 효과 데이터를 영웅 데이터에서 찾는다.
//***************************************************************************
const SStatusTemplate* CBattleZone::FindStatusTemplate(uint32 statusId) const
{
	return _heroes->FindStatus(statusId);
}

//***************************************************************************
// @brief 스킬 시전 거부를 요청한 본인에게 알린다.
//***************************************************************************
void CBattleZone::OnSkillRejected(CZoneEntity& caster, const SSkillTemplate& skill, SkillResult reason)
{
	ESkillRejectReason code = ESkillRejectReason::InvalidTarget;
	switch( reason )
	{
	case SkillResult::CasterDead:		code = ESkillRejectReason::CasterDead; break;
	case SkillResult::Cooldown:			code = ESkillRejectReason::Cooldown; break;
	case SkillResult::OutOfRange:		code = ESkillRejectReason::OutOfRange; break;
	case SkillResult::NoLineOfSight:	code = ESkillRejectReason::NoLineOfSight; break;
	case SkillResult::Casting:			code = ESkillRejectReason::Casting; break;
	default:							break;
	}

	SendSkillReject(caster, skill.id, code);
}

//***************************************************************************
// @brief 스킬 요청 거부 패킷을 요청한 플레이어에게만 보낸다.
//***************************************************************************
void CBattleZone::SendSkillReject(CZoneEntity& to, uint32 skillId, ESkillRejectReason reason)
{
	ZcSkillRejectNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcSkillRejectNotify);
	notify.skillId = skillId;
	notify.reason = static_cast<uint8>(reason);

	SendToEntity(to, &notify, notify.size);
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
	if( about.GetType() == EntityType::Player )
		notify.templateId = static_cast<const CZonePlayer&>(about).GetHeroId();
	else
		notify.templateId = _monsters->IsAiMonster(about) ? static_cast<const CMonster&>(about).GetTemplate().id : 0;

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
// @brief 엔티티가 존에서 제거되기 직전에 몬스터 AI 목록에서 뺀다.
// @param entity 제거될 엔티티
//***************************************************************************
void CBattleZone::OnEntityRemoved(CZoneEntity& entity)
{
	_monsters->OnRemoved(entity);
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
	case MoveResult::Blocked:			correct.reason = static_cast<uint8>(ECorrectPosReason::Blocked); break;
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

	_monsters->OnDamaged(attacker, victim, damage);
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

	_monsters->OnDied(victim);
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

	_monsters->OnRespawned(entity);
}

//***************************************************************************
// @brief 체력이 회복되면 주변 전원에게 알린다.
// @param entity 회복된 엔티티
// @param amount 실제 회복량
//***************************************************************************
void CBattleZone::OnEntityHealed(CZoneEntity& entity, int32 amount)
{
	ZcHealNotifyPacket notify{};
	InitZonePacket(notify, EZoneClientPacketType::ZcHealNotify);
	notify.entityId = entity.GetId();
	notify.amount = amount;
	notify.hp = entity.GetHp();
	notify.maxHp = entity.GetCombatStats().maxHp;

	BroadcastAround(entity, &notify, notify.size);
}

//***************************************************************************
// @brief 매 틱 이동 갱신이 끝난 뒤 몬스터 AI를 진행한다.
// @param dtMs 이전 틱 이후 경과 시간(ms)
//***************************************************************************
void CBattleZone::OnUpdate(uint32 dtMs)
{
	CFieldZone<>::OnUpdate(dtMs);
	_monsters->Update();
}

//***************************************************************************
// @brief AI 몬스터는 템플릿의 부활 시간을, 그 외는 기본 정책을 따른다.
//***************************************************************************
uint32 CBattleZone::GetRespawnDelayMs(const CZoneEntity& victim) const
{
	return _monsters->IsAiMonster(victim) ? _monsters->GetRespawnDelayMs(victim) : CFieldZone<>::GetRespawnDelayMs(victim);
}

//***************************************************************************
// @brief AI 몬스터는 스폰 반경 안의 무작위 지점에서, 그 외는 기본 정책대로 부활한다.
//***************************************************************************
Vec2 CBattleZone::GetRespawnPos(const CZoneEntity& entity) const
{
	Vec2 pos;
	return _monsters->GetRespawnPos(entity, pos) ? pos : CFieldZone<>::GetRespawnPos(entity);
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
	case AttackResult::NoLineOfSight:	notify.reason = static_cast<uint8>(EAttackRejectReason::NoLineOfSight); break;
	case AttackResult::Casting:			notify.reason = static_cast<uint8>(EAttackRejectReason::Casting); break;
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

	const CZonePlayer& player = static_cast<CZonePlayer&>(target);
	if( player.IsLeaving() )
		return;

	_server->SendToClient(player.GetClientSessionId(), data, size);
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
	// 시야에 플레이어가 없는 몬스터(복귀 중 이동/회복, 외딴 곳의 부활 등)는 알릴 대상이 없으니 AOI를 훑지 않는다.
	if( HasNoObserver(entity) )
		return;

	_recipients.clear();

	AddRecipient(entity);
	ForEachWatcherInAoi(entity, [this](CZoneEntity& other)
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

	bool otherVisited = (other == nullptr || other == &victim);		// 자기 자신이 상대면 이미 수신자다
	ForEachWatcherInAoi(victim, [this, other, &otherVisited](CZoneEntity& neighbor)
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
// @brief 이 엔티티의 변화를 볼 플레이어가 없는지 확인한다. AI 몬스터는 시야 안 플레이어 수(_nearPlayers)를 이미 세고 있다.
// @param entity 확인할 엔티티
// @return 보는 플레이어가 없다고 확실하면 true (플레이어와 그 외 엔티티는 항상 false)
//***************************************************************************
bool CBattleZone::HasNoObserver(const CZoneEntity& entity) const
{
	return _monsters->IsAiMonster(entity) && static_cast<const CMonster&>(entity).GetNearPlayers() == 0;
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
	if( _recipients.empty() )
		return;

	_server->SendToClients(_recipients.data(), _recipients.size(), data, size);
}