
//***************************************************************************
// MonsterSystem.cpp : implementation of the CMonsterSystem class.
//
//***************************************************************************

#include "pch.h"
#include "MonsterSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	constexpr float kArriveDistanceSq = 0.25f;		// 복귀 도착으로 보는 거리(0.5 units)의 제곱
	constexpr float kChaseStandRatio = 0.8f;		// 추적 목적지를 대상 앞 사거리의 이 비율 지점으로 잡는다 (대상 위로 겹쳐 걷지 않게)
}

//***************************************************************************
// @brief 몬스터 시스템을 생성합니다.
// @param zone   몬스터를 배치하고 구동할 존 (이 객체보다 오래 살아야 함)
// @param data   몬스터 데이터 (nullptr이면 몬스터 없이 동작)
// @param config AI 튜닝값
//***************************************************************************
CMonsterSystem::CMonsterSystem(CFieldZoneBase& zone, std::shared_ptr<const CMonsterData> data, const SMonsterSystemConfig& config)
	: _zone(zone)
	, _data(std::move(data))
	, _config(config)
	, _rng(zone.GetZoneId())
	, _serial(0)
	, _monsterCount(0)
{
	if( _config.thinkIntervalMs < 1 )
		_config.thinkIntervalMs = 1;

	_active.reserve(256);
}

//***************************************************************************
// @brief 이 존의 스폰 테이블이 존 설정과 맞는지 검사합니다. (데이터 자체의 검사는 CMonsterData::Validate)
//        - 스폰 중심이 존 영역 안
//        - 선공 감지 거리 <= AOI 최소 보장 시야 (시야 밖 플레이어는 AI를 깨우지 못하므로 감지할 수 없다)
//        - 기본 공격 판정 거리(사거리 + 허용 오차 + 이동 지연 보정) <= AOI 최소 보장 시야.
//          이동 지연 보정은 공격자와 대상이 모두 최고 속도로 움직일 때를 가정하므로,
//          속도는 몬스터 자신과 상대(플레이어)의 최고 속도 중 큰 값을 쓴다
//        - AI 튜닝값 범위
// @param error            [out] 오류 설명
// @param opponentMaxSpeed 몬스터와 맞붙는 상대(플레이어)가 가질 수 있는 최고 이동 속도 (units/sec). 모르면 0
// @return 문제가 없으면 true
//***************************************************************************
bool CMonsterSystem::Validate(std::string& error, float opponentMaxSpeed) const
{
	char buf[256];

	if( !(_config.engageRatio > 0.0f && _config.engageRatio <= 1.0f) || !(_config.repathDistance > 0.0f) )
	{
		error = "monster system config: engageRatio must be in (0, 1] and repathDistance > 0";
		return false;
	}

	if( _data == nullptr )
		return true;

	const FieldZoneConfig& fc = _zone.GetFieldConfig();
	const float viewRange = _zone.GetGuaranteedViewRange();

	for( const SSpawnEntry& s : _data->GetSpawns() )
	{
		if( s.zoneId != _zone.GetZoneId() )
			continue;

		const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
		if( tmpl == nullptr )
		{
			std::snprintf(buf, sizeof(buf), "zone %u: unknown monster id %u", s.zoneId, s.monsterId);
			error = buf;
			return false;
		}

		if( !(s.x >= fc.minX && s.x <= fc.maxX && s.y >= fc.minY && s.y <= fc.maxY) )
		{
			std::snprintf(buf, sizeof(buf), "zone %u: spawn of monster %u is outside the zone bounds", s.zoneId, s.monsterId);
			error = buf;
			return false;
		}

		if( tmpl->aggroRange > viewRange )
		{
			std::snprintf(buf, sizeof(buf), "monster %u: aggroRange %.1f exceeds the guaranteed AOI view range %.1f", tmpl->id, tmpl->aggroRange, viewRange);
			error = buf;
			return false;
		}

		if( !_zone.IsAttackRangeSupported(tmpl->stats.attackRange, (std::max)(tmpl->speed, opponentMaxSpeed)) )
		{
			std::snprintf(buf, sizeof(buf), "monster %u: attack reach exceeds the guaranteed AOI view range %.1f", tmpl->id, viewRange);
			error = buf;
			return false;
		}
	}

	return true;
}

