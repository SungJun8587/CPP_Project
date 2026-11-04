
//***************************************************************************
// BossSystem.cpp : implementation of the CBossSystem class.
//
//***************************************************************************

#include "pch.h"
#include "BossSystem.h"
#include "MonsterSystem.h"

#include <algorithm>
#include <cstdio>

//***************************************************************************
// @brief 보스 시스템을 생성합니다.
// @param zone        보스를 구동할 존 (이 객체보다 오래 살아야 함)
// @param monsters    몬스터 시스템 (소환수를 만든다. 이 객체보다 오래 살아야 함)
// @param data        보스 데이터 (nullptr이면 보스 없이 동작)
// @param skills      스킬/상태 효과 데이터
// @param monsterData 몬스터 데이터 (참조 검증용)
//***************************************************************************
CBossSystem::CBossSystem(CFieldZoneBase& zone, CMonsterSystem& monsters, std::shared_ptr<const CBossData> data,
	std::shared_ptr<const CHeroData> skills, std::shared_ptr<const CMonsterData> monsterData)
	: _zone(zone)
	, _monsters(monsters)
	, _data(data != nullptr ? std::move(data) : std::make_shared<const CBossData>())
	, _skills(skills != nullptr ? std::move(skills) : CHeroData::MakeDefault())
	, _monsterData(monsterData != nullptr ? std::move(monsterData) : std::make_shared<const CMonsterData>())
	, _rng(0x9E3779B97F4A7C15ull ^ zone.GetZoneId())
{
	_candidates.reserve(64);
}

//***************************************************************************
// @brief 보스 데이터가 유효하고 이 존의 AOI 시야 안에서 동작하는지 검사합니다. Start() 전에 호출합니다.
//        - 데이터 자체의 검사(CBossData::Validate): 값 범위와 몬스터/스킬/상태 효과 참조
//        - 보스 스킬의 시전 거리 + 효과 반경, 소환 반경이 AOI 보장 시야 이하 (시야 밖에서 일어나는 일은 플레이어에게 전달되지 않는다)
// @param error [out] 오류 설명
// @return 문제가 없으면 true
//***************************************************************************
bool CBossSystem::Validate(std::string& error) const
{
	if( !_data->Validate(*_monsterData, *_skills, error) )
		return false;

	const float viewRange = _zone.GetGuaranteedViewRange();
	char buf[200];

	for( const SBossTemplate& boss : _data->GetBosses() )
	{
		for( const SBossPhaseTemplate& phase : boss.phases )
		{
			for( const SBossActionTemplate& act : phase.actions )
			{
				if( act.type == EBossActionType::Cast )
				{
					const SSkillTemplate* skill = _skills->FindSkill(act.skillId);
					if( skill != nullptr && skill->range + skill->radius > viewRange )
					{
						std::snprintf(buf, sizeof(buf), "boss %u skill %u: range + radius exceeds the guaranteed AOI view range %.1f", boss.id, skill->id, viewRange);
						error = buf;
						return false;
					}
				}
				else if( act.type == EBossActionType::Summon && act.spreadRadius > viewRange )
				{
					std::snprintf(buf, sizeof(buf), "boss %u: SpreadRadius exceeds the guaranteed AOI view range %.1f", boss.id, viewRange);
					error = buf;
					return false;
				}
			}
		}
	}

	return true;
}

//***************************************************************************
// @brief 보스 개체의 전투 상태를 조회합니다.
// @param entityId 보스 엔티티 ID
// @return 상태, 보스가 아니면 nullptr
//***************************************************************************
const SBossRuntime* CBossSystem::Find(uint64 entityId) const
{
	auto it = _runtimes.find(entityId);
	return it != _runtimes.end() ? it->second.get() : nullptr;
}

