
//***************************************************************************
// Item.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter이 GameData.xlsx의 Item 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter을 다시 실행하세요.
// SItemRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_ITEM_GEN_H
#define GAMEDATAROWS_ITEM_GEN_H

#include <initializer_list>
#include <limits>

#include "Enums.gen.h"
#include "EnumStrings.gen.h"

namespace GameDataRows
{
	// Item.json의 한 행.
	struct SItemRow
	{
		uint32	id = 0;
		_tstring	name;
		GameDataEnums::EColor	color = GameDataEnums::EColor::Red;
		GameDataEnums::EFlag	flag = GameDataEnums::EFlag::None;
		double	weight = 0;
		bool	active = false;
		int32	delta = 0;
		bool	enumOk = true;		// FromJSON에서 enum 이름을 모르는 값을 만나면 false

		static constexpr const TCHAR* kFile = _T("Item.json");
		static constexpr const TCHAR* kTable = _T("Item");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Name"), EFieldType::String },
				{ _T("Color"), EFieldType::String },
				{ _T("Flag"), EFieldType::String, (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)(), false },
				{ _T("Weight"), EFieldType::Number },
				{ _T("Active"), EFieldType::Bool },
				{ _T("Delta"), EFieldType::Int, -100ll, 100ll, false },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), this->id, allocator);
			value.AddMember(_T("Name"), _tValue(this->name.c_str(), allocator), allocator);
			value.AddMember(_T("Color"), _tValue(GameDataEnums::ToString(this->color), allocator), allocator);
			value.AddMember(_T("Flag"), _tValue(GameDataEnums::ToString(this->flag), allocator), allocator);
			value.AddMember(_T("Weight"), this->weight, allocator);
			value.AddMember(_T("Active"), this->active, allocator);
			value.AddMember(_T("Delta"), this->delta, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->id = value[_T("Id")].GetUint();
			this->name = value[_T("Name")].GetString();
			if( !GameDataEnums::TryParse(value[_T("Color")].GetString(), this->color) ) this->enumOk = false;
			if( value.HasMember(_T("Flag")) && !GameDataEnums::TryParse(value[_T("Flag")].GetString(), this->flag) ) this->enumOk = false;
			this->weight = value[_T("Weight")].GetDouble();
			this->active = value[_T("Active")].GetBool();
			if( value.HasMember(_T("Delta")) ) this->delta = value[_T("Delta")].GetInt();
		}
	};
}

#endif // ndef GAMEDATAROWS_ITEM_GEN_H
