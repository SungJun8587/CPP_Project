
//***************************************************************************
// FieldZoneSkill.cpp : CFieldZoneBase의 스킬/상태 효과 구현부.
//   스킬 시전(즉시/시전 시간), 투사체, 상태 효과(Dot/Buff)를 다룬다. 전투 규칙 중 나머지(이동, 기본 공격, 피해, 사망)는 FieldZoneBase.cpp에 있다.
//
//***************************************************************************

#include "pch.h"
#include "FieldZoneBase.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <vector>

//***************************************************************************
// @brief 스킬을 시전합니다. 검증 순서는 RequestAttack과 같은 원칙(싸고 위조하기 쉬운 값부터)이다.
//        1) 같은 틱의 공격/스킬 요청 중복은 조용히 무시(Throttled, 기본 공격과 같은 게이트를 쓴다)
//        2) 시전자 생존 -> 3) 이미 시전 중이 아님 -> 4) 재사용 대기 -> 5) 사거리(Single은 대상 위치, Area는 지정한 중심)
//        -> 6) 시선(장애물) -> 7) Single은 대상이 생존/공격 가능/무적 아님. 대상이 없는 경우와 사거리 밖은 같은 사유(엔티티 존재 여부 비노출).
//        거부된 요청(Throttled 제외)은 HandleSkillRejected로 알리고 재사용 대기를 소모하지 않는다.
//        통과하면 재사용 대기가 시작되고(시전 시간이 있어도 시작 시점부터) 시전자 본인의 무적이 해제된다.
//        - 시전 시간이 0이면 즉시 효과를 낸다: 대상이 0이어도 시전은 성공하며, 시전 알림(HandleSkillCast)이 피해 알림보다 먼저 나간다.
//        - 시전 시간이 있으면 시전을 시작하고(HandleSkillCastStarted) 이동을 멈춘다. 끝나는 시점에 대상/사거리/시선을 다시 확인해
//          효과를 내거나 취소(HandleSkillCastCancelled)한다. 시전 중 이동/순간이동/사망하면 취소되고, 재사용 대기는 돌려주지 않는다.
//        존 스레드에서만 호출해야 하며, caster는 이 존에 속해야 한다.
// @param caster    시전자
// @param skill     스킬 (시전자가 쓸 수 있는지는 호출자가 확인)
// @param targetId  Single의 대상 ID (Area는 무시)
// @param center    Area의 중심 좌표 (Single은 무시)
// @param readyAtMs [in,out] 이 시전자의 이 스킬을 다시 쓸 수 있는 존 논리 시각. 성공하면 갱신된다
// @return 처리 결과. Ok는 즉시 발동했거나 시전을 시작했다는 뜻이다
//***************************************************************************
SkillResult CFieldZoneBase::CastSkill(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& center, uint64& readyAtMs)
{
	assert(CanTouchZoneState());
	assert(caster._inZone);
	assert(!_updatingMovement && "CFieldZoneBase::CastSkill called during movement update");

	const uint64 nowMs = GetNowMs();
	if( !caster._attackGate.TryPass(nowMs, 1) )
		return SkillResult::Throttled;

	auto reject = [&](SkillResult reason)
		{
			HandleSkillRejected(caster, skill, reason);
			return reason;
		};

	if( caster._dead )
		return reject(SkillResult::CasterDead);

	if( caster._cast != nullptr )
		return reject(SkillResult::Casting);

	if( nowMs + GetTickIntervalMs() < readyAtMs )
		return reject(SkillResult::Cooldown);

	const bool single = skill.shape == ESkillShape::Single;
	CZoneEntity* target = nullptr;
	Vec2 mid;
	float moveSpeed = caster._moving ? caster._speed : 0.0f;
	if( single )
	{
		target = FindEntity(targetId);
		if( target == nullptr )
			return reject(SkillResult::OutOfRange);
		mid = target->_pos;
		if( target->_moving )
			moveSpeed += target->_speed;
	}
	else
	{
		if( !std::isfinite(center.x) || !std::isfinite(center.y) || !IsInBounds(center) )
			return reject(SkillResult::OutOfRange);
		mid = center;
	}

	const float reach = skill.range + _fieldConfig.posToleranceBase + moveSpeed * static_cast<float>(_fieldConfig.maxLatencyMs) * 0.001f;
	if( !(DistanceSq(caster._pos, mid) <= reach * reach) )
		return reject(SkillResult::OutOfRange);

	if( _fieldConfig.blockMap != nullptr && !_fieldConfig.blockMap->IsSegmentClear(caster._pos, mid) )
		return reject(SkillResult::NoLineOfSight);

	if( single && (target->_dead || !CanAttack(caster, *target) || IsInvulnerable(*target)) )
		return reject(SkillResult::InvalidTarget);

	readyAtMs = (readyAtMs > nowMs ? readyAtMs : nowMs) + skill.cooldownMs;
	caster._invulnerableUntilMs = 0;

	const uint64 effectTarget = single ? targetId : 0;
	if( skill.castTimeMs > 0 && BeginCast(caster, skill, effectTarget, mid) )
		return SkillResult::Ok;

	ExecuteSkill(caster, skill, effectTarget, mid);
	return SkillResult::Ok;
}

