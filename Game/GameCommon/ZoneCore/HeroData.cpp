
//***************************************************************************
// HeroData.cpp : implementation of the CHeroData class.
//
//***************************************************************************

#include "pch.h"
#include "HeroData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	std::string Format(const char* fmt, unsigned a)
	{
		char buf[192];
		std::snprintf(buf, sizeof(buf), fmt, a);
		return buf;
	}

	bool IsFinitePositive(float v) { return std::isfinite(v) && v > 0.0f; }
}

//***************************************************************************
// @brief 영웅 템플릿을 추가합니다.
// @param hero 추가할 템플릿
// @return 추가했으면 true, ID가 0이거나 이미 있으면 false
//***************************************************************************
bool CHeroData::AddHero(const SHeroTemplate& hero)
{
	if( hero.id == 0 )
		return false;

	return _heroes.emplace(hero.id, hero).second;
}

//***************************************************************************
// @brief 스킬 템플릿을 추가합니다.
// @return 추가했으면 true, ID가 0이거나 이미 있으면 false
//***************************************************************************
bool CHeroData::AddSkill(const SSkillTemplate& skill)
{
	if( skill.id == 0 )
		return false;

	return _skills.emplace(skill.id, skill).second;
}

//***************************************************************************
// @brief 상태 효과 템플릿을 추가합니다.
// @return 추가했으면 true, ID가 0이거나 이미 있으면 false
//***************************************************************************
bool CHeroData::AddStatus(const SStatusTemplate& status)
{
	if( status.id == 0 )
		return false;

	return _statuses.emplace(status.id, status).second;
}

//***************************************************************************
// @brief 영웅에게 스킬을 연결합니다. 영웅이 쓸 수 있는 스킬이 된다.
// @return 연결했으면 true, 영웅/스킬이 없거나 이미 연결됐거나 영웅당 상한(kMaxHeroSkills)을 넘으면 false
//***************************************************************************
bool CHeroData::AssignSkill(uint32 heroId, uint32 skillId)
{
	auto hero = _heroes.find(heroId);
	if( hero == _heroes.end() || _skills.find(skillId) == _skills.end() )
		return false;

	std::vector<uint32>& skills = hero->second.skills;
	if( skills.size() >= kMaxHeroSkills || std::find(skills.begin(), skills.end(), skillId) != skills.end() )
		return false;

	skills.push_back(skillId);
	return true;
}