//***************************************************************************
// @brief 몬스터가 존에 배치된 직후 호출합니다. 보스 데이터에 있는 몬스터면 전투 상태를 만들어 붙입니다.
// @param monster 배치된 몬스터
//***************************************************************************
void CBossSystem::OnSpawned(CMonster& monster)
{
	const SBossTemplate* tmpl = _data->FindByMonster(monster.GetTemplate().id);
	if( tmpl == nullptr )
		return;

	std::unique_ptr<SBossRuntime> rt = std::make_unique<SBossRuntime>();
	rt->tmpl = tmpl;
	monster._boss = rt.get();
	_runtimes[monster.GetId()] = std::move(rt);
}

//***************************************************************************
// @brief 보스가 추적 상태에서 사고할 때마다 호출합니다. 격노, 페이즈, 행동 순으로 처리합니다.
// @param monster 보스
// @param target  현재 추적 대상 (살아 있음)
// @param nowMs   현재 존 논리 시각
// @return 보스가 시전을 시작해 이번 사고에서 더 움직이거나 공격하면 안 되면 true
//***************************************************************************
bool CBossSystem::OnChase(CMonster& monster, CZoneEntity& target, uint64 nowMs)
{
	SBossRuntime* rt = monster._boss;
	if( rt == nullptr )
		return false;

	if( !rt->engaged )
	{
		rt->engaged = true;
		rt->engagedAtMs = nowMs;
		StartPhase(*rt, monster, 0, nowMs);
	}

	CheckEnrage(*rt, monster, nowMs);
	CheckPhase(*rt, monster, nowMs);
	RunAction(*rt, monster, target, nowMs);
	return monster.IsCasting();
}

//***************************************************************************
// @brief 보스가 복귀하거나 부활할 때 호출합니다. 페이즈/격노/재사용 대기를 처음으로 돌리고, 건 상태 효과를 풀고, 소환수를 치웁니다.
// @param monster 보스 (보스가 아니면 무시)
//***************************************************************************
void CBossSystem::OnReset(CMonster& monster)
{
	SBossRuntime* rt = monster._boss;
	if( rt == nullptr )
		return;

	DespawnAdds(*rt);
	ResetRuntime(*rt);

	if( monster.IsAlive() )
		_zone.ClearStatuses(monster, StatusEndReason::Expired);
}

//***************************************************************************
// @brief 보스가 죽었을 때 호출합니다. 소환수를 치우고 전투 상태를 초기화합니다. (부활하면 처음부터 다시 시작)
// @param monster 보스 (보스가 아니면 무시)
//***************************************************************************
void CBossSystem::OnDied(CMonster& monster)
{
	SBossRuntime* rt = monster._boss;
	if( rt == nullptr )
		return;

	DespawnAdds(*rt);
	ResetRuntime(*rt);
}

//***************************************************************************
// @brief 보스가 존에서 제거되기 직전에 호출합니다. 상태를 지우고 소환수를 치웁니다.
// @param monster 제거될 몬스터 (보스가 아니면 무시)
//***************************************************************************
void CBossSystem::OnRemoved(CMonster& monster)
{
	SBossRuntime* rt = monster._boss;
	if( rt == nullptr )
		return;

	DespawnAdds(*rt);
	monster._boss = nullptr;
	_runtimes.erase(monster.GetId());
}

//***************************************************************************
// @brief 페이즈에 들어갑니다. 진입 상태 효과와 회복을 적용하고 행동 순서를 처음으로 돌립니다.
//***************************************************************************
void CBossSystem::StartPhase(SBossRuntime& rt, CMonster& boss, size_t phaseIdx, uint64 nowMs)
{
	const SBossPhaseTemplate& phase = rt.tmpl->phases[phaseIdx];

	rt.phaseIdx = phaseIdx;
	rt.actionIdx = 0;
	rt.nextActionMs = nowMs + phase.transitionMs;
	rt.blockedSinceMs = 0;

	if( phase.enterStatusId != 0 )
	{
		const SStatusTemplate* status = _skills->FindStatus(phase.enterStatusId);
		if( status != nullptr )
			_zone.ApplyStatus(boss, *status, &boss);
	}

	if( phase.enterHealPercent != 0 )
	{
		const int64 amount = static_cast<int64>(boss.GetCombatStats().maxHp) * phase.enterHealPercent / 100;
		_zone.Heal(boss, static_cast<int32>(amount));
	}
}