//***************************************************************************
// @brief 이 존의 스폰 테이블에 나오는 몬스터 중 가장 빠른 이동 속도를 반환합니다.
//        플레이어의 공격 판정 거리 검증(IsAttackRangeSupported)에 상대 속도로 씁니다.
// @return 최고 속도 (units/sec), 스폰이 없으면 0
//***************************************************************************
float CMonsterSystem::GetMaxSpawnSpeed() const
{
	float maxSpeed = 0.0f;
	if( _data == nullptr )
		return maxSpeed;

	for( const SSpawnEntry& s : _data->GetSpawns() )
	{
		if( s.zoneId != _zone.GetZoneId() )
			continue;

		const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
		if( tmpl != nullptr && tmpl->speed > maxSpeed )
			maxSpeed = tmpl->speed;
	}

	return maxSpeed;
}

//***************************************************************************
// @brief 이 존의 스폰 테이블대로 몬스터를 모두 배치합니다. 존 구동 전(또는 존 스레드)에서 한 번 호출합니다.
//        최초 위치는 스폰 중심 반경 안의 무작위 지점입니다.
// @return 배치된 몬스터 수
//***************************************************************************
uint32 CMonsterSystem::SpawnAll()
{
	if( _data == nullptr )
		return 0;

	uint32 spawned = 0;
	for( const SSpawnEntry& s : _data->GetSpawns() )
	{
		if( s.zoneId != _zone.GetZoneId() )
			continue;

		const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
		if( tmpl == nullptr )
			continue;

		Vec2 anchor;
		anchor.x = s.x;
		anchor.y = s.y;

		for( uint32 i = 0; i < s.count; ++i )
		{
			const Vec2 pos = ClampToZone(RandomPointInDisc(anchor, s.radius));

			std::unique_ptr<CMonster> monster = std::make_unique<CMonster>(kMonsterIdBase | ++_serial, *tmpl, this, anchor, s.radius);
			monster->SetSpawnPos(pos);
			monster->_home = pos;

			if( _zone.AddEntity(std::move(monster)) != nullptr )
			{
				++spawned;
				++_monsterCount;
			}
		}
	}

	return spawned;
}

//***************************************************************************
// @brief 매 틱 호출합니다. 활성 몬스터 중 사고 시각이 된 것만 사고합니다.
//        이동 갱신이 끝난 뒤에 호출해야 한다(사고가 공격/이동 명령을 내리므로 이동 갱신 중에는 금지).
//***************************************************************************
void CMonsterSystem::Update()
{
	const uint64 nowMs = _zone.GetNowMs();

	for( size_t i = 0; i < _active.size(); )
	{
		CMonster* monster = _active[i];
		if( nowMs < monster->_nextThinkMs )
		{
			++i;
			continue;
		}

		monster->_nextThinkMs = nowMs + _config.thinkIntervalMs;
		Think(*monster, nowMs);

		// 사고 중 스스로 비활성화되면 swap-remove로 i번 자리에 다른 몬스터가 들어오므로 i를 올리지 않는다.
		if( i < _active.size() && _active[i] == monster )
			++i;
	}
}

//***************************************************************************
// @brief 몬스터를 활성 목록에 올립니다. 이미 올라 있거나 죽었으면 무시합니다.
//        첫 사고 시각은 몬스터 ID로 분산해, 한 틱에 사고가 몰리지 않게 한다.
// @param monster 활성화할 몬스터
//***************************************************************************
void CMonsterSystem::Activate(CMonster& monster)
{
	if( monster._active || !monster.IsAlive() )
		return;

	monster._active = true;
	monster._activeIdx = _active.size();
	monster._nextThinkMs = _zone.GetNowMs() + (monster.GetId() % _config.thinkIntervalMs);
	_active.push_back(&monster);
}

//***************************************************************************
// @brief 몬스터를 활성 목록에서 O(1)로 뺍니다.
// @param monster 비활성화할 몬스터
//***************************************************************************
void CMonsterSystem::Deactivate(CMonster& monster)
{
	if( !monster._active )
		return;

	const size_t idx = monster._activeIdx;
	CMonster* last = _active.back();
	_active[idx] = last;
	last->_activeIdx = idx;
	_active.pop_back();

	monster._active = false;
	monster._activeIdx = 0;
}

//***************************************************************************
// @brief 몬스터 한 마리의 AI를 한 번 진행합니다.
// @param monster 사고할 몬스터
// @param nowMs   현재 존 논리 시각
//***************************************************************************
void CMonsterSystem::Think(CMonster& monster, uint64 nowMs)
{
	if( !monster.IsAlive() )
	{
		Deactivate(monster);
		return;
	}

	switch( monster._state )
	{
	case EMonsterState::Idle:	ThinkIdle(monster, nowMs); break;
	case EMonsterState::Chase:	ThinkChase(monster, nowMs); break;
	case EMonsterState::Return:	ThinkReturn(monster); break;
	}
}

