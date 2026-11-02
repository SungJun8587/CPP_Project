
//***************************************************************************
// SkillData.h : 스킬 템플릿 데이터 (서버 구동 시 로드되는 불변 데이터)
//
//***************************************************************************

#ifndef SKILLDATA_H
#define SKILLDATA_H

#include <BaseRedefineDataType.h>

#include <cstdint>
#include <string>

//***************************************************************************
// @brief 스킬 효과 범위 형태.
//***************************************************************************
enum class ESkillShape : uint8
{
	Single = 0,		// 대상 하나 (대상 ID를 지정)
	Area = 1,		// 지정한 지점을 중심으로 한 원형 범위 (좌표를 지정)
};

//***************************************************************************
// @brief 스킬 한 종류의 데이터. 기획 데이터(엑셀)의 Skill 시트 한 행에 대응한다.
// @details 즉시 발동 스킬이다. 시전 시간, 투사체, 지속 효과는 없다.
//          피해량 = max(minDamage, 공격력 * damagePercent / 100 + flatDamage - 방어력)
//          Area는 중심이 시전자로부터 range 안이어야 하고, 중심에서 radius 안의 공격 가능한 엔티티가 maxTargets까지 맞는다.
//          시전 가능한 최대 거리는 range + radius이므로 AOI 보장 시야 안에 들어와야 한다(CBattleZone::ValidateMonsters가 검사).
//***************************************************************************
struct SSkillTemplate
{
	uint32			id = 0;					// 스킬 ID (유일, 0 불가)
	std::string		name;					// 이름 (UTF-8, 로그/디버깅용)
	ESkillShape		shape = ESkillShape::Single;
	uint32			damagePercent = 100;	// 공격력에 곱하는 비율(%)
	int32			flatDamage = 0;			// 더하는 고정 피해
	float			range = 3.0f;			// 시전 거리 (시전자 -> 대상/중심)
	float			radius = 0.0f;			// Area의 반경 (Single은 0)
	uint32			cooldownMs = 1000;		// 재사용 대기 시간
	uint32			maxTargets = 1;			// 한 번에 맞는 최대 수 (Single은 1)
};

constexpr uint32 kMaxSkillTargets = 32;		// Area 한 번에 맞을 수 있는 수의 절대 상한
constexpr uint32 kMaxHeroSkills = 8;		// 영웅 한 명이 가질 수 있는 스킬 수 상한 (클라이언트 슬롯 수와 맞춘다)
constexpr uint32 kMaxSkillDamagePercent = 100000;

//***************************************************************************
// @brief 스킬 피해량 공식입니다. 64비트로 계산해 오버플로를 막습니다.
// @param skill     스킬
// @param attack    시전자 공격력
// @param defense   피해자 방어력
// @param minDamage 최소 피해
// @return 피해량 (minDamage 이상, INT32_MAX 이하)
//***************************************************************************
inline int32 CalcSkillDamage(const SSkillTemplate& skill, int32 attack, int32 defense, int32 minDamage)
{
	int64 damage = static_cast<int64>(attack) * skill.damagePercent / 100 + skill.flatDamage - defense;
	if( damage < minDamage )
		damage = minDamage;
	if( damage > INT32_MAX )
		damage = INT32_MAX;
	return static_cast<int32>(damage);
}

#endif // ndef SKILLDATA_H