//***************************************************************************
// @brief 시전을 시작합니다. 완료 타이머를 걸고 이동 중이면 멈춥니다.
// @return 시전을 시작했으면 true, 타이머를 걸 수 없으면 false (호출자가 즉시 발동으로 대신한다)
//***************************************************************************
bool CFieldZoneBase::BeginCast(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& mid)
{
	const uint64 casterId = caster.GetId();
	const uint64 timerId = ScheduleTimer(skill.castTimeMs, [casterId](CZone& zone)
		{
			static_cast<CFieldZoneBase&>(zone).FinishCast(casterId);
		});
	if( timerId == 0 )
	{
		LOG_ERROR(_T("[CFieldZoneBase] cast timer failed; casting immediately. zoneId=%u, entityId=%llu, skillId=%u"),
			GetZoneId(), static_cast<unsigned long long>(casterId), skill.id);
		return false;
	}

	if( caster._moving )
		StopMove(casterId);

	auto cast = std::make_unique<SCastState>();
	cast->skill = skill;
	cast->targetId = targetId;
	cast->center = mid;
	cast->endMs = GetNowMs() + skill.castTimeMs;
	cast->timerId = timerId;
	caster._cast = std::move(cast);

	HandleSkillCastStarted(caster, skill, targetId, mid);
	return true;
}

//***************************************************************************
// @brief 시전 완료 타이머의 처리입니다. 시전 중 달라진 것이 없는지 다시 확인한 뒤 효과를 냅니다.
//        Single은 대상이 여전히 공격 가능하고 사거리/시선 안이어야 하며, 대상의 현재 위치가 효과 중심이 된다.
//        Area는 시작할 때 정한 중심에서 터진다(시전자가 움직이면 이미 취소되었다).
// @param casterId 시전자 ID
//***************************************************************************
void CFieldZoneBase::FinishCast(uint64 casterId)
{
	CZoneEntity* caster = FindEntity(casterId);
	if( caster == nullptr || caster->_cast == nullptr )
		return;

	// 타이머는 이미 소진되었으므로 취소하지 않고 상태만 비운다.
	std::unique_ptr<SCastState> cast = std::move(caster->_cast);
	if( caster->_dead )
		return;

	const SSkillTemplate& skill = cast->skill;
	Vec2 mid = cast->center;

	if( skill.shape == ESkillShape::Single )
	{
		CZoneEntity* target = FindEntity(cast->targetId);
		if( target == nullptr || target->_dead || !CanAttack(*caster, *target) || IsInvulnerable(*target) )
		{
			HandleSkillCastCancelled(*caster, skill, SkillCancelReason::InvalidTarget);
			return;
		}

		mid = target->_pos;
		const float moveSpeed = target->_moving ? target->_speed : 0.0f;
		const float reach = skill.range + _fieldConfig.posToleranceBase + moveSpeed * static_cast<float>(_fieldConfig.maxLatencyMs) * 0.001f;
		if( !(DistanceSq(caster->_pos, mid) <= reach * reach) )
		{
			HandleSkillCastCancelled(*caster, skill, SkillCancelReason::OutOfRange);
			return;
		}

		if( _fieldConfig.blockMap != nullptr && !_fieldConfig.blockMap->IsSegmentClear(caster->_pos, mid) )
		{
			HandleSkillCastCancelled(*caster, skill, SkillCancelReason::NoLineOfSight);
			return;
		}
	}

	ExecuteSkill(*caster, skill, skill.shape == ESkillShape::Single ? cast->targetId : 0, mid);
}

