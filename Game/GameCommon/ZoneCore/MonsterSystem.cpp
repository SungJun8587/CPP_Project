
//***************************************************************************
// MonsterSystem.cpp : implementation of the CMonsterSystem class.
//
//***************************************************************************

#include "pch.h"
#include "MonsterSystem.h"
#include "BossSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

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
//        - AI 튜닝값 범위
//        - 스폰 중심이 존 영역 안이고 장애물 밖 (오류에 스폰 순번과 좌표를 적는다)
//        - 스폰 테이블에 나오는 템플릿 검사(ValidateTemplate)
//        첫 오류에서 멈추지 않고 모아서 한 번에 돌려준다(최대 kMaxReportedErrors개).
// @param error            [out] 오류 설명 (여러 개면 줄바꿈으로 구분)
// @param opponentMaxSpeed 몬스터와 맞붙는 상대(플레이어)가 가질 수 있는 최고 이동 속도 (units/sec). 모르면 0
// @return 문제가 없으면 true
//***************************************************************************
bool CMonsterSystem::Validate(std::string& error, float opponentMaxSpeed) const
{
	std::vector<std::string> errors;
	char buf[256];

	if( !(_config.engageRatio > 0.0f && _config.engageRatio <= 1.0f) || !(_config.repathDistance > 0.0f) )
		errors.emplace_back("monster system config: engageRatio must be in (0, 1] and repathDistance > 0");

	if( _config.pathSearchesPerTick < 1 || _config.pathMaxNodes < 1 )
		errors.emplace_back("monster system config: pathSearchesPerTick and pathMaxNodes must be >= 1");

	if( _data != nullptr )
	{
		uint64 totalCount = 0;
		bool overLimitReported = false;
		std::set<uint32> checkedTemplates;

		const std::vector<SSpawnEntry>& spawns = _data->GetSpawns();
		for( size_t i = 0; i < spawns.size(); ++i )
		{
			const SSpawnEntry& s = spawns[i];
			if( s.zoneId != _zone.GetZoneId() )
				continue;

			totalCount += s.count;
			if( totalCount > _config.maxMonsters && !overLimitReported )
			{
				overLimitReported = true;
				std::snprintf(buf, sizeof(buf), "zone %u: total spawn count exceeds the configured limit %u (reached at spawn[%zu])", s.zoneId, _config.maxMonsters, i);
				errors.emplace_back(buf);
			}

			const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
			if( tmpl == nullptr )
			{
				std::snprintf(buf, sizeof(buf), "spawn[%zu] zone %u: unknown monster id %u", i, s.zoneId, s.monsterId);
				errors.emplace_back(buf);
				continue;
			}

			const Vec2 center{ s.x, s.y };
			if( !_zone.IsInBounds(center) )
			{
				std::snprintf(buf, sizeof(buf), "spawn[%zu] zone %u: center (%.1f, %.1f) of monster %u is outside the zone bounds", i, s.zoneId, s.x, s.y, s.monsterId);
				errors.emplace_back(buf);
			}
			else if( _zone.IsBlocked(center) )
			{
				std::snprintf(buf, sizeof(buf), "spawn[%zu] zone %u: center (%.1f, %.1f) of monster %u is inside an obstacle", i, s.zoneId, s.x, s.y, s.monsterId);
				errors.emplace_back(buf);
			}

			if( checkedTemplates.insert(tmpl->id).second )
				ValidateTemplate(*tmpl, opponentMaxSpeed, errors);
		}
	}

	return JoinErrors(errors, error);
}

