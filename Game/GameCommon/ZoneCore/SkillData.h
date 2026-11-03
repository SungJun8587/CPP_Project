
//***************************************************************************
// SkillData.h : 스킬 템플릿 데이터 (서버 구동 시 로드되는 불변 데이터)
//
//***************************************************************************

#ifndef SKILLDATA_H
#define SKILLDATA_H

#include <BaseRedefineDataType.h>

#include <cstdint>
#include <string>

#include "../Generated/Enums.gen.h"
#include "../Generated/Consts.gen.h"

//***************************************************************************
// 스킬/상태 효과의 enum과 일부 상한은 기획 데이터(GameData.xlsx의 Enum, Const 시트)에서 XlsxToJson이 생성한다.
// 서버, 클라이언트, 데이터 로더가 같은 정의를 쓰도록 여기서 이름만 가져온다.
//   ESkillShape   스킬 효과 범위 형태 (Single: 대상 하나, Area: 지정 지점 중심 원형 범위)
//   EStatusKind   상태 효과 종류 (Dot: 지속 피해, Buff: 능력치 보정)
//   EStatusTarget 상태 효과 적용 대상 (Hit: 맞은 대상, Self: 시전자)
//***************************************************************************
using GameDataEnums::ESkillShape;
using GameDataEnums::EStatusKind;
using GameDataEnums::EStatusTarget;

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
	uint32			castTimeMs = 0;			// 시전 시간. 0이면 즉시 발동. 시전 중 이동하면 취소되고, 시전 중에는 기본 공격/다른 스킬을 쓸 수 없다
	float			projectileSpeed = 0.0f;	// 투사체 속도(units/sec). 0이면 즉시 적중. 양수면 비행 시간(거리/속도) 뒤 착탄 시점에 판정한다
	uint32			statusId = 0;			// 적중(또는 시전)할 때 거는 상태 효과 ID. 0이면 없음
};

//***************************************************************************
// @brief 상태 효과 데이터. 기획 데이터(엑셀)의 Status 시트 한 행에 대응한다.
// @details 같은 ID의 효과가 이미 걸려 있으면 지속시간을 새로 시작하고 값을 갱신한다(중첩 없음).
//          Dot의 틱 피해는 건 순간의 시전자 공격력으로 정해진다: max(1, 공격력 * damagePercent / 100 + flatDamage).
//          Buff는 percent 합산으로 적용되며 엔티티당 합계는 kMin/kMaxStatPercent로 묶는다.
//***************************************************************************
struct SStatusTemplate
{
	uint32			id = 0;					// 상태 효과 ID (유일, 0 불가)
	std::string		name;					// 이름 (UTF-8, 로그/디버깅용)
	EStatusKind		kind = EStatusKind::Dot;
	EStatusTarget	target = EStatusTarget::Hit;
	uint32			durationMs = 5000;		// 지속시간
	uint32			tickMs = 1000;			// Dot의 피해 간격 (Buff는 0)
	uint32			damagePercent = 0;		// Dot: 공격력에 곱하는 비율(%)
	int32			flatDamage = 0;			// Dot: 더하는 고정 피해
	int32			attackPercent = 0;		// Buff: 공격력 증감(%)
	int32			defensePercent = 0;		// Buff: 방어력 증감(%)
	int32			speedPercent = 0;		// Buff: 이동속도 증감(%)
};

constexpr uint32 kMaxSkillTargets = 32;		// Area 한 번에 맞을 수 있는 수의 절대 상한
constexpr uint32 kMaxHeroSkills = GameDataConsts::kMaxHeroSkills;			// 영웅 한 명이 가질 수 있는 스킬 수 상한 (Const 시트. 클라이언트 슬롯 수와 맞춘다)
constexpr uint32 kMaxSkillDamagePercent = 100000;
constexpr uint32 kMaxCastTimeMs = 10000;			// 시전 시간 상한
constexpr float  kMaxProjectileSpeed = 1000.0f;		// 투사체 속도 상한 (units/sec)
constexpr uint32 kMaxProjectileFlightMs = 5000;		// 투사체 비행 시간 상한. 이보다 오래 걸리는 거리/속도 조합은 데이터 검증이 거부한다
constexpr uint32 kMaxStatusesPerEntity = GameDataConsts::kMaxStatusesPerEntity;	// 엔티티 한 명에게 동시에 걸릴 수 있는 상태 효과 수 (Const 시트)
constexpr uint32 kMaxStatusDurationMs = 600000;		// 상태 효과 지속시간 상한
constexpr uint32 kMinStatusTickMs = 100;			// Dot 틱 간격 하한
constexpr int32  kMinStatPercent = -90;				// 능력치 보정 합계 하한(%). 방어/이동이 음수로 뒤집히지 않게 한다
constexpr int32  kMaxStatPercent = 500;				// 능력치 보정 합계 상한(%)
constexpr int32  kMaxSpeedBuffPercent = 100;		// 이동속도 보정 데이터 상한(%). AOI 시야 검증이 이 값을 전제로 한다

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

//***************************************************************************
// @brief 스킬이 직접 피해를 주는지 확인합니다. 피해가 없는 스킬은 상태 효과만 건다.
//***************************************************************************
inline bool SkillHasDamage(const SSkillTemplate& skill)
{
	return skill.damagePercent != 0 || skill.flatDamage != 0;
}

//***************************************************************************
// @brief Dot 한 틱의 피해량입니다. 방어력은 무시하며 최소 1입니다. 64비트로 계산해 오버플로를 막습니다.
// @param status 상태 효과
// @param attack 건 순간의 시전자 공격력
//***************************************************************************
inline int32 CalcDotDamage(const SStatusTemplate& status, int32 attack)
{
	int64 damage = static_cast<int64>(attack) * status.damagePercent / 100 + status.flatDamage;
	if( damage < 1 )
		damage = 1;
	if( damage > INT32_MAX )
		damage = INT32_MAX;
	return static_cast<int32>(damage);
}

//***************************************************************************
// @brief 능력치에 % 보정을 적용합니다. 결과는 0 이상입니다.
// @param base    기본값 (0 이상)
// @param percent 증감 %. -90이면 10%만 남고 +100이면 두 배
//***************************************************************************
inline int32 ApplyStatPercent(int32 base, int32 percent)
{
	int64 value = static_cast<int64>(base) * (100 + static_cast<int64>(percent)) / 100;
	if( value < 0 )
		value = 0;
	if( value > INT32_MAX )
		value = INT32_MAX;
	return static_cast<int32>(value);
}

#endif // ndef SKILLDATA_H