//***************************************************************************
// @brief 진행 중인 시전을 취소합니다. 완료 타이머를 해제하고 취소 훅을 호출합니다.
//        재사용 대기는 돌려주지 않습니다. 존 스레드에서만 호출해야 합니다.
// @param caster 시전자
// @param reason 취소 사유
// @return 취소했으면 true, 시전 중이 아니면 false
//***************************************************************************
bool CFieldZoneBase::CancelCast(CZoneEntity& caster, SkillCancelReason reason)
{
	if( caster._cast == nullptr )
		return false;

	std::unique_ptr<SCastState> cast = std::move(caster._cast);
	CancelTimer(cast->timerId);

	HandleSkillCastCancelled(caster, cast->skill, reason);
	return true;
}

//***************************************************************************
// @brief 검증을 마친 스킬의 효과를 냅니다. 즉시 스킬은 바로 피해를 주고, 투사체 스킬은 발사만 한다.
//        시전 알림은 피해 알림보다 먼저 나간다.
// @param caster   시전자
// @param skill    스킬
// @param targetId Single의 대상 ID (Area는 0)
// @param mid      효과 중심 (Single은 대상 위치)
//***************************************************************************
void CFieldZoneBase::ExecuteSkill(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& mid)
{
	if( skill.projectileSpeed > 0.0f )
	{
		LaunchProjectile(caster, skill, targetId, mid);
		return;
	}

	CZoneEntity* target = skill.shape == ESkillShape::Single ? FindEntity(targetId) : nullptr;
	std::vector<SSkillHit> hits;
	CollectHits(caster, skill, target, mid, hits);

	HandleSkillCast(caster, skill, targetId, mid, static_cast<uint32>(hits.size()));

	ApplyHits(caster, skill, hits, caster.GetAttack());
	ApplySelfStatus(caster, skill);
}

//***************************************************************************
// @brief 맞는 대상을 모읍니다. Single은 대상이 여전히 공격 가능할 때만 하나, Area는 중심에서 radius 안의 공격 가능한 엔티티를
//        장애물에 가린 것을 빼고 모아 중심에 가까운 순(같으면 ID 순)으로 maxTargets까지 남긴다.
//***************************************************************************
void CFieldZoneBase::CollectHits(CZoneEntity& caster, const SSkillTemplate& skill, CZoneEntity* target, const Vec2& mid, std::vector<SSkillHit>& hits) const
{
	hits.clear();
	if( skill.shape == ESkillShape::Single )
	{
		if( target != nullptr && !target->_dead && CanAttack(caster, *target) && !IsInvulnerable(*target) )
			hits.push_back({ 0.0f, target->GetId() });
		return;
	}

	ForEachInRadius(mid, skill.radius, [&](CZoneEntity& e)
		{
			if( &e == &caster || e._dead || !CanAttack(caster, e) || IsInvulnerable(e) )
				return;
			if( _fieldConfig.blockMap != nullptr && !_fieldConfig.blockMap->IsSegmentClear(mid, e._pos) )
				return;		// 폭발이 벽에 가려진 대상에게는 닿지 않는다
			hits.push_back({ DistanceSq(mid, e._pos), e.GetId() });
		});

	std::sort(hits.begin(), hits.end(), [](const SSkillHit& a, const SSkillHit& b)
		{
			return a.distSq != b.distSq ? a.distSq < b.distSq : a.id < b.id;
		});
	if( hits.size() > skill.maxTargets )
		hits.resize(skill.maxTargets);
}

