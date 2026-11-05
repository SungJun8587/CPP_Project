
//***************************************************************************
// BossData.cpp : implementation of the CBossData class.
//
//***************************************************************************

#include "pch.h"
#include "BossData.h"
#include "HeroData.h"
#include "MonsterData.h"

#include <cmath>
#include <cstdio>

//***************************************************************************
// @brief 보스를 등록합니다. 보스 ID나 몬스터 ID가 이미 있으면 거부합니다. (값 검사는 Validate)
// @param boss 보스 템플릿
// @return 등록했으면 true, ID 또는 몬스터가 겹치면 false
//***************************************************************************
bool CBossData::AddBoss(const SBossTemplate& boss)
{
	if( _byMonster.count(boss.monsterId) != 0 )
		return false;

	for( const SBossTemplate& b : _bosses )
		if( b.id == boss.id )
			return false;

	_byMonster[boss.monsterId] = _bosses.size();
	_bosses.push_back(boss);
	return true;
}

namespace
{
	//***************************************************************************
	// @brief 보스 데이터 오류 문장을 만듭니다. (boss N phase M action K: 사유)
	//***************************************************************************
	std::string Where(uint32 bossId, uint32 phase, size_t actionIdx, const char* what)
	{
		char buf[256];
		if( phase == 0 )
			std::snprintf(buf, sizeof(buf), "boss %u: %s", bossId, what);
		else if( actionIdx == static_cast<size_t>(-1) )
			std::snprintf(buf, sizeof(buf), "boss %u phase %u: %s", bossId, phase, what);
		else
			std::snprintf(buf, sizeof(buf), "boss %u phase %u action #%u: %s", bossId, phase, static_cast<unsigned>(actionIdx + 1), what);
		return buf;
	}

	//***************************************************************************
	// @brief 보스 자신에게 거는 상태 효과인지 확인합니다. (없는 ID, Self가 아닌 효과는 거부)
	//***************************************************************************
	bool CheckSelfStatus(const CHeroData& data, uint32 statusId, uint32 bossId, uint32 phase, const char* label, std::string& error)
	{
		if( statusId == 0 )
			return true;

		const SStatusTemplate* status = data.FindStatus(statusId);
		if( status == nullptr )
		{
			error = Where(bossId, phase, static_cast<size_t>(-1), label) + " unknown status id";
			return false;
		}
		if( status->target != EStatusTarget::Self )
		{
			error = Where(bossId, phase, static_cast<size_t>(-1), label) + " status must have Target=Self";
			return false;
		}
		return true;
	}
}