//***************************************************************************
// @brief 전투 시작 후 정해진 시간이 지났으면 격노 상태 효과를 한 번 겁니다.
//***************************************************************************
void CBossSystem::CheckEnrage(SBossRuntime& rt, CMonster& boss, uint64 nowMs)
{
	if( rt.enraged || rt.tmpl->enrageAfterMs == 0 || nowMs - rt.engagedAtMs < rt.tmpl->enrageAfterMs )
		return;

	rt.enraged = true;
	const SStatusTemplate* status = _skills->FindStatus(rt.tmpl->enrageStatusId);
	if( status != nullptr )
		_zone.ApplyStatus(boss, *status, &boss);
}

//***************************************************************************
// @brief 체력이 다음 페이즈의 기준 % 이하면 한 단계 올립니다. 한 사고에 한 단계씩이라 건너뛴 페이즈의 진입 효과도 빠짐없이 적용된다.
//        보스가 시전 중일 때는 사고가 건너뛰어지므로 전환은 시전이 끝난 다음 사고에서 일어난다 (시전은 중간에 끊지 않는다).
//***************************************************************************
void CBossSystem::CheckPhase(SBossRuntime& rt, CMonster& boss, uint64 nowMs)
{
	if( rt.phaseIdx + 1 >= rt.tmpl->phases.size() )
		return;

	const SBossPhaseTemplate& next = rt.tmpl->phases[rt.phaseIdx + 1];
	const int64 maxHp = boss.GetCombatStats().maxHp;
	if( static_cast<int64>(boss.GetHp()) * 100 <= static_cast<int64>(next.hpBelowPercent) * maxHp )
		StartPhase(rt, boss, rt.phaseIdx + 1, nowMs);
}

//***************************************************************************
// @brief 때가 된 행동 하나를 실행합니다. 조건(대상, 사거리, 재사용)이 안 맞으면 다음 사고에서 다시 시도하고,
//        kActionTimeoutMs 넘게 막히면 그 행동을 건너뜁니다.
//***************************************************************************
void CBossSystem::RunAction(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, uint64 nowMs)
{
	if( nowMs < rt.nextActionMs )
		return;

	const SBossPhaseTemplate& phase = rt.tmpl->phases[rt.phaseIdx];
	if( rt.actionIdx >= phase.actions.size() )
	{
		if( phase.loopStart >= phase.actions.size() )
			return;		// 반복 행동이 없는 페이즈: 진입 행동을 마치면 기본 공격만 한다
		rt.actionIdx = phase.loopStart;
	}

	const SBossActionTemplate& act = phase.actions[rt.actionIdx];

	switch( act.type )
	{
	case EBossActionType::Cast:
	{
		uint32 waitMs = act.delayMs;
		switch( DoCast(rt, boss, current, act, nowMs, waitMs) )
		{
		case EOutcome::Done:
			Advance(rt, phase, nowMs, waitMs);
			break;
		case EOutcome::Retry:
			break;
		case EOutcome::Blocked:
			if( rt.blockedSinceMs == 0 )
				rt.blockedSinceMs = nowMs;
			else if( nowMs - rt.blockedSinceMs >= _actionTimeoutMs )
				Advance(rt, phase, nowMs, act.delayMs);		// 건너뛴다
			break;
		}
		break;
	}
	case EBossActionType::Summon:
		DoSummon(rt, boss, current, act);
		Advance(rt, phase, nowMs, act.delayMs);
		break;
	case EBossActionType::Aggro:
		DoAggro(boss, current, act);
		Advance(rt, phase, nowMs, act.delayMs);
		break;
	}
}