//***************************************************************************
// @brief 모은 대상에게 피해와 상태 효과를 적용합니다. 앞선 피해의 훅에서 사망/퇴장한 대상은 건너뜁니다.
// @param attack 피해 계산에 쓰는 시전자 공격력 (즉시 스킬은 현재 값, 투사체는 발사 시점 값)
//***************************************************************************
void CFieldZoneBase::ApplyHits(CZoneEntity& caster, const SSkillTemplate& skill, const std::vector<SSkillHit>& hits, int32 attack)
{
	const SStatusTemplate* status = skill.statusId != 0 ? FindStatusTemplate(skill.statusId) : nullptr;
	const bool statusOnHit = status != nullptr && status->target == EStatusTarget::Hit;
	const bool damaging = SkillHasDamage(skill);

	for( const SSkillHit& hit : hits )
	{
		CZoneEntity* victim = FindEntity(hit.id);
		if( victim == nullptr || victim->_dead )
			continue;

		if( damaging )
			DealDamage(*victim, &caster, CalcSkillDamage(skill, attack, victim->GetDefense(), _fieldConfig.minDamage));

		if( statusOnHit && !victim->_dead )
			ApplyStatus(*victim, *status, &caster);
	}
}

//***************************************************************************
// @brief 스킬이 시전자 본인에게 거는 상태 효과(target == Self)를 적용합니다.
//***************************************************************************
void CFieldZoneBase::ApplySelfStatus(CZoneEntity& caster, const SSkillTemplate& skill)
{
	if( skill.statusId == 0 || caster._dead )
		return;

	const SStatusTemplate* status = FindStatusTemplate(skill.statusId);
	if( status != nullptr && status->target == EStatusTarget::Self )
		ApplyStatus(caster, *status, &caster);
}

//***************************************************************************
// @brief 투사체를 발사합니다. 별도 엔티티 없이 착탄 시각에 타이머로 판정한다.
//        비행 시간은 거리/속도(틱 간격 이상, kMaxProjectileFlightMs 이하). 시전 알림(적중 수 0)과 발사 알림이 나가고,
//        시전자 본인에게 거는 상태 효과는 발사 시점에 적용한다.
//***************************************************************************
void CFieldZoneBase::LaunchProjectile(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& mid)
{
	HandleSkillCast(caster, skill, targetId, mid, 0);
	ApplySelfStatus(caster, skill);

	const Vec2 from = caster._pos;
	const float dist = std::sqrt(DistanceSq(from, mid));
	double flight = std::ceil(static_cast<double>(dist) / static_cast<double>(skill.projectileSpeed) * 1000.0);
	flight = (std::max)(flight, static_cast<double>(GetTickIntervalMs()));
	flight = (std::min)(flight, static_cast<double>(kMaxProjectileFlightMs));
	const uint32 flightMs = static_cast<uint32>(flight);

	if( ++_nextProjectileId == 0 )
		++_nextProjectileId;
	const uint32 projectileId = _nextProjectileId;

	HandleProjectileLaunched(caster, skill, projectileId, targetId, from, mid, flightMs);

	const uint64 casterId = caster.GetId();
	const int32 attack = caster.GetAttack();
	const uint64 timerId = ScheduleTimer(flightMs, [casterId, skill, projectileId, targetId, from, mid, attack](CZone& zone)
		{
			static_cast<CFieldZoneBase&>(zone).LandProjectile(casterId, skill, projectileId, targetId, from, mid, attack);
		});

	// 타이머를 걸 수 없으면 허공에서 사라지지 않도록 바로 착탄시킨다.
	if( timerId == 0 )
	{
		LOG_ERROR(_T("[CFieldZoneBase] projectile timer failed; landing immediately. zoneId=%u, entityId=%llu, skillId=%u"),
			GetZoneId(), static_cast<unsigned long long>(casterId), skill.id);
		LandProjectile(casterId, skill, projectileId, targetId, from, mid, attack);
	}
}

