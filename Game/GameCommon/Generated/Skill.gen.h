
//***************************************************************************
// Skill.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Skill 시트로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SSkillRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_SKILL_GEN_H
#define GAMEDATAROWS_SKILL_GEN_H

#include <initializer_list>
#include <limits>

#include "Enums.gen.h"
#include "EnumStrings.gen.h"

namespace GameDataRows
{
	// Skill.json의 한 행.
	struct SSkillRow
	{
		uint32	id = 0;
		_tstring	name;
		GameDataEnums::ESkillShape	shape = GameDataEnums::ESkillShape::Single;
		uint32	damagePercent = 0;
		int32	flatDamage = 0;
		double	range = 0;
		double	radius = 0;
		uint32	cooldownMs = 0;
		uint32	maxTargets = 0;
		uint32	castTimeMs = 0;
		double	projectileSpeed = 0;
		uint32	statusId = 0;
		GameDataEnums::ESkillTarget	target = GameDataEnums::ESkillTarget::Enemy;
		uint32	healPercent = 0;
		int32	flatHeal = 0;
		double	angle = 0;
		bool	enumOk = true;		// FromJSON에서 enum 이름을 모르는 값을 만나면 false

		static constexpr const TCHAR* kFile = _T("Skill.json");
		static constexpr const TCHAR* kTable = _T("Skill");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Name"), EFieldType::String },
				{ _T("Shape"), EFieldType::String },
				{ _T("DamagePercent"), EFieldType::UInt, 0ll, 4294967295ll },
				{ _T("FlatDamage"), EFieldType::Int, 0ll, 2147483647ll },
				{ _T("Range"), EFieldType::Number },
				{ _T("Radius"), EFieldType::Number },
				{ _T("CooldownMs"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("MaxTargets"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("CastTimeMs"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("ProjectileSpeed"), EFieldType::Number, (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)(), false },
				{ _T("StatusId"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("Target"), EFieldType::String, (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)(), false },
				{ _T("HealPercent"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("FlatHeal"), EFieldType::Int, 0ll, 2147483647ll, false },
				{ _T("Angle"), EFieldType::Number, (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)(), false },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), this->id, allocator);
			value.AddMember(_T("Name"), _tValue(this->name.c_str(), allocator), allocator);
			value.AddMember(_T("Shape"), _tValue(GameDataEnums::ToString(this->shape), allocator), allocator);
			value.AddMember(_T("DamagePercent"), this->damagePercent, allocator);
			value.AddMember(_T("FlatDamage"), this->flatDamage, allocator);
			value.AddMember(_T("Range"), this->range, allocator);
			value.AddMember(_T("Radius"), this->radius, allocator);
			value.AddMember(_T("CooldownMs"), this->cooldownMs, allocator);
			value.AddMember(_T("MaxTargets"), this->maxTargets, allocator);
			value.AddMember(_T("CastTimeMs"), this->castTimeMs, allocator);
			value.AddMember(_T("ProjectileSpeed"), this->projectileSpeed, allocator);
			value.AddMember(_T("StatusId"), this->statusId, allocator);
			value.AddMember(_T("Target"), _tValue(GameDataEnums::ToString(this->target), allocator), allocator);
			value.AddMember(_T("HealPercent"), this->healPercent, allocator);
			value.AddMember(_T("FlatHeal"), this->flatHeal, allocator);
			value.AddMember(_T("Angle"), this->angle, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->id = value[_T("Id")].GetUint();
			this->name = value[_T("Name")].GetString();
			if( !GameDataEnums::TryParse(value[_T("Shape")].GetString(), this->shape) ) this->enumOk = false;
			this->damagePercent = value[_T("DamagePercent")].GetUint();
			this->flatDamage = value[_T("FlatDamage")].GetInt();
			this->range = value[_T("Range")].GetDouble();
			this->radius = value[_T("Radius")].GetDouble();
			this->cooldownMs = value[_T("CooldownMs")].GetUint();
			this->maxTargets = value[_T("MaxTargets")].GetUint();
			if( value.HasMember(_T("CastTimeMs")) ) this->castTimeMs = value[_T("CastTimeMs")].GetUint();
			if( value.HasMember(_T("ProjectileSpeed")) ) this->projectileSpeed = value[_T("ProjectileSpeed")].GetDouble();
			if( value.HasMember(_T("StatusId")) ) this->statusId = value[_T("StatusId")].GetUint();
			if( value.HasMember(_T("Target")) && !GameDataEnums::TryParse(value[_T("Target")].GetString(), this->target) ) this->enumOk = false;
			if( value.HasMember(_T("HealPercent")) ) this->healPercent = value[_T("HealPercent")].GetUint();
			if( value.HasMember(_T("FlatHeal")) ) this->flatHeal = value[_T("FlatHeal")].GetInt();
			if( value.HasMember(_T("Angle")) ) this->angle = value[_T("Angle")].GetDouble();
		}
	};
}

#endif // ndef GAMEDATAROWS_SKILL_GEN_H