//***************************************************************************
// @brief 몬스터 템플릿 하나가 이 존의 AOI 시야 안에서 동작하는지 검사하고, 문제를 errors에 덧붙입니다.
//        스폰 테이블에 나오는 몬스터와 보스가 소환하는 몬스터가 같은 기준을 쓰도록 분리했다.
//        - 선공 감지 거리 <= AOI 최소 보장 시야 (시야 밖 플레이어는 AI를 깨우지 못하므로 감지할 수 없다)
//        - 기본 공격 판정 거리(사거리 + 허용 오차 + 이동 지연 보정) <= AOI 최소 보장 시야.
//          이동 지연 보정은 공격자와 대상이 모두 최고 속도로 움직일 때를 가정하므로,
//          속도는 몬스터가 낼 수 있는 최고 속도(템플릿 속도 + 이동속도 버프 최대치)와 상대(플레이어)의 최고 속도 중 큰 값을 쓴다
//        - 도움 요청 거리 <= AOI 최소 보장 시야
//        - 몬스터 스킬: 스킬 데이터가 있고, 적 대상의 비투사체 스킬이며, 시전 거리 + 효과 반경이 AOI 보장 시야 안 (보스 스킬과 같은 기준)
// @param tmpl             검사할 템플릿
// @param opponentMaxSpeed 상대(플레이어)가 가질 수 있는 최고 이동 속도 (units/sec). 모르면 0
// @param errors           [in,out] 오류 설명을 덧붙일 목록
// @return 문제가 없으면 true
//***************************************************************************
bool CMonsterSystem::ValidateTemplate(const SMonsterTemplate& tmpl, float opponentMaxSpeed, std::vector<std::string>& errors) const
{
	const size_t before = errors.size();
	const float viewRange = _zone.GetGuaranteedViewRange();
	const float buffMul = 1.0f + static_cast<float>(_skills != nullptr ? _skills->GetMaxSpeedBuffPercent() : 0) * 0.01f;
	const float reachSpeed = (std::max)(tmpl.speed * buffMul, opponentMaxSpeed);
	char buf[256];

	if( tmpl.aggroRange > viewRange )
	{
		std::snprintf(buf, sizeof(buf), "monster %u: aggroRange %.1f exceeds the guaranteed AOI view range %.1f", tmpl.id, tmpl.aggroRange, viewRange);
		errors.emplace_back(buf);
	}

	if( !_zone.IsAttackRangeSupported(tmpl.stats.attackRange, reachSpeed) )
	{
		std::snprintf(buf, sizeof(buf), "monster %u: attack reach (range %.1f) exceeds the guaranteed AOI view range %.1f", tmpl.id, tmpl.stats.attackRange, viewRange);
		errors.emplace_back(buf);
	}

	if( tmpl.assistRange > viewRange )
	{
		std::snprintf(buf, sizeof(buf), "monster %u: assistRange %.1f exceeds the guaranteed AOI view range %.1f", tmpl.id, tmpl.assistRange, viewRange);
		errors.emplace_back(buf);
	}

	if( tmpl.skillId != 0 )
	{
		const SSkillTemplate* skill = _skills != nullptr ? _skills->FindSkill(tmpl.skillId) : nullptr;
		if( skill == nullptr )
		{
			std::snprintf(buf, sizeof(buf), "monster %u: unknown skill id %u (skill data missing?)", tmpl.id, tmpl.skillId);
			errors.emplace_back(buf);
		}
		else if( skill->target != ESkillTarget::Enemy || skill->projectileSpeed > 0.0f )
		{
			std::snprintf(buf, sizeof(buf), "monster %u: skill %u must target enemies and must not be a projectile", tmpl.id, tmpl.skillId);
			errors.emplace_back(buf);
		}
		else if( !_zone.IsAttackRangeSupported(skill->range + skill->radius, reachSpeed) )
		{
			std::snprintf(buf, sizeof(buf), "monster %u: skill %u reach (range + radius %.1f) exceeds the guaranteed AOI view range %.1f", tmpl.id, tmpl.skillId, skill->range + skill->radius, viewRange);
			errors.emplace_back(buf);
		}
	}

	return errors.size() == before;
}

//***************************************************************************
// @brief 오류 목록을 줄바꿈으로 이어 붙여 error에 담습니다. 많으면 앞의 kMaxReportedErrors개만 적고 나머지 수를 덧붙입니다.
// @param errors 오류 설명 목록
// @param error  [out] 합친 설명 (목록이 비었으면 건드리지 않는다)
// @return 목록이 비었으면 true (검증 통과)
//***************************************************************************
bool CMonsterSystem::JoinErrors(const std::vector<std::string>& errors, std::string& error)
{
	if( errors.empty() )
		return true;

	constexpr size_t kMaxReportedErrors = 16;
	error.clear();
	for( size_t i = 0; i < errors.size() && i < kMaxReportedErrors; ++i )
	{
		if( i != 0 )
			error += '\n';
		error += errors[i];
	}

	if( errors.size() > kMaxReportedErrors )
	{
		char buf[48];
		std::snprintf(buf, sizeof(buf), "\n(+%zu more)", errors.size() - kMaxReportedErrors);
		error += buf;
	}

	return false;
}