//***************************************************************************
// @brief 보스 데이터의 값 범위와 테이블 간 참조를 검사합니다.
//        - 보스: 몬스터 존재, 격노 시간/상태 효과(Self)
//        - 페이즈: 1부터 빠짐없이, 1페이즈 100%, 체력 기준은 점점 작아짐, 진입 상태 효과(Self), 회복 0~100%, 전환 지연 상한
//        - 행동: 개수 상한, Seq가 1부터 늘어나는 순서, 대기 시간 상한, 종류별 필수 값
//          (Cast는 스킬 존재 + Self 대상은 Area 스킬, Summon은 보스가 아닌 몬스터 + 마릿수/반경, Aggro는 Current/Self 대상 불가)
//        - 반복 행동 한 바퀴가 kMinBossLoopMs 이상이어야 한다 (매 틱 행동하는 보스 방지)
// @param monsters 몬스터 데이터 (참조 대상)
// @param skills   스킬/상태 효과 데이터 (참조 대상)
// @param error    [out] 오류 설명
// @return 문제가 없으면 true
//***************************************************************************
bool CBossData::Validate(const CMonsterData& monsters, const CHeroData& skills, std::string& error) const
{
	for( const SBossTemplate& boss : _bosses )
	{
		if( boss.id == 0 )
		{
			error = "boss: id must be at least 1";
			return false;
		}

		if( monsters.FindMonster(boss.monsterId) == nullptr )
		{
			error = Where(boss.id, 0, 0, "unknown MonsterId");
			return false;
		}

		if( boss.enrageAfterMs > kMaxBossEnrageMs )
		{
			error = Where(boss.id, 0, 0, "EnrageAfterMs is too large");
			return false;
		}
		if( (boss.enrageAfterMs == 0) != (boss.enrageStatusId == 0) )
		{
			error = Where(boss.id, 0, 0, "EnrageAfterMs and EnrageStatusId must be set together");
			return false;
		}
		if( !CheckSelfStatus(skills, boss.enrageStatusId, boss.id, 0, "EnrageStatusId", error) )
			return false;

		if( boss.phases.empty() || boss.phases.size() > kMaxBossPhases )
		{
			error = Where(boss.id, 0, 0, "needs 1 to 8 phases");
			return false;
		}

		for( size_t p = 0; p < boss.phases.size(); ++p )
		{
			const SBossPhaseTemplate& phase = boss.phases[p];
			const uint32 pn = static_cast<uint32>(p + 1);

			if( phase.phase != pn )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "Phase numbers must start at 1 and have no gaps");
				return false;
			}
			if( p == 0 && phase.hpBelowPercent != 100 )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "the first phase must have HpBelowPercent=100");
				return false;
			}
			if( p > 0 && !(phase.hpBelowPercent >= 1 && phase.hpBelowPercent < boss.phases[p - 1].hpBelowPercent) )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "HpBelowPercent must be smaller than the previous phase (and at least 1)");
				return false;
			}
			if( phase.enterHealPercent > 100 || phase.transitionMs > kMaxBossTransitionMs )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "EnterHealPercent must be 0..100 and TransitionMs at most 60000");
				return false;
			}
			if( !CheckSelfStatus(skills, phase.enterStatusId, boss.id, pn, "EnterStatusId", error) )
				return false;

			if( phase.actions.size() > kMaxBossActionsPerPhase || phase.loopStart > phase.actions.size() )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "too many actions");
				return false;
			}

			uint64 loopMs = 0;
			uint32 lastSeq = 0;
			EBossTrigger lastTrigger = EBossTrigger::OnEnter;
			for( size_t a = 0; a < phase.actions.size(); ++a )
			{
				const SBossActionTemplate& act = phase.actions[a];
				const bool isLoop = a >= phase.loopStart;

				if( (act.trigger == EBossTrigger::Loop) != isLoop )
				{
					error = Where(boss.id, pn, a, "OnEnter actions must come before Loop actions");
					return false;
				}
				if( a == 0 || act.trigger != lastTrigger )
					lastSeq = 0;
				if( act.seq <= lastSeq )
				{
					error = Where(boss.id, pn, a, "Seq must be unique and increasing within a trigger");
					return false;
				}
				lastSeq = act.seq;
				lastTrigger = act.trigger;

				if( act.delayMs > kMaxBossDelayMs )
				{
					error = Where(boss.id, pn, a, "DelayMs is too large");
					return false;
				}

				uint32 castMs = 0;
				switch( act.type )
				{
				case EBossActionType::Cast:
				{
					const SSkillTemplate* skill = skills.FindSkill(act.skillId);
					if( skill == nullptr )
					{
						error = Where(boss.id, pn, a, "Cast needs a valid SkillId");
						return false;
					}
					if( act.target == EBossTarget::Self && skill->shape != ESkillShape::Area && skill->shape != ESkillShape::Self )
					{
						error = Where(boss.id, pn, a, "Target=Self needs an Area skill (or a Self-shape skill)");
						return false;
					}
					if( skill->shape == ESkillShape::Self && act.target != EBossTarget::Self )
					{
						error = Where(boss.id, pn, a, "a Self skill needs Target=Self");
						return false;
					}
					if( skill->target == ESkillTarget::Ally && act.target != EBossTarget::Self )
					{
						error = Where(boss.id, pn, a, "an Ally skill needs Target=Self (bosses only target players otherwise)");
						return false;
					}
					castMs = skill->castTimeMs;
					break;
				}
				case EBossActionType::Summon:
				{
					const SMonsterTemplate* mon = monsters.FindMonster(act.monsterId);
					if( mon == nullptr )
					{
						error = Where(boss.id, pn, a, "Summon needs a valid MonsterId");
						return false;
					}
					if( _byMonster.count(act.monsterId) != 0 )
					{
						error = Where(boss.id, pn, a, "a boss cannot be summoned");
						return false;
					}
					if( act.count < 1 || act.count > kMaxBossAdds )
					{
						error = Where(boss.id, pn, a, "Count must be 1 up to MaxBossAdds");
						return false;
					}
					if( !(act.spreadRadius >= 0.0f && act.spreadRadius <= kMaxBossSpreadRadius) )
					{
						error = Where(boss.id, pn, a, "SpreadRadius must be 0..30");
						return false;
					}
					if( act.target != EBossTarget::Self )
					{
						error = Where(boss.id, pn, a, "Summon must use Target=Self");
						return false;
					}
					break;
				}
				case EBossActionType::Aggro:
					if( act.target == EBossTarget::Current || act.target == EBossTarget::Self )
					{
						error = Where(boss.id, pn, a, "Aggro cannot target Current or Self");
						return false;
					}
					if( act.skillId != 0 && skills.FindSkill(act.skillId) == nullptr )
					{
						error = Where(boss.id, pn, a, "unknown SkillId");
						return false;
					}
					break;
				default:
					error = Where(boss.id, pn, a, "unknown action type");
					return false;
				}

				if( isLoop )
					loopMs += static_cast<uint64>(castMs) + act.delayMs;
			}

			if( phase.loopStart < phase.actions.size() && loopMs < kMinBossLoopMs )
			{
				error = Where(boss.id, pn, static_cast<size_t>(-1), "the loop actions add up to less than 500ms per cycle");
				return false;
			}
		}
	}

	return true;
}