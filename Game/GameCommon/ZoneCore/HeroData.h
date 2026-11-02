
//***************************************************************************
// HeroData.h : 영웅(플레이어 캐릭터 종류) 템플릿 데이터 (서버 구동 시 로드되는 불변 데이터)
//
//***************************************************************************

#ifndef HERODATA_H
#define HERODATA_H

#include <BaseRedefineDataType.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "SkillData.h"
#include "ZoneEntity.h"

//***************************************************************************
// @struct SHeroTemplate
// @brief 영웅 종류별 데이터. 기획 데이터(엑셀)의 Hero 시트 한 행에 대응한다.
// @details 플레이어가 입장할 때 선택한 영웅의 전투 능력치와 이동 속도가 그 플레이어 엔티티에 적용된다.
//          값은 서버 데이터만 쓰며 클라이언트가 보낸 능력치는 신뢰하지 않는다.
//***************************************************************************
struct SHeroTemplate
{
	uint32			id = 0;						// 영웅 ID (유일, 0 불가)
	std::string		name;						// 이름 (UTF-8, 로그/디버깅용)
	SCombatStats	stats;						// 전투 능력치
	float			speed = 6.0f;				// 이동 속도 (units/sec)
	std::vector<uint32>	skills;					// 사용할 수 있는 스킬 ID (최대 kMaxHeroSkills). CHeroData::AssignSkill로 채운다
};

//***************************************************************************
// @class CHeroData
// @brief 영웅 템플릿 모음. 로드가 끝나면 변경하지 않으며, shared_ptr<const>로 존들이 공유한다.
// @details 영웅은 1개 이상이어야 한다. 선택 정보가 없는 입장(heroId 0)은 ID가 가장 작은 영웅(기본 영웅)으로 처리한다.
//          Validate()는 값 범위를 검사하며, 통과해야 서버가 구동된다.
//          존의 AOI 시야에 의존하는 검사(사거리 등)는 CBattleZone::ValidateMonsters()가 영웅마다 수행한다.
//***************************************************************************
class CHeroData
{
public:
	bool AddHero(const SHeroTemplate& hero);
	bool AddSkill(const SSkillTemplate& skill);
	bool AssignSkill(uint32 heroId, uint32 skillId);
	bool Validate(std::string& error) const;

	static std::shared_ptr<const CHeroData> MakeDefault();

	//***************************************************************************
	// @brief 영웅을 ID로 찾습니다.
	// @param id 영웅 ID
	// @return 영웅, 없으면 nullptr
	//***************************************************************************
	const SHeroTemplate* FindHero(uint32 id) const
	{
		auto it = _heroes.find(id);
		return it != _heroes.end() ? &it->second : nullptr;
	}

	//***************************************************************************
	// @brief 영웅을 선택하지 않은 입장에 쓰는 기본 영웅(ID가 가장 작은 영웅)을 반환합니다.
	// @return 기본 영웅, 영웅이 하나도 없으면 nullptr
	//***************************************************************************
	//***************************************************************************
	// @brief 스킬을 ID로 찾습니다.
	// @return 스킬, 없으면 nullptr
	//***************************************************************************
	const SSkillTemplate* FindSkill(uint32 id) const
	{
		auto it = _skills.find(id);
		return it != _skills.end() ? &it->second : nullptr;
	}

	//***************************************************************************
	// @brief 영웅이 그 스킬을 쓸 수 있는지 확인하고 스킬을 돌려줍니다.
	// @return 스킬, 영웅이 가진 스킬이 아니면 nullptr
	//***************************************************************************
	const SSkillTemplate* FindHeroSkill(const SHeroTemplate& hero, uint32 skillId) const
	{
		for( const uint32 id : hero.skills )
			if( id == skillId )
				return FindSkill(id);
		return nullptr;
	}

	size_t GetSkillCount() const { return _skills.size(); }
	const std::map<uint32, SSkillTemplate>& GetSkills() const { return _skills; }

	const SHeroTemplate* GetDefaultHero() const { return _heroes.empty() ? nullptr : &_heroes.begin()->second; }

	//***************************************************************************
	// @brief heroId로 영웅을 고릅니다. 0이면 기본 영웅입니다.
	// @param heroId 영웅 ID (0이면 기본 영웅)
	// @return 영웅, 없는 ID이면 nullptr
	//***************************************************************************
	const SHeroTemplate* Resolve(uint32 heroId) const { return heroId == 0 ? GetDefaultHero() : FindHero(heroId); }

	//***************************************************************************
	// @brief 전체 영웅을 ID 오름차순으로 반환합니다.
	//***************************************************************************
	const std::map<uint32, SHeroTemplate>& GetHeroes() const { return _heroes; }

	//***************************************************************************
	// @brief 등록된 영웅 수를 반환합니다.
	//***************************************************************************
	size_t GetHeroCount() const { return _heroes.size(); }

	//***************************************************************************
	// @brief 영웅들 중 가장 빠른 이동 속도를 반환합니다. 몬스터 쪽 사거리 검증의 상대 속도로 쓴다.
	// @return 최고 속도 (units/sec), 영웅이 없으면 0
	//***************************************************************************
	float GetMaxSpeed() const
	{
		float maxSpeed = 0.0f;
		for( const auto& pair : _heroes )
			if( pair.second.speed > maxSpeed )
				maxSpeed = pair.second.speed;
		return maxSpeed;
	}

private:
	std::map<uint32, SSkillTemplate>	_skills;	// 스킬 ID -> 스킬
	std::map<uint32, SHeroTemplate>	_heroes;	// 영웅 ID -> 영웅 (정렬되어 있어 첫 원소가 기본 영웅)
};

#endif // ndef HERODATA_H