//***************************************************************************
// @brief 투사체 착탄 처리입니다. 시전자가 존을 떠났으면 사라진다(알림 없음).
//        Single은 대상을 쫓아가 착탄 시점의 위치에서 맞으며, 대상이 사라졌거나 무적/공격 불가이거나 발사 지점과의 사이에
//        장애물이 생겼으면 빗나간다(적중 0으로 착탄 알림). Area는 발사 때 정한 지점에서 터진다.
// @param attack 발사 시점의 시전자 공격력
//***************************************************************************
void CFieldZoneBase::LandProjectile(uint64 casterId, const SSkillTemplate& skill, uint32 projectileId, uint64 targetId, const Vec2& from, const Vec2& mid, int32 attack)
{
	CZoneEntity* caster = FindEntity(casterId);
	if( caster == nullptr )
		return;

	CZoneEntity* target = nullptr;
	Vec2 center = mid;
	if( skill.shape == ESkillShape::Single )
	{
		target = FindEntity(targetId);
		if( target != nullptr )
		{
			center = target->_pos;
			if( _fieldConfig.blockMap != nullptr && !_fieldConfig.blockMap->IsSegmentClear(from, center) )
				target = nullptr;
		}
	}

	std::vector<SSkillHit> hits;
	CollectHits(*caster, skill, target, center, hits);

	HandleProjectileImpact(*caster, skill, projectileId, center, static_cast<uint32>(hits.size()));

	ApplyHits(*caster, skill, hits, attack);
}

//***************************************************************************
// @brief 상태 효과를 겁니다. 같은 ID가 이미 걸려 있으면 지속시간을 새로 시작하고 값을 갱신합니다(중첩 없음).
//        Dot의 틱 피해는 건 순간의 시전자 공격력으로 정해집니다. 엔티티당 kMaxStatusesPerEntity개까지이며 가득 차면 새 효과는 걸리지 않습니다.
//        죽은 엔티티에는 걸리지 않습니다. 존 스레드에서만 호출해야 합니다.
// @param target 효과를 받을 엔티티 (이 존에 등록된 엔티티)
// @param status 상태 효과 데이터
// @param caster 건 엔티티 (없으면 nullptr. Dot 피해는 공격력 0 기준으로 계산되고 피해의 공격자도 없다)
// @return 걸었거나 갱신했으면 true
//***************************************************************************
bool CFieldZoneBase::ApplyStatus(CZoneEntity& target, const SStatusTemplate& status, CZoneEntity* caster)
{
	assert(CanTouchZoneState());
	assert(target._inZone);

	if( target._dead || status.id == 0 )
		return false;

	SActiveStatus* slot = nullptr;
	for( SActiveStatus& s : target._statuses )
		if( s.id == status.id )
		{
			slot = &s;
			break;
		}

	if( slot == nullptr )
	{
		if( target._statuses.size() >= kMaxStatusesPerEntity )
			return false;

		if( target._statuses.empty() )
		{
			target._statusIdx = _statusHolders.size();
			_statusHolders.push_back(&target);
		}
		target._statuses.emplace_back();
		slot = &target._statuses.back();
	}

	const uint64 nowMs = GetNowMs();
	slot->id = status.id;
	slot->kind = status.kind;
	slot->casterId = caster != nullptr ? caster->GetId() : 0;
	slot->endMs = nowMs + status.durationMs;
	slot->tickMs = status.tickMs;
	slot->nextTickMs = nowMs + status.tickMs;
	slot->dotDamage = status.kind == EStatusKind::Dot ? CalcDotDamage(status, caster != nullptr ? caster->GetAttack() : 0) : 0;
	slot->attackPercent = status.attackPercent;
	slot->defensePercent = status.defensePercent;
	slot->speedPercent = status.speedPercent;

	RecomputeStatMods(target);

	HandleStatusApplied(target, status, slot->casterId);
	return true;
}

