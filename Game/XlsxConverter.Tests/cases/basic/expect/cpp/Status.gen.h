
//***************************************************************************
// Status.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Status 시트로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SStatusRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_STATUS_GEN_H
#define GAMEDATAROWS_STATUS_GEN_H

#include <initializer_list>
#include <limits>

#include "Enums.gen.h"
#include "EnumStrings.gen.h"

namespace GameDataRows
{
	// Status.json의 한 행.
	struct SStatusRow
	{
		uint32	id = 0;
		_tstring	name;
		GameDataEnums::EStatusKind	kind = GameDataEnums::EStatusKind::Dot;
		GameDataEnums::EStatusTarget	target = GameDataEnums::EStatusTarget::Hit;
		uint32	durationMs = 0;
		uint32	tickMs = 0;
		uint32	damagePercent = 0;
		int32	flatDamage = 0;
		int32	attackPercent = 0;
		int32	defensePercent = 0;
		int32	speedPercent = 0;
		bool	enumOk = true;		// FromJSON에서 enum 이름을 모르는 값을 만나면 false

		static constexpr const TCHAR* kFile = _T("Status.json");
		static constexpr const TCHAR* kTable = _T("Status");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Name"), EFieldType::String },
				{ _T("Kind"), EFieldType::String },
				{ _T("Target"), EFieldType::String },
				{ _T("DurationMs"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("TickMs"), EFieldType::UInt, 0ll, 4294967295ll },
				{ _T("DamagePercent"), EFieldType::UInt, 0ll, 4294967295ll },
				{ _T("FlatDamage"), EFieldType::Int, 0ll, 2147483647ll },
				{ _T("AttackPercent"), EFieldType::Int, -1000ll, 1000ll },
				{ _T("DefensePercent"), EFieldType::Int, -1000ll, 1000ll },
				{ _T("SpeedPercent"), EFieldType::Int, -1000ll, 1000ll },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), this->id, allocator);
			value.AddMember(_T("Name"), _tValue(this->name.c_str(), allocator), allocator);
			value.AddMember(_T("Kind"), _tValue(GameDataEnums::ToString(this->kind), allocator), allocator);
			value.AddMember(_T("Target"), _tValue(GameDataEnums::ToString(this->target), allocator), allocator);
			value.AddMember(_T("DurationMs"), this->durationMs, allocator);
			value.AddMember(_T("TickMs"), this->tickMs, allocator);
			value.AddMember(_T("DamagePercent"), this->damagePercent, allocator);
			value.AddMember(_T("FlatDamage"), this->flatDamage, allocator);
			value.AddMember(_T("AttackPercent"), this->attackPercent, allocator);
			value.AddMember(_T("DefensePercent"), this->defensePercent, allocator);
			value.AddMember(_T("SpeedPercent"), this->speedPercent, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->id = value[_T("Id")].GetUint();
			this->name = value[_T("Name")].GetString();
			if( !GameDataEnums::TryParse(value[_T("Kind")].GetString(), this->kind) ) this->enumOk = false;
			if( !GameDataEnums::TryParse(value[_T("Target")].GetString(), this->target) ) this->enumOk = false;
			this->durationMs = value[_T("DurationMs")].GetUint();
			this->tickMs = value[_T("TickMs")].GetUint();
			this->damagePercent = value[_T("DamagePercent")].GetUint();
			this->flatDamage = value[_T("FlatDamage")].GetInt();
			this->attackPercent = value[_T("AttackPercent")].GetInt();
			this->defensePercent = value[_T("DefensePercent")].GetInt();
			this->speedPercent = value[_T("SpeedPercent")].GetInt();
		}
	};
}

#endif // ndef GAMEDATAROWS_STATUS_GEN_H