//***************************************************************************
// @brief 영웅 데이터의 값 범위를 검사합니다. 첫 번째 오류만 설명으로 돌려줍니다.
//        검사 항목: 영웅 1개 이상, maxHp >= 1, attack/defense >= 0, 사거리/속도 유한하고 > 0, 공격 간격 >= 1
// @param error [out] 오류 설명
// @return 문제가 없으면 true
//***************************************************************************
bool CHeroData::Validate(std::string& error) const
{
	if( _heroes.empty() )
	{
		error = "at least one hero is required";
		return false;
	}

	for( const auto& pair : _heroes )
	{
		const SHeroTemplate& h = pair.second;
		const unsigned id = h.id;

		if( h.stats.maxHp < 1 )				{ error = Format("hero %u: maxHp must be >= 1", id); return false; }
		if( h.stats.attack < 0 )			{ error = Format("hero %u: attack must be >= 0", id); return false; }
		if( h.stats.defense < 0 )			{ error = Format("hero %u: defense must be >= 0", id); return false; }
		if( !IsFinitePositive(h.stats.attackRange) )	{ error = Format("hero %u: attackRange must be > 0", id); return false; }
		if( h.stats.attackIntervalMs < 1 )	{ error = Format("hero %u: attackIntervalMs must be >= 1", id); return false; }
		if( !IsFinitePositive(h.speed) )	{ error = Format("hero %u: speed must be > 0", id); return false; }
	}

	for( const auto& pair : _skills )
	{
		const SSkillTemplate& s = pair.second;
		const unsigned id = s.id;

		const bool shapeKnown = s.shape == ESkillShape::Single || s.shape == ESkillShape::Area || s.shape == ESkillShape::Self
			|| s.shape == ESkillShape::Cone || s.shape == ESkillShape::Line;
		if( !shapeKnown )									{ error = Format("skill %u: unknown shape", id); return false; }
		if( s.target != ESkillTarget::Enemy && s.target != ESkillTarget::Ally )	{ error = Format("skill %u: unknown target", id); return false; }
		if( s.healPercent > kMaxSkillDamagePercent || s.flatHeal < 0 )			{ error = Format("skill %u: heal out of range", id); return false; }
		if( SkillHasHeal(s) && s.target != ESkillTarget::Ally )					{ error = Format("skill %u: heal needs target Ally", id); return false; }
		if( SkillHasDamage(s) && s.target == ESkillTarget::Ally )				{ error = Format("skill %u: Ally skill cannot deal damage", id); return false; }
		if( s.damagePercent > kMaxSkillDamagePercent )		{ error = Format("skill %u: damagePercent is too large", id); return false; }
		if( s.flatDamage < 0 )								{ error = Format("skill %u: flatDamage must be >= 0", id); return false; }
		if( !SkillHasDamage(s) && !SkillHasHeal(s) && s.statusId == 0 )	{ error = Format("skill %u: needs damage, heal or statusId", id); return false; }
		if( s.castTimeMs > kMaxCastTimeMs )					{ error = Format("skill %u: castTimeMs is too large", id); return false; }
		if( !std::isfinite(s.projectileSpeed) || s.projectileSpeed < 0.0f || s.projectileSpeed > kMaxProjectileSpeed )
															{ error = Format("skill %u: projectileSpeed must be 0..1000", id); return false; }
		if( s.statusId != 0 && FindStatus(s.statusId) == nullptr )	{ error = Format("skill %u: unknown statusId", id); return false; }
		if( s.shape == ESkillShape::Self )
		{
			if( s.range != 0.0f )							{ error = Format("skill %u: Self skill must have range 0", id); return false; }
		}
		else if( !IsFinitePositive(s.range) )				{ error = Format("skill %u: range must be > 0", id); return false; }
		if( s.shape == ESkillShape::Cone )
		{
			if( !std::isfinite(s.angle) || s.angle <= 0.0f || s.angle > 360.0f )	{ error = Format("skill %u: Cone angle must be 0..360", id); return false; }
		}
		else if( s.angle != 0.0f )							{ error = Format("skill %u: only Cone uses angle", id); return false; }
		if( s.projectileSpeed > 0.0f && s.shape != ESkillShape::Single && s.shape != ESkillShape::Area )
															{ error = Format("skill %u: projectile needs Single or Area", id); return false; }
		if( s.cooldownMs < 1 )								{ error = Format("skill %u: cooldownMs must be >= 1", id); return false; }
		if( s.shape == ESkillShape::Single )
		{
			if( s.radius != 0.0f )							{ error = Format("skill %u: Single skill must have radius 0", id); return false; }
			if( s.maxTargets != 1 )							{ error = Format("skill %u: Single skill must have maxTargets 1", id); return false; }
		}
		else
		{
			if( s.shape == ESkillShape::Cone )
			{
				if( s.radius != 0.0f )						{ error = Format("skill %u: Cone skill must have radius 0", id); return false; }
			}
			else if( !IsFinitePositive(s.radius) )			{ error = Format("skill %u: Area/Self/Line skill needs radius > 0", id); return false; }
			if( s.maxTargets < 1 || s.maxTargets > kMaxSkillTargets )	{ error = Format("skill %u: maxTargets must be 1..32", id); return false; }
		}

		// 비행 시간이 상한을 넘는 조합은 투사체가 도착하기 전에 시야 밖으로 사라지고 타이머가 오래 남는다.
		if( s.projectileSpeed > 0.0f && (s.range + s.radius) / s.projectileSpeed * 1000.0f > static_cast<float>(kMaxProjectileFlightMs) )
															{ error = Format("skill %u: projectile flight time exceeds the limit", id); return false; }
	}

	for( const auto& pair : _statuses )
	{
		const SStatusTemplate& t = pair.second;
		const unsigned id = t.id;

		const bool kindKnown = t.kind == EStatusKind::Dot || t.kind == EStatusKind::Buff || t.kind == EStatusKind::Stun
			|| t.kind == EStatusKind::Shield || t.kind == EStatusKind::Hot;
		if( !kindKnown )														{ error = Format("status %u: unknown kind", id); return false; }
		if( t.healPercent > kMaxSkillDamagePercent || t.flatHeal < 0 )			{ error = Format("status %u: amount out of range", id); return false; }
		if( t.kind != EStatusKind::Hot && t.kind != EStatusKind::Shield && (t.healPercent != 0 || t.flatHeal != 0) )
																				{ error = Format("status %u: only Hot/Shield use healPercent/flatHeal", id); return false; }
		if( t.target != EStatusTarget::Hit && t.target != EStatusTarget::Self )	{ error = Format("status %u: unknown target", id); return false; }
		if( t.durationMs < 1 || t.durationMs > kMaxStatusDurationMs )			{ error = Format("status %u: durationMs must be 1..600000", id); return false; }
		if( t.kind == EStatusKind::Dot )
		{
			if( t.tickMs < kMinStatusTickMs || t.tickMs > t.durationMs )		{ error = Format("status %u: Dot tickMs must be 100..durationMs", id); return false; }
			if( t.damagePercent > kMaxSkillDamagePercent || t.flatDamage < 0 )	{ error = Format("status %u: Dot damage out of range", id); return false; }
			if( t.damagePercent == 0 && t.flatDamage == 0 )						{ error = Format("status %u: Dot needs damagePercent or flatDamage", id); return false; }
			if( t.attackPercent != 0 || t.defensePercent != 0 || t.speedPercent != 0 )
																				{ error = Format("status %u: Dot must not have stat percents", id); return false; }
		}
		else if( t.kind == EStatusKind::Hot )
		{
			if( t.tickMs < kMinStatusTickMs || t.tickMs > t.durationMs )		{ error = Format("status %u: Hot tickMs must be 100..durationMs", id); return false; }
			if( t.healPercent == 0 && t.flatHeal == 0 )							{ error = Format("status %u: Hot needs healPercent or flatHeal", id); return false; }
			if( t.damagePercent != 0 || t.flatDamage != 0 || t.attackPercent != 0 || t.defensePercent != 0 || t.speedPercent != 0 )
																				{ error = Format("status %u: Hot must not have damage or stat percents", id); return false; }
		}
		else if( t.kind == EStatusKind::Shield )
		{
			if( t.healPercent == 0 && t.flatHeal == 0 )							{ error = Format("status %u: Shield needs healPercent or flatHeal", id); return false; }
			if( t.tickMs != 0 || t.damagePercent != 0 || t.flatDamage != 0 || t.attackPercent != 0 || t.defensePercent != 0 || t.speedPercent != 0 )
																				{ error = Format("status %u: Shield must not have tick, damage or stat percents", id); return false; }
		}
		else if( t.kind == EStatusKind::Stun )
		{
			if( t.tickMs != 0 || t.damagePercent != 0 || t.flatDamage != 0 || t.attackPercent != 0 || t.defensePercent != 0 || t.speedPercent != 0 )
																				{ error = Format("status %u: Stun must not have tick, damage or stat percents", id); return false; }
		}
		else
		{
			if( t.tickMs != 0 || t.damagePercent != 0 || t.flatDamage != 0 )	{ error = Format("status %u: Buff must not have tick or damage", id); return false; }
			if( t.attackPercent == 0 && t.defensePercent == 0 && t.speedPercent == 0 )
																				{ error = Format("status %u: Buff needs a stat percent", id); return false; }
			if( t.attackPercent < kMinStatPercent || t.attackPercent > kMaxStatPercent
				|| t.defensePercent < kMinStatPercent || t.defensePercent > kMaxStatPercent
				|| t.speedPercent < kMinStatPercent || t.speedPercent > kMaxSpeedBuffPercent )
																				{ error = Format("status %u: stat percent out of range", id); return false; }
		}
	}

	return true;
}

//***************************************************************************
// @brief 내장 기본 영웅 하나만 가진 데이터를 만듭니다. 게임 데이터 폴더 없이 구동할 때(개발/테스트) 쓴다.
//        값은 SCombatStats/SHeroTemplate의 기본값이다.
// @return 기본 영웅 데이터
//***************************************************************************
std::shared_ptr<const CHeroData> CHeroData::MakeDefault()
{
	auto data = std::make_shared<CHeroData>();

	SHeroTemplate hero;
	hero.id = 1;
	hero.name = "Default";
	data->AddHero(hero);

	return data;
}