//***************************************************************************
// @brief 엔티티의 상태 효과를 모두 해제합니다. 해제마다 훅을 호출하며, 능력치 보정도 원래대로 돌아옵니다.
// @param entity 대상 엔티티
// @param reason 해제 사유
//***************************************************************************
void CFieldZoneBase::ClearStatuses(CZoneEntity& entity, StatusEndReason reason)
{
	if( entity._statuses.empty() )
		return;

	std::array<uint32, kMaxStatusesPerEntity> ids;
	size_t count = 0;
	for( const SActiveStatus& s : entity._statuses )
		ids[count++] = s.id;

	entity._statuses.clear();
	RemoveStatusHolder(entity);
	RecomputeStatMods(entity);

	for( size_t i = 0; i < count; ++i )
		HandleStatusEnded(entity, ids[i], reason);
}

//***************************************************************************
// @brief 엔티티를 상태 효과 보유 목록에서 O(1)로 뺍니다. (마지막 원소와 교체 후 pop)
//***************************************************************************
void CFieldZoneBase::RemoveStatusHolder(CZoneEntity& entity)
{
	const size_t idx = entity._statusIdx;
	if( idx == CZoneEntity::kInvalidIndex )
		return;

	CZoneEntity* last = _statusHolders.back();
	_statusHolders[idx] = last;
	last->_statusIdx = idx;
	_statusHolders.pop_back();

	entity._statusIdx = CZoneEntity::kInvalidIndex;
}

//***************************************************************************
// @brief 걸려 있는 Buff의 증감(%)을 합산해 공격력/방어력/이동속도 보정을 다시 계산합니다. 합계는 kMin/kMaxStatPercent로 묶는다.
//***************************************************************************
void CFieldZoneBase::RecomputeStatMods(CZoneEntity& entity)
{
	int32 attack = 0, defense = 0, speed = 0;
	for( const SActiveStatus& s : entity._statuses )
	{
		if( s.kind != EStatusKind::Buff )
			continue;
		attack += s.attackPercent;
		defense += s.defensePercent;
		speed += s.speedPercent;
	}

	entity._attackPct = (std::min)((std::max)(attack, kMinStatPercent), kMaxStatPercent);
	entity._defensePct = (std::min)((std::max)(defense, kMinStatPercent), kMaxStatPercent);
	entity._speedPct = (std::min)((std::max)(speed, kMinStatPercent), kMaxStatPercent);
	entity._speed = entity._baseSpeed * static_cast<float>(100 + entity._speedPct) * 0.01f;
}

//***************************************************************************
// @brief 존에서 제거되는 엔티티의 시전/상태 효과를 훅 없이 정리합니다. 시전 타이머를 해제하고 보유 목록에서 뺀다.
//***************************************************************************
void CFieldZoneBase::DetachSkillState(CZoneEntity& entity)
{
	if( entity._cast != nullptr )
	{
		CancelTimer(entity._cast->timerId);
		entity._cast.reset();
	}

	RemoveStatusHolder(entity);
	entity._statuses.clear();
}

//***************************************************************************
// @brief 매 틱 상태 효과를 갱신합니다. 보유 엔티티만 순회한다.
//        Dot은 틱 시각이 된 만큼 피해를 주고(틱 간격이 지나치게 밀려도 한 번에 상태당 4틱까지만 처리하고 나머지는 다음 틱에 이어서 한다),
//        지속시간이 끝나면 마지막 틱까지 마친 뒤 해제한다. 만료 처리를 피해보다 먼저 해서, 피해 훅이 어떤 일을 하든 목록이 일관되게 유지된다.
//***************************************************************************
void CFieldZoneBase::UpdateStatuses()
{
	if( _statusHolders.empty() )
		return;

	constexpr uint32 kMaxTicksPerStatus = 4;
	struct STick { int32 damage; uint64 casterId; };

	const uint64 nowMs = GetNowMs();

	_statusScratch.clear();
	for( CZoneEntity* e : _statusHolders )
		_statusScratch.push_back(e->GetId());

	for( const uint64 id : _statusScratch )
	{
		CZoneEntity* entity = FindEntity(id);
		if( entity == nullptr || entity->_statuses.empty() )
			continue;

		std::array<STick, kMaxStatusesPerEntity* kMaxTicksPerStatus> ticks;
		size_t tickCount = 0;
		for( SActiveStatus& s : entity->_statuses )
		{
			if( s.kind != EStatusKind::Dot || s.tickMs == 0 )
				continue;
			for( uint32 n = 0; n < kMaxTicksPerStatus && s.nextTickMs <= nowMs && s.nextTickMs <= s.endMs; ++n )
			{
				ticks[tickCount++] = { s.dotDamage, s.casterId };
				s.nextTickMs += s.tickMs;
			}
		}

		std::array<uint32, kMaxStatusesPerEntity> ended;
		size_t endedCount = 0;
		for( size_t i = 0; i < entity->_statuses.size(); )
		{
			const SActiveStatus& s = entity->_statuses[i];
			const bool ticksLeft = s.kind == EStatusKind::Dot && s.tickMs != 0 && s.nextTickMs <= s.endMs;
			if( s.endMs <= nowMs && !ticksLeft )
			{
				ended[endedCount++] = s.id;
				entity->_statuses.erase(entity->_statuses.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
				++i;
		}

		if( endedCount != 0 )
		{
			if( entity->_statuses.empty() )
				RemoveStatusHolder(*entity);
			RecomputeStatMods(*entity);
		}

		for( size_t i = 0; i < endedCount; ++i )
			HandleStatusEnded(*entity, ended[i], StatusEndReason::Expired);

		for( size_t i = 0; i < tickCount && !entity->_dead; ++i )
			DealDamage(*entity, FindEntity(ticks[i].casterId), ticks[i].damage);
	}
}

// ── 훅의 기본 구현 (아무것도 하지 않는다) ────────────────

void CFieldZoneBase::HandleSkillCastStarted(CZoneEntity& caster, const SSkillTemplate& skill, uint64 targetId, const Vec2& center)
{
	(void)caster; (void)skill; (void)targetId; (void)center;
}

void CFieldZoneBase::HandleSkillCastCancelled(CZoneEntity& caster, const SSkillTemplate& skill, SkillCancelReason reason)
{
	(void)caster; (void)skill; (void)reason;
}

void CFieldZoneBase::HandleProjectileLaunched(CZoneEntity& caster, const SSkillTemplate& skill, uint32 projectileId, uint64 targetId, const Vec2& from, const Vec2& to, uint32 flightMs)
{
	(void)caster; (void)skill; (void)projectileId; (void)targetId; (void)from; (void)to; (void)flightMs;
}

void CFieldZoneBase::HandleProjectileImpact(CZoneEntity& caster, const SSkillTemplate& skill, uint32 projectileId, const Vec2& center, uint32 hitCount)
{
	(void)caster; (void)skill; (void)projectileId; (void)center; (void)hitCount;
}

void CFieldZoneBase::HandleStatusApplied(CZoneEntity& entity, const SStatusTemplate& status, uint64 casterId)
{
	(void)entity; (void)status; (void)casterId;
}

void CFieldZoneBase::HandleStatusEnded(CZoneEntity& entity, uint32 statusId, StatusEndReason reason)
{
	(void)entity; (void)statusId; (void)reason;
}

const SStatusTemplate* CFieldZoneBase::FindStatusTemplate(uint32 statusId) const
{
	(void)statusId;
	return nullptr;
}