//***************************************************************************
// @brief 대기 상태 사고. 어그로 표에 대상이 있으면 추적을 시작하고, 없으면 선공 대상을 찾는다.
//        시야에 플레이어가 없으면 비활성화한다.
//***************************************************************************
void CMonsterSystem::ThinkIdle(CMonster& monster, uint64 nowMs)
{
	CZoneEntity* target = PickTarget(monster);

	if( target == nullptr && monster._tmpl->aggroRange > 0.0f )
	{
		target = FindAggroCandidate(monster);
		if( target != nullptr )
			monster.AddThreat(target->GetId(), 1);
	}

	if( target != nullptr )
	{
		monster._state = EMonsterState::Chase;
		monster._targetId = target->GetId();
		monster._lastTargetPos.x = std::numeric_limits<float>::infinity();	// 첫 추적은 반드시 이동 명령을 내리게 한다
		ThinkChase(monster, nowMs);
		return;
	}

	if( monster._nearPlayers == 0 )
		Deactivate(monster);
}

//***************************************************************************
// @brief 추적 상태 사고. 추적 한계를 넘었거나 대상이 없으면 복귀하고, 교전 거리면 정지해 공격하며,
//        아니면 대상 쪽으로 이동 명령을 낸다. 대상이 크게 움직였을 때만 이동 명령을 다시 낸다.
//***************************************************************************
void CMonsterSystem::ThinkChase(CMonster& monster, uint64 nowMs)
{
	const SMonsterTemplate& tmpl = *monster._tmpl;

	if( DistanceSq(monster.GetPos(), monster._home) > tmpl.leashRange * tmpl.leashRange )
	{
		EnterReturn(monster);
		return;
	}

	CZoneEntity* target = PickTarget(monster);
	if( target == nullptr )
	{
		EnterReturn(monster);
		return;
	}

	const float distSq = DistanceSq(monster.GetPos(), target->GetPos());
	const float engage = tmpl.stats.attackRange * _config.engageRatio;

	if( distSq <= engage * engage )
	{
		if( monster.IsMoving() )
			_zone.StopMove(monster.GetId());

		// 쿨다운 중에는 요청 자체를 건너뛴다.
		if( nowMs >= monster.GetNextAttackMs() )
		{
			switch( _zone.RequestAttack(monster, target->GetId()) )
			{
			case AttackResult::TargetDead:
			case AttackResult::TargetNotFound:
			case AttackResult::InvalidTarget:
			case AttackResult::TargetInvulnerable:
				monster.RemoveThreat(target->GetId());		// 공격할 수 없는 대상은 어그로에서 뺀다
				break;
			default:
				break;
			}
		}
		return;
	}

	const float repath = _config.repathDistance;
	if( monster.IsMoving() && DistanceSq(target->GetPos(), monster._lastTargetPos) <= repath * repath )
		return;

	// 대상 바로 위가 아니라 사거리 안쪽 지점을 목적지로 잡는다.
	Vec2 dest = target->GetPos();
	const float dist = std::sqrt(distSq);
	if( dist > 1e-3f )
	{
		const float stand = tmpl.stats.attackRange * kChaseStandRatio;
		dest.x += (monster.GetPos().x - dest.x) / dist * stand;
		dest.y += (monster.GetPos().y - dest.y) / dist * stand;
	}

	if( _zone.MoveEntity(monster, ClampToZone(dest)) )
		monster._lastTargetPos = target->GetPos();
}

//***************************************************************************
// @brief 복귀 상태 사고. 스폰 지점에 도착하면 체력을 가득 채우고 대기 상태로 돌아간다.
//***************************************************************************
void CMonsterSystem::ThinkReturn(CMonster& monster)
{
	if( monster.IsMoving() )
		return;

	if( DistanceSq(monster.GetPos(), monster._home) < kArriveDistanceSq )
	{
		_zone.Heal(monster, monster.GetCombatStats().maxHp);
		monster._state = EMonsterState::Idle;

		if( monster._nearPlayers == 0 )
			Deactivate(monster);
		return;
	}

	// 이동 명령이 거부되면(복귀 지점이 영역 밖 등 설정 오류) 순간이동으로 복귀를 마무리한다.
	if( !_zone.MoveEntity(monster, monster._home) )
		_zone.Teleport(monster.GetId(), monster._home);
}