//***************************************************************************
// @brief 다음 행동으로 넘어갑니다. 반복 행동 끝에서는 첫 반복 행동으로 돌아갑니다.
// @param waitMs 다음 행동까지 기다릴 시간
//***************************************************************************
void CBossSystem::Advance(SBossRuntime& rt, const SBossPhaseTemplate& phase, uint64 nowMs, uint32 waitMs)
{
	++rt.actionIdx;
	if( rt.actionIdx >= phase.actions.size() && phase.loopStart < phase.actions.size() )
		rt.actionIdx = phase.loopStart;

	rt.nextActionMs = nowMs + waitMs;
	rt.blockedSinceMs = 0;
}

//***************************************************************************
// @brief 스킬 시전 행동을 실행합니다. 대상은 스킬 사거리 안에서 고르며, Self는 보스 위치를 중심으로 한다.
// @param waitMs [out] 성공했을 때 다음 행동까지의 대기 (시전 시간 + DelayMs)
// @return Done: 시전함 / Retry: 다음 사고에 다시 (같은 틱 중복, 이미 시전 중) / Blocked: 조건이 안 맞음 (대상 없음, 사거리 밖, 재사용 대기 등)
//***************************************************************************
CBossSystem::EOutcome CBossSystem::DoCast(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act, uint64 nowMs, uint32& waitMs)
{
	(void)nowMs;
	const SSkillTemplate* skill = _skills->FindSkill(act.skillId);
	if( skill == nullptr )
		return EOutcome::Done;		// 검증을 통과한 데이터에서는 일어나지 않는다. 걸리면 행동만 건너뛴다

	Vec2 center = boss.GetPos();
	uint64 targetId = 0;
	if( act.target != EBossTarget::Self )
	{
		CZoneEntity* target = PickEntity(boss, current, act.target, skill->range);
		if( target == nullptr )
			return EOutcome::Blocked;

		center = target->GetPos();
		if( skill->shape == ESkillShape::Single )
			targetId = target->GetId();
	}

	switch( _zone.CastSkill(boss, *skill, targetId, center, rt.readyAtMs[skill->id]) )
	{
	case SkillResult::Ok:
		waitMs = skill->castTimeMs + act.delayMs;
		return EOutcome::Done;
	case SkillResult::Throttled:
	case SkillResult::Casting:
	case SkillResult::CasterDead:
		return EOutcome::Retry;
	default:
		return EOutcome::Blocked;
	}
}

//***************************************************************************
// @brief 소환 행동을 실행합니다. 동시에 데리고 있을 수 있는 수(kMaxBossAdds)까지만 소환하며, 소환수는 보스의 현재 대상에게 어그로를 둡니다.
//***************************************************************************
void CBossSystem::DoSummon(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act)
{
	PruneAdds(rt);

	const size_t room = rt.adds.size() < kMaxBossAdds ? kMaxBossAdds - rt.adds.size() : 0;
	const size_t count = (std::min)(static_cast<size_t>(act.count), room);

	for( size_t i = 0; i < count; ++i )
	{
		CMonster* add = _monsters.SpawnSummon(act.monsterId, boss.GetPos(), act.spreadRadius, boss.GetId(), current.GetId());
		if( add != nullptr )
			rt.adds.push_back(add->GetId());
	}
}

//***************************************************************************
// @brief 어그로 교체 행동을 실행합니다. 고른 대상의 위협을 현재 최대 위협의 kAggroBoostPercent%로 올려 다음 사고에서 대상이 바뀌게 합니다.
//***************************************************************************
void CBossSystem::DoAggro(CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act)
{
	const SSkillTemplate* skill = act.skillId != 0 ? _skills->FindSkill(act.skillId) : nullptr;
	const float radius = skill != nullptr ? skill->range : boss.GetTemplate().aggroRange;

	CZoneEntity* target = PickEntity(boss, current, act.target, radius);
	if( target == nullptr )
		return;

	int64 maxThreat = 0;
	int64 currentThreat = 0;
	for( size_t i = 0; i < boss._aggroCount; ++i )
	{
		maxThreat = (std::max)(maxThreat, boss._aggro[i].threat);
		if( boss._aggro[i].entityId == target->GetId() )
			currentThreat = boss._aggro[i].threat;
	}

	if( maxThreat > INT64_MAX / 2 )
		return;

	const int64 wanted = maxThreat * kAggroBoostPercent / 100 + 1;
	if( wanted > currentThreat )
		boss.AddThreat(target->GetId(), wanted - currentThreat);
}

