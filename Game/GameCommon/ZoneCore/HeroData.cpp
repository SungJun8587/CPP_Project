
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

		if( h.stats.maxHp < 1 ) { error = Format("hero %u: maxHp must be >= 1", id); return false; }
		if( h.stats.attack < 0 ) { error = Format("hero %u: attack must be >= 0", id); return false; }
		if( h.stats.defense < 0 ) { error = Format("hero %u: defense must be >= 0", id); return false; }
		if( !IsFinitePositive(h.stats.attackRange) ) { error = Format("hero %u: attackRange must be > 0", id); return false; }
		if( h.stats.attackIntervalMs < 1 ) { error = Format("hero %u: attackIntervalMs must be >= 1", id); return false; }
		if( !IsFinitePositive(h.speed) ) { error = Format("hero %u: speed must be > 0", id); return false; }
	}

	for( const auto& pair : _skills )
	{
		const SSkillTemplate& s = pair.second;
		const unsigned id = s.id;

		if( s.shape != ESkillShape::Single && s.shape != ESkillShape::Area ) { error = Format("skill %u: unknown shape", id); return false; }
		if( s.damagePercent > kMaxSkillDamagePercent ) { error = Format("skill %u: damagePercent is too large", id); return false; }
		if( s.flatDamage < 0 ) { error = Format("skill %u: flatDamage must be >= 0", id); return false; }
		if( s.damagePercent == 0 && s.flatDamage == 0 ) { error = Format("skill %u: needs damagePercent or flatDamage", id); return false; }
		if( !IsFinitePositive(s.range) ) { error = Format("skill %u: range must be > 0", id); return false; }
		if( s.cooldownMs < 1 ) { error = Format("skill %u: cooldownMs must be >= 1", id); return false; }
		if( s.shape == ESkillShape::Single )
		{
			if( s.radius != 0.0f ) { error = Format("skill %u: Single skill must have radius 0", id); return false; }
			if( s.maxTargets != 1 ) { error = Format("skill %u: Single skill must have maxTargets 1", id); return false; }
		}
		else
		{
			if( !IsFinitePositive(s.radius) ) { error = Format("skill %u: Area skill needs radius > 0", id); return false; }
			if( s.maxTargets < 1 || s.maxTargets > kMaxSkillTargets ) { error = Format("skill %u: maxTargets must be 1..32", id); return false; }
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