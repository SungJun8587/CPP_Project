
//***************************************************************************
// Boss.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Boss 시트로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SBossRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_BOSS_GEN_H
#define GAMEDATAROWS_BOSS_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// Boss.json의 한 행.
	struct SBossRow
	{
		uint32	id = 0;
		uint32	monsterId = 0;
		_tstring	name;
		uint32	enrageAfterMs = 0;
		uint32	enrageStatusId = 0;

		static constexpr const TCHAR* kFile = _T("Boss.json");
		static constexpr const TCHAR* kTable = _T("Boss");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("MonsterId"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Name"), EFieldType::String },
				{ _T("EnrageAfterMs"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("EnrageStatusId"), EFieldType::UInt, 0ll, 4294967295ll, false },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), this->id, allocator);
			value.AddMember(_T("MonsterId"), this->monsterId, allocator);
			value.AddMember(_T("Name"), _tValue(this->name.c_str(), allocator), allocator);
			value.AddMember(_T("EnrageAfterMs"), this->enrageAfterMs, allocator);
			value.AddMember(_T("EnrageStatusId"), this->enrageStatusId, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->id = value[_T("Id")].GetUint();
			this->monsterId = value[_T("MonsterId")].GetUint();
			this->name = value[_T("Name")].GetString();
			if( value.HasMember(_T("EnrageAfterMs")) ) this->enrageAfterMs = value[_T("EnrageAfterMs")].GetUint();
			if( value.HasMember(_T("EnrageStatusId")) ) this->enrageStatusId = value[_T("EnrageStatusId")].GetUint();
		}
	};
}

#endif // ndef GAMEDATAROWS_BOSS_GEN_H