//***************************************************************************
// @brief 규칙에 맞는 대상을 고릅니다. 후보는 보스로부터 radius 안에서 공격 가능한 살아 있는 플레이어이며,
//        무작위는 ID 순으로 정렬한 뒤 고르므로 같은 시드에서 재현된다. 동점은 ID가 작은 쪽.
// @param current 보스의 현재 대상 (Current 규칙)
// @return 대상, 후보가 없으면 nullptr
//***************************************************************************
CZoneEntity* CBossSystem::PickEntity(CMonster& boss, CZoneEntity& current, EBossTarget rule, float radius)
{
	const Vec2 pos = boss.GetPos();

	if( rule == EBossTarget::Current )
		return (current.IsAlive() && DistanceSq(pos, current.GetPos()) <= radius * radius) ? &current : nullptr;

	_candidates.clear();
	_zone.ForEachLivePlayerInRadius(pos, radius, [&](CZoneEntity& e)
		{
			if( _zone.CanAttack(boss, e) )
				_candidates.push_back(&e);
		});

	if( _candidates.empty() )
		return nullptr;

	std::sort(_candidates.begin(), _candidates.end(), [](const CZoneEntity* a, const CZoneEntity* b) { return a->GetId() < b->GetId(); });

	if( rule == EBossTarget::Random )
		return _candidates[static_cast<size_t>(_rng() % _candidates.size())];

	CZoneEntity* best = _candidates.front();
	for( CZoneEntity* e : _candidates )
	{
		switch( rule )
		{
		case EBossTarget::Farthest:
			if( DistanceSq(pos, e->GetPos()) > DistanceSq(pos, best->GetPos()) ) best = e;
			break;
		case EBossTarget::Nearest:
			if( DistanceSq(pos, e->GetPos()) < DistanceSq(pos, best->GetPos()) ) best = e;
			break;
		case EBossTarget::LowestHp:
			if( e->GetHp() < best->GetHp() ) best = e;
			break;
		default:
			break;
		}
	}
	return best;
}

//***************************************************************************
// @brief 소환수를 모두 치웁니다. 제거는 다음 틱에 타이머로 한다 (몬스터 사고 목록을 순회하는 중일 수 있으므로).
//***************************************************************************
void CBossSystem::DespawnAdds(SBossRuntime& rt)
{
	for( const uint64 id : rt.adds )
	{
		if( !_zone.ScheduleAfter(_zone.GetTickIntervalMs(), [id](CZone& zone)
			{
				static_cast<CFieldZoneBase&>(zone).RemoveEntity(id);
			}) )
		{
			LOG_ERROR(_T("[CBossSystem] add removal timer failed. zoneId=%u, entityId=%llu"), _zone.GetZoneId(), static_cast<unsigned long long>(id));
		}
	}
	rt.adds.clear();
}

//***************************************************************************
// @brief 소환수 목록에서 이미 제거되었거나 죽은 것을 뺍니다.
//***************************************************************************
void CBossSystem::PruneAdds(SBossRuntime& rt)
{
	rt.adds.erase(std::remove_if(rt.adds.begin(), rt.adds.end(), [&](uint64 id)
		{
			CZoneEntity* e = _zone.FindEntity(id);
			return e == nullptr || !e->IsAlive();
		}), rt.adds.end());
}

//***************************************************************************
// @brief 전투 상태를 처음으로 돌립니다. (소환수 목록은 호출자가 먼저 처리)
//***************************************************************************
void CBossSystem::ResetRuntime(SBossRuntime& rt)
{
	rt.phaseIdx = 0;
	rt.engaged = false;
	rt.enraged = false;
	rt.engagedAtMs = 0;
	rt.actionIdx = 0;
	rt.nextActionMs = 0;
	rt.blockedSinceMs = 0;
	rt.readyAtMs.clear();
}