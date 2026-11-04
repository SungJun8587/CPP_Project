
//***************************************************************************
// BossAction.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData.xlsx의 BossAction 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SBossActionRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_BOSSACTION_GEN_H
#define GAMEDATAROWS_BOSSACTION_GEN_H

#include <initializer_list>
#include <limits>

#include "Enums.gen.h"
#include "EnumStrings.gen.h"

namespace GameDataRows
{
	// BossAction.json의 한 행.
	struct SBossActionRow
	{
		uint32	bossId = 0;
		uint32	phase = 0;
		uint32	seq = 0;
		GameDataEnums::EBossTrigger	trigger = GameDataEnums::EBossTrigger::Loop;
		GameDataEnums::EBossActionType	type = GameDataEnums::EBossActionType::Cast;
		GameDataEnums::EBossTarget	target = GameDataEnums::EBossTarget::Current;
		uint32	skillId = 0;
		uint32	monsterId = 0;
		uint32	count = 0;
		double	spreadRadius = 0;
		uint32	delayMs = 0;
		bool	enumOk = true;		// FromJSON에서 enum 이름을 모르는 값을 만나면 false

		static constexpr const TCHAR* kFile = _T("BossAction.json");
		static constexpr const TCHAR* kTable = _T("BossAction");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("BossId"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Phase"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Seq"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Trigger"), EFieldType::String },
				{ _T("Type"), EFieldType::String },
				{ _T("Target"), EFieldType::String },
				{ _T("SkillId"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("MonsterId"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("Count"), EFieldType::UInt, 0ll, 4294967295ll, false },
				{ _T("SpreadRadius"), EFieldType::Number, (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)(), false },
				{ _T("DelayMs"), EFieldType::UInt, 0ll, 4294967295ll },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("BossId"), this->bossId, allocator);
			value.AddMember(_T("Phase"), this->phase, allocator);
			value.AddMember(_T("Seq"), this->seq, allocator);
			value.AddMember(_T("Trigger"), _tValue(GameDataEnums::ToString(this->trigger), allocator), allocator);
			value.AddMember(_T("Type"), _tValue(GameDataEnums::ToString(this->type), allocator), allocator);
			value.AddMember(_T("Target"), _tValue(GameDataEnums::ToString(this->target), allocator), allocator);
			value.AddMember(_T("SkillId"), this->skillId, allocator);
			value.AddMember(_T("MonsterId"), this->monsterId, allocator);
			value.AddMember(_T("Count"), this->count, allocator);
			value.AddMember(_T("SpreadRadius"), this->spreadRadius, allocator);
			value.AddMember(_T("DelayMs"), this->delayMs, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->bossId = value[_T("BossId")].GetUint();
			this->phase = value[_T("Phase")].GetUint();
			this->seq = value[_T("Seq")].GetUint();
			if( !GameDataEnums::TryParse(value[_T("Trigger")].GetString(), this->trigger) ) this->enumOk = false;
			if( !GameDataEnums::TryParse(value[_T("Type")].GetString(), this->type) ) this->enumOk = false;
			if( !GameDataEnums::TryParse(value[_T("Target")].GetString(), this->target) ) this->enumOk = false;
			if( value.HasMember(_T("SkillId")) ) this->skillId = value[_T("SkillId")].GetUint();
			if( value.HasMember(_T("MonsterId")) ) this->monsterId = value[_T("MonsterId")].GetUint();
			if( value.HasMember(_T("Count")) ) this->count = value[_T("Count")].GetUint();
			if( value.HasMember(_T("SpreadRadius")) ) this->spreadRadius = value[_T("SpreadRadius")].GetDouble();
			this->delayMs = value[_T("DelayMs")].GetUint();
		}
	};
}

#endif // ndef GAMEDATAROWS_BOSSACTION_GEN_H
