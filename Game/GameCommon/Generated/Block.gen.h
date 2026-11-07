
//***************************************************************************
// Block.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Block 시트로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SBlockRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_BLOCK_GEN_H
#define GAMEDATAROWS_BLOCK_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// Block.json의 한 행.
	struct SBlockRow
	{
		uint32	zoneId = 0;
		_tstring	type;
		double	x = 0;
		double	y = 0;
		double	w = 0;
		double	h = 0;
		double	r = 0;

		static constexpr const TCHAR* kFile = _T("Block.json");
		static constexpr const TCHAR* kTable = _T("Block");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("ZoneId"), EFieldType::UInt, 0ll, 4294967295ll },
				{ _T("Type"), EFieldType::String },
				{ _T("X"), EFieldType::Number },
				{ _T("Y"), EFieldType::Number },
				{ _T("W"), EFieldType::Number },
				{ _T("H"), EFieldType::Number },
				{ _T("R"), EFieldType::Number },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("ZoneId"), this->zoneId, allocator);
			value.AddMember(_T("Type"), _tValue(this->type.c_str(), allocator), allocator);
			value.AddMember(_T("X"), this->x, allocator);
			value.AddMember(_T("Y"), this->y, allocator);
			value.AddMember(_T("W"), this->w, allocator);
			value.AddMember(_T("H"), this->h, allocator);
			value.AddMember(_T("R"), this->r, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->zoneId = value[_T("ZoneId")].GetUint();
			this->type = value[_T("Type")].GetString();
			this->x = value[_T("X")].GetDouble();
			this->y = value[_T("Y")].GetDouble();
			this->w = value[_T("W")].GetDouble();
			this->h = value[_T("H")].GetDouble();
			this->r = value[_T("R")].GetDouble();
		}
	};
}

#endif // ndef GAMEDATAROWS_BLOCK_GEN_H
