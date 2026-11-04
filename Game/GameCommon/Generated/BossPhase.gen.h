
//***************************************************************************
// BossPhase.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData.xlsx의 BossPhase 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SBossPhaseRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_BOSSPHASE_GEN_H
#define GAMEDATAROWS_BOSSPHASE_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// BossPhase.json의 한 행.
	struct SBossPhaseRow
	{
		uint32	bossId = 0;
		uint32	phase = 0;
		uint32	hpBelowPercent = 0;
		uint32	enterStatusId = 0;
		uint32	enterHealPercent = 0;
		uint32	transitionMs = 0;

		static constexpr const TCHAR* kFile = _T("BossPhase.json");
		static constexpr const TCHAR* kTable = _T("BossPhase");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("BossId"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Phase"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("HpBelowPercent"), EFieldType::UInt, 1ll, 100ll },
				{ _T("EnterStatusId"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("EnterHealPercent"), EFieldType::UInt, 0ll, 100ll, false },
				{ _T("TransitionMs"), EFieldType::UInt, 0ll, 4294967295ll, false },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("BossId"), this->bossId, allocator);
			value.AddMember(_T("Phase"), this->phase, allocator);
			value.AddMember(_T("HpBelowPercent"), this->hpBelowPercent, allocator);
			value.AddMember(_T("EnterStatusId"), this->enterStatusId, allocator);
			value.AddMember(_T("EnterHealPercent"), this->enterHealPercent, allocator);
			value.AddMember(_T("TransitionMs"), this->transitionMs, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->bossId = value[_T("BossId")].GetUint();
			this->phase = value[_T("Phase")].GetUint();
			this->hpBelowPercent = value[_T("HpBelowPercent")].GetUint();
			if( value.HasMember(_T("EnterStatusId")) ) this->enterStatusId = value[_T("EnterStatusId")].GetUint();
			if( value.HasMember(_T("EnterHealPercent")) ) this->enterHealPercent = value[_T("EnterHealPercent")].GetUint();
			if( value.HasMember(_T("TransitionMs")) ) this->transitionMs = value[_T("TransitionMs")].GetUint();
		}
	};
}

#endif // ndef GAMEDATAROWS_BOSSPHASE_GEN_H
