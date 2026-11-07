
//***************************************************************************
// HeroSkill.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 HeroSkill 시트로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SHeroSkillRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_HEROSKILL_GEN_H
#define GAMEDATAROWS_HEROSKILL_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// HeroSkill.json의 한 행.
	struct SHeroSkillRow
	{
		uint32	heroId = 0;
		uint32	skillId = 0;

		static constexpr const TCHAR* kFile = _T("HeroSkill.json");
		static constexpr const TCHAR* kTable = _T("HeroSkill");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("HeroId"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("SkillId"), EFieldType::UInt, 1ll, 4294967295ll },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("HeroId"), this->heroId, allocator);
			value.AddMember(_T("SkillId"), this->skillId, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->heroId = value[_T("HeroId")].GetUint();
			this->skillId = value[_T("SkillId")].GetUint();
		}
	};
}

#endif // ndef GAMEDATAROWS_HEROSKILL_GEN_H