//***************************************************************************
// @brief 이 존의 스폰 테이블에 나오는 몬스터가 낼 수 있는 가장 빠른 이동 속도(템플릿 속도 + 이동속도 버프 최대치)를 반환합니다.
//        플레이어의 공격 판정 거리 검증(IsAttackRangeSupported)에 상대 속도로 씁니다.
// @return 최고 속도 (units/sec), 스폰이 없으면 0
//***************************************************************************
float CMonsterSystem::GetMaxSpawnSpeed() const
{
	float maxSpeed = 0.0f;
	if( _data == nullptr )
		return maxSpeed;

	// 이동속도 버프를 받은 몬스터가 낼 수 있는 최고 속도로 센다(런타임 합계는 kMaxSpeedBuffPercent로 묶인다).
	const float buffMul = 1.0f + static_cast<float>(_skills != nullptr ? _skills->GetMaxSpeedBuffPercent() : 0) * 0.01f;

	for( const SSpawnEntry& s : _data->GetSpawns() )
	{
		if( s.zoneId != _zone.GetZoneId() )
			continue;

		const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
		if( tmpl != nullptr && tmpl->speed * buffMul > maxSpeed )
			maxSpeed = tmpl->speed * buffMul;
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
	uint64 expected = 0;
	for( const SSpawnEntry& s : _data->GetSpawns() )
	{
		if( s.zoneId != _zone.GetZoneId() )
			continue;

		const SMonsterTemplate* tmpl = _data->FindMonster(s.monsterId);
		if( tmpl == nullptr )
			continue;

		expected += s.count;

		Vec2 anchor;
		anchor.x = s.x;
		anchor.y = s.y;

		for( uint32 i = 0; i < s.count; ++i )
		{
			const Vec2 pos = PickFreePoint(anchor, s.radius);

			std::unique_ptr<CMonster> monster = std::make_unique<CMonster>(kMonsterIdBase | ++_serial, *tmpl, this, anchor, s.radius);
			monster->SetSpawnPos(pos);
			monster->_home = pos;

			CMonster* raw = monster.get();
			if( _zone.AddEntity(std::move(monster)) != nullptr )
			{
				++spawned;
				++_monsterCount;

				if( _boss != nullptr )
					_boss->OnSpawned(*raw);
			}
			else
			{
				LOG_ERROR(_T("[CMonsterSystem] spawn failed. zoneId=%u, monsterId=%u, pos=(%.1f, %.1f)"),
					_zone.GetZoneId(), s.monsterId, pos.x, pos.y);
			}
		}
	}

	if( spawned != expected )
	{
		LOG_ERROR(_T("[CMonsterSystem] spawned fewer monsters than the spawn table. zoneId=%u, expected=%llu, spawned=%u"),
			_zone.GetZoneId(), static_cast<unsigned long long>(expected), spawned);
	}

	return spawned;
}

//***************************************************************************
// @brief 보스가 부르는 몬스터를 만들어 존에 배치합니다. 소환수는 부활하지 않고(보스가 리셋/사망하면 사라짐),
//        스폰 지점이 곧 복귀 지점이며, 지정한 대상에게 위협을 두고 바로 사고를 시작합니다.
// @param monsterId      몬스터 템플릿 ID
// @param center         소환 중심
// @param spreadRadius   소환 반경 (장애물 밖의 무작위 지점)
// @param ownerId        소환한 보스의 엔티티 ID
// @param threatTargetId 처음부터 노릴 대상 (0이면 없음)
// @return 소환수, 템플릿이 없거나 존에 등록하지 못했으면 nullptr
//***************************************************************************
CMonster* CMonsterSystem::SpawnSummon(uint32 monsterId, const Vec2& center, float spreadRadius, uint64 ownerId, uint64 threatTargetId)
{
	const SMonsterTemplate* tmpl = _data != nullptr ? _data->FindMonster(monsterId) : nullptr;
	if( tmpl == nullptr )
		return nullptr;

	const Vec2 pos = PickFreePoint(center, spreadRadius);

	std::unique_ptr<CMonster> monster = std::make_unique<CMonster>(kMonsterIdBase | ++_serial, *tmpl, this, pos, 0.0f);
	monster->SetSpawnPos(pos);
	monster->_home = pos;
	monster->_summoned = true;
	monster->_ownerId = ownerId;

	CMonster* raw = monster.get();
	if( _zone.AddEntity(std::move(monster)) == nullptr )
	{
		LOG_ERROR(_T("[CMonsterSystem] summon failed. zoneId=%u, monsterId=%u, ownerId=%llu"),
			_zone.GetZoneId(), monsterId, static_cast<unsigned long long>(ownerId));
		return nullptr;
	}

	++_monsterCount;

	if( threatTargetId != 0 )
		raw->AddThreat(threatTargetId, 1);

	Activate(*raw);
	raw->_nextThinkMs = _zone.GetNowMs();
	return raw;
}

//***************************************************************************
// @brief 매 틱 호출합니다. 활성 몬스터 중 사고 시각이 된 것만 사고합니다.
//        이동 갱신이 끝난 뒤에 호출해야 한다(사고가 공격/이동 명령을 내리므로 이동 갱신 중에는 금지).
//***************************************************************************
void CMonsterSystem::Update()
{
	const uint64 nowMs = _zone.GetNowMs();
	_pathBudget = _config.pathSearchesPerTick;

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

	// 기절한 동안은 움직이지도 공격하지도 않는다. 이동 실패로 대상을 놓치지 않도록 사고 자체를 건너뛴다.
	if( _zone.IsStunned(monster) )
		return;

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

	if( target == nullptr && monster._tmpl->aggroRange > 0.0f && nowMs >= monster._noAggroUntilMs )
	{
		target = FindAggroCandidate(monster);
		if( target != nullptr )
		{
			monster.AddThreat(target->GetId(), 1);
			AlertAllies(monster, target->GetId());
		}
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
	// 시전 중에는 움직이지도 기본 공격을 하지도 않는다 (이동하면 시전이 취소된다). 시전이 끝나면 다음 사고에서 이어간다.
	if( monster.IsCasting() )
		return;

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

	// 보스는 페이즈/행동을 먼저 처리한다. 시전을 시작했으면 이번 사고는 여기까지.
	if( monster._boss != nullptr && _boss != nullptr && _boss->OnChase(monster, *target, nowMs) )
		return;

	// 몬스터 스킬은 사거리 안이고 재사용 대기가 끝났으면 기본 공격/추적보다 먼저 쓴다. 시전을 시작했으면 이번 사고는 여기까지.
	if( tmpl.skillId != 0 && monster._boss == nullptr && TryUseSkill(monster, *target, nowMs) )
		return;

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
			case AttackResult::NoLineOfSight:
				// 사거리 안이지만 벽이 가로막고 있다. 시선이 트이는 곳으로 돌아가 보고, 갈 길이 없으면 포기한다.
				if( !StepToward(monster, target->GetPos()) )
					GiveUpTarget(monster, *target, nowMs);
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

	dest = _zone.ClampToBounds(dest);
	if( _zone.IsBlocked(dest) )
		dest = target->GetPos();		// 사거리 안쪽 지점이 장애물 안이면 대상 위치로 향한다

	if( StepToward(monster, dest) )
		monster._lastTargetPos = target->GetPos();
	else if( !monster.IsMoving() )
		GiveUpTarget(monster, *target, nowMs);		// 다가갈 수 없는 대상(벽에 막힘). 이동 중 거부는 다음 사고에서 다시 본다.
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

	// 한 걸음(경유점)씩 복귀한다. 이동 명령이 거부되면(영역 밖 설정 오류, 길이 막힘) 순간이동으로 복귀를 마무리한다.
	if( !StepToward(monster, monster._home) )
		_zone.Teleport(monster.GetId(), monster._home);
}

//***************************************************************************
// @brief 복귀 상태로 전환합니다. 어그로를 비우고 스폰 지점으로 이동을 시작한다.
//***************************************************************************
void CMonsterSystem::EnterReturn(CMonster& monster)
{
	monster._state = EMonsterState::Return;
	monster.ClearAggro();

	if( _boss != nullptr )
		_boss->OnReset(monster);

	if( !StepToward(monster, monster._home) )
		_zone.Teleport(monster.GetId(), monster._home);
}

//***************************************************************************
// @brief 닿을 수 없는 대상을 포기합니다. 어그로에서 빼고, 한동안 근접 선공으로 다시 잡지 않아
//        같은 대상에 대한 추적-복귀 반복을 막습니다. (피해를 준 대상은 OnDamaged로 다시 위협이 쌓인다)
//***************************************************************************
void CMonsterSystem::GiveUpTarget(CMonster& monster, CZoneEntity& target, uint64 nowMs)
{
	monster.RemoveThreat(target.GetId());
	monster._noAggroUntilMs = nowMs + _config.pathRetryMs;
}

//***************************************************************************
// @brief 몬스터를 목표 쪽으로 한 걸음 보냅니다. 직선이 비어 있으면 목표로 곧장 가고, 막혀 있으면 우회 경로의
//        첫 경유점으로 간다. 경유점에 도착해 멈추면 다음 사고에서 다시 호출되어 다음 경유점으로 이어진다(경로를 저장하지 않는다).
//        경로가 없으면 pathRetryMs 동안은 탐색하지 않고 실패를 돌려준다.
// @param monster 이동시킬 몬스터
// @param goal    목표 지점 (이동 가능한 위치)
// @return 이동 명령을 냈으면 true
//***************************************************************************
bool CMonsterSystem::StepToward(CMonster& monster, const Vec2& goal)
{
	const CBlockMap* blockMap = _zone.GetBlockMap();
	if( blockMap == nullptr || blockMap->IsSegmentClear(monster.GetPos(), goal) )
		return _zone.MoveEntity(monster, goal);

	const uint64 nowMs = _zone.GetNowMs();
	if( nowMs < monster._noPathUntilMs )
		return false;

	if( _pathBudget == 0 )
		return true;		// 이번 틱 탐색 예산을 다 썼다. 제자리에서 다음 사고(주기 뒤)에 다시 시도한다
	--_pathBudget;

	if( !blockMap->FindPath(monster.GetPos(), goal, _pathScratch, _config.pathMaxNodes) || _pathScratch.empty() )
	{
		monster._noPathUntilMs = nowMs + _config.pathRetryMs;
		return false;
	}

	return _zone.MoveEntity(monster, _pathScratch.front());
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
//        AOI 관찰자만 훑으므로 플레이어 엔티티는 CZoneEntity::IsAoiWatcher()가 true(기본값)여야 한다.
// @return 플레이어, 없으면 nullptr
//***************************************************************************
CZoneEntity* CMonsterSystem::FindAggroCandidate(CMonster& monster)
{
	CZoneEntity* best = nullptr;
	float bestDistSq = std::numeric_limits<float>::infinity();
	const Vec2 pos = monster.GetPos();

	_zone.ForEachLivePlayerInRadius(pos, monster._tmpl->aggroRange, [&](CZoneEntity& entity)
	{
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
// @brief 무리 가세: source 주변의 대기 중인 몬스터가 같은 대상을 노리게 합니다.
//        source의 assistRange 안에서, 대기 상태이고 어그로 표가 비어 있으며 보스가 아닌 살아 있는 몬스터만 깨웁니다.
//        한 번에 kMaxAlertPerCall마리까지만 깨워 밀집 지역에서 한 틱 비용이 튀지 않게 한다. 깨어난 몬스터는 다시 다른 몬스터를 깨우지 않는다.
// @param source   먼저 싸우기 시작한 몬스터
// @param targetId 노릴 대상 ID
//***************************************************************************
void CMonsterSystem::AlertAllies(CMonster& source, uint64 targetId)
{
	const float range = source._tmpl->assistRange;
	if( !(range > 0.0f) || source._boss != nullptr )
		return;

	constexpr uint32 kMaxAlertPerCall = 12;
	const uint64 nowMs = _zone.GetNowMs();
	uint32 alerted = 0;

	_zone.ForEachInRadius(source.GetPos(), range, [&](CZoneEntity& e)
		{
			if( alerted >= kMaxAlertPerCall || &e == &source || !IsAiMonster(e) || !e.IsAlive() )
				return;

			CMonster& ally = static_cast<CMonster&>(e);
			if( ally._boss != nullptr || ally._state != EMonsterState::Idle || ally.GetAggroCount() != 0 || nowMs < ally._noAggroUntilMs )
				return;

			ally.AddThreat(targetId, 1);
			Activate(ally);
			ally._nextThinkMs = nowMs;
			++alerted;
		});
}

//***************************************************************************
// @brief 몬스터 스킬을 씁니다. 재사용 대기가 끝났고 대상이 스킬 사거리 안일 때만 시전합니다.
//        Single은 대상을, Area는 대상 위치를, Self는 자기 위치를, Cone/Line은 대상 쪽 방향을 겨냥한다.
// @return 시전을 시작했거나 즉시 발동했으면 true (이번 사고에서 다른 행동을 하지 않는다)
//***************************************************************************
bool CMonsterSystem::TryUseSkill(CMonster& monster, CZoneEntity& target, uint64 nowMs)
{
	if( _skills == nullptr || nowMs < monster._skillReadyAtMs )
		return false;

	const SSkillTemplate* skill = _skills->FindSkill(monster._tmpl->skillId);
	if( skill == nullptr )
		return false;

	const float distSq = DistanceSq(monster.GetPos(), target.GetPos());
	const float reach = skill->shape == ESkillShape::Self ? skill->radius * 0.8f : skill->range;
	if( distSq > reach * reach )
		return false;

	Vec2 center = target.GetPos();
	uint64 targetId = 0;
	if( skill->shape == ESkillShape::Self )
		center = monster.GetPos();
	else if( skill->shape == ESkillShape::Single )
		targetId = target.GetId();

	return _zone.CastSkill(monster, *skill, targetId, center, monster._skillReadyAtMs) == SkillResult::Ok;
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

	const bool firstBlood = monster.GetAggroCount() == 0;
	monster.AddThreat(attacker->GetId(), damage);
	Activate(monster);
	monster._nextThinkMs = _zone.GetNowMs();

	// 처음 맞은 순간에만 주변에 알린다 (맞을 때마다 알리면 비용만 든다).
	if( firstBlood )
		AlertAllies(monster, attacker->GetId());
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

	if( _boss != nullptr )
		_boss->OnDied(monster);

	// 소환수는 부활하지 않으므로 시체를 잠시 두었다 치운다.
	if( monster._summoned )
	{
		const uint64 id = monster.GetId();
		_zone.ScheduleAfter(CBossSystem::kAddCorpseMs, [id](CZone& zone)
			{
				static_cast<CFieldZoneBase&>(zone).RemoveEntity(id);
			});
	}
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

	if( _boss != nullptr )
		_boss->OnReset(monster);

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

	CMonster& removed = static_cast<CMonster&>(entity);
	Deactivate(removed);

	if( _boss != nullptr )
		_boss->OnRemoved(removed);

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

	const CMonster& monster = static_cast<const CMonster&>(entity);
	return monster._summoned ? CFieldZoneBase::kNoRespawn : monster.GetTemplate().respawnMs;
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
	out = PickFreePoint(monster.GetAnchor(), monster.GetSpawnRadius());
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
// @brief 스폰 중심 반경 안에서 장애물 밖인 무작위 지점을 고릅니다. 영역 안으로 보정하며, 여러 번 시도해도 못 찾으면 중심을 씁니다.
//        중심은 Validate()에서 장애물 밖임을 확인하므로 항상 유효하다.
// @param center 중심
// @param radius 반경
// @return 이동 가능한 지점
//***************************************************************************
Vec2 CMonsterSystem::PickFreePoint(const Vec2& center, float radius) const
{
	constexpr int kMaxTries = 16;
	for( int i = 0; i < kMaxTries; ++i )
	{
		const Vec2 p = _zone.ClampToBounds(RandomPointInDisc(center, radius));
		if( _zone.IsWalkable(p) )
			return p;
	}
	return _zone.ClampToBounds(center);
}

