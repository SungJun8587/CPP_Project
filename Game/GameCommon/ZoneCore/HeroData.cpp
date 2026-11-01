
//***************************************************************************
// HeroData.cpp : implementation of the CHeroData class.
//
//***************************************************************************

#include "pch.h"
#include "HeroData.h"

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