//***************************************************************************
// @brief 복귀 상태로 전환합니다. 어그로를 비우고 스폰 지점으로 이동을 시작한다.
//***************************************************************************
void CMonsterSystem::EnterReturn(CMonster& monster)
{
	monster._state = EMonsterState::Return;
	monster.ClearAggro();

	if( !_zone.MoveEntity(monster, monster._home) )
		_zone.Teleport(monster.GetId(), monster._home);
}

//***************************************************************************
// @brief 어그로 표에서 추적할 대상을 고릅니다. 죽었거나 존에 없는 항목은 표에서 지웁니다.
//        위협이 가장 큰 대상을 고르되, 현재 대상보다 10%를 넘게 높을 때만 대상을 바꿔 잦은 갈아타기를 막는다.
// @return 대상 엔티티, 없으면 nullptr (이때 monster._targetId는 0)
//***************************************************************************
CZoneEntity* CMonsterSystem::PickTarget(CMonster& monster)
{
	CZoneEntity* best = nullptr;
	int64 bestThreat = -1;
	CZoneEntity* current = nullptr;
	int64 currentThreat = 0;

	for( size_t i = 0; i < monster._aggroCount; )
	{
		const SAggroEntry& entry = monster._aggro[i];
		CZoneEntity* entity = _zone.FindEntity(entry.entityId);

		if( entity == nullptr || !entity->IsAlive() )
		{
			if( monster._targetId == entry.entityId )
				monster._targetId = 0;

			monster._aggro[i] = monster._aggro[--monster._aggroCount];
			continue;
		}

		if( entry.entityId == monster._targetId )
		{
			current = entity;
			currentThreat = entry.threat;
		}

		if( entry.threat > bestThreat )
		{
			best = entity;
			bestThreat = entry.threat;
		}
		++i;
	}

	if( best == nullptr )
	{
		monster._targetId = 0;
		return nullptr;
	}

	if( current != nullptr && current != best && bestThreat <= currentThreat + currentThreat / 10 )
		best = current;

	monster._targetId = best->GetId();
	return best;
}

//***************************************************************************
// @brief 선공 범위 안에서 가장 가까운 공격 가능한 플레이어를 찾습니다. (생존, 무적 아님)
// @return 플레이어, 없으면 nullptr
//***************************************************************************
CZoneEntity* CMonsterSystem::FindAggroCandidate(CMonster& monster)
{
	CZoneEntity* best = nullptr;
	float bestDistSq = std::numeric_limits<float>::infinity();
	const Vec2 pos = monster.GetPos();

	_zone.ForEachInRadius(pos, monster._tmpl->aggroRange, [&](CZoneEntity& entity)
		{
			if( entity.GetType() != EntityType::Player || !entity.IsAlive() || _zone.IsInvulnerable(entity) )
				return;

			const float distSq = DistanceSq(pos, entity.GetPos());
			if( distSq < bestDistSq )
			{
				bestDistSq = distSq;
				best = &entity;
			}
		});

	return best;
}

//***************************************************************************
// @brief 몬스터가 피해를 입었을 때 호출합니다. 플레이어가 준 피해만큼 위협을 쌓고 즉시 반응하게 합니다.
//        복귀 중에는 어그로를 받지 않습니다.
// @param attacker 공격자 (없으면 무시)
// @param victim   피해를 입은 엔티티 (몬스터가 아니면 무시)
// @param damage   피해량
//***************************************************************************
void CMonsterSystem::OnDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage)
{
	if( attacker == nullptr || attacker->GetType() != EntityType::Player || !IsAiMonster(victim) )
		return;

	CMonster& monster = static_cast<CMonster&>(victim);
	if( monster._state == EMonsterState::Return )
		return;

	monster.AddThreat(attacker->GetId(), damage);
	Activate(monster);
	monster._nextThinkMs = _zone.GetNowMs();
}

//***************************************************************************
// @brief 몬스터가 죽었을 때 호출합니다. AI 목록에서 빼고 어그로를 비웁니다.
// @param victim 사망한 엔티티 (몬스터가 아니면 무시)
//***************************************************************************
void CMonsterSystem::OnDied(CZoneEntity& victim)
{
	if( !IsAiMonster(victim) )
		return;

	CMonster& monster = static_cast<CMonster&>(victim);
	monster.ClearAggro();
	monster._state = EMonsterState::Idle;
	Deactivate(monster);
}

//***************************************************************************
// @brief 몬스터가 부활했을 때 호출합니다. 복귀 지점을 새 위치로 갱신하고, 시야에 플레이어가 있으면 AI를 깨웁니다.
// @param entity 부활한 엔티티 (몬스터가 아니면 무시)
//***************************************************************************
void CMonsterSystem::OnRespawned(CZoneEntity& entity)
{
	if( !IsAiMonster(entity) )
		return;

	CMonster& monster = static_cast<CMonster&>(entity);
	monster._home = monster.GetPos();
	monster._state = EMonsterState::Idle;
	monster.ClearAggro();

	if( monster._nearPlayers > 0 )
		Activate(monster);
}

//***************************************************************************
// @brief 몬스터가 존에서 제거되기 직전에 호출합니다. AI 활성 목록에서 빼서 해제된 포인터가 남지 않게 합니다.
// @param entity 제거될 엔티티 (몬스터가 아니면 무시)
//***************************************************************************
void CMonsterSystem::OnRemoved(CZoneEntity& entity)
{
	if( !IsAiMonster(entity) )
		return;

	Deactivate(static_cast<CMonster&>(entity));

	if( _monsterCount > 0 )
		--_monsterCount;
}

//***************************************************************************
// @brief 이 시스템이 발급한 AI 몬스터인지 확인합니다. (엔티티 종류가 Monster이고 ID가 몬스터 대역)
// @param entity 확인할 엔티티
// @return AI 몬스터면 true
//***************************************************************************
bool CMonsterSystem::IsAiMonster(const CZoneEntity& entity) const
{
	return entity.GetType() == EntityType::Monster && (entity.GetId() & kMonsterIdBase) != 0;
}

//***************************************************************************
// @brief AI 몬스터의 부활 지연(템플릿의 respawnMs)을 반환합니다.
// @param entity 사망한 엔티티
// @return 지연(ms), AI 몬스터가 아니면 CFieldZoneBase::kNoRespawn
//***************************************************************************
uint32 CMonsterSystem::GetRespawnDelayMs(const CZoneEntity& entity) const
{
	if( !IsAiMonster(entity) )
		return CFieldZoneBase::kNoRespawn;

	return static_cast<const CMonster&>(entity).GetTemplate().respawnMs;
}

//***************************************************************************
// @brief AI 몬스터의 부활 위치를 정합니다. 스폰 중심 반경 안의 무작위 지점이며 존 영역으로 보정한다.
// @param entity 부활하는 엔티티
// @param out    [out] 부활 좌표
// @return AI 몬스터면 true (아니면 out을 건드리지 않음)
//***************************************************************************
bool CMonsterSystem::GetRespawnPos(const CZoneEntity& entity, Vec2& out) const
{
	if( !IsAiMonster(entity) )
		return false;

	const CMonster& monster = static_cast<const CMonster&>(entity);
	out = ClampToZone(RandomPointInDisc(monster.GetAnchor(), monster.GetSpawnRadius()));
	return true;
}

//***************************************************************************
// @brief 원 안에서 균등하게 분포한 무작위 지점을 고릅니다. (반경 sqrt 보정)
// @param center 중심
// @param radius 반경 (0이면 중심)
// @return 무작위 지점
//***************************************************************************
Vec2 CMonsterSystem::RandomPointInDisc(const Vec2& center, float radius) const
{
	if( !(radius > 0.0f) )
		return center;

	constexpr double kTwoPi = 6.283185307179586;
	const double u1 = static_cast<double>(_rng() >> 11) * (1.0 / 9007199254740992.0);
	const double u2 = static_cast<double>(_rng() >> 11) * (1.0 / 9007199254740992.0);
	const double r = static_cast<double>(radius) * std::sqrt(u1);
	const double theta = kTwoPi * u2;

	Vec2 p;
	p.x = center.x + static_cast<float>(r * std::cos(theta));
	p.y = center.y + static_cast<float>(r * std::sin(theta));
	return p;
}

//***************************************************************************
// @brief 좌표를 존 영역 안으로 보정합니다.
// @param pos 보정할 좌표
// @return 영역 안의 좌표
//***************************************************************************
Vec2 CMonsterSystem::ClampToZone(const Vec2& pos) const
{
	const FieldZoneConfig& fc = _zone.GetFieldConfig();

	Vec2 p = pos;
	p.x = (std::min)((std::max)(p.x, fc.minX), fc.maxX);
	p.y = (std::min)((std::max)(p.y, fc.minY), fc.maxY);
	return p;
}