
//***************************************************************************
// Monster.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter이 GameData.xlsx의 Monster 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter을 다시 실행하세요.
// SMonsterRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_MONSTER_GEN_H
#define GAMEDATAROWS_MONSTER_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// Monster.json의 한 행.
	struct SMonsterRow
	{
		uint32	id = 0;
		_tstring	name;
		int32	maxHp = 0;
		int32	attack = 0;
		int32	defense = 0;
		double	attackRange = 0;
		uint32	attackIntervalMs = 0;
		double	speed = 0;
		double	aggroRange = 0;
		double	leashRange = 0;
		uint32	respawnMs = 0;

		static constexpr const TCHAR* kFile = _T("Monster.json");
		static constexpr const TCHAR* kTable = _T("Monster");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("Id"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Name"), EFieldType::String },
				{ _T("MaxHp"), EFieldType::Int, 1ll, 2147483647ll },
				{ _T("Attack"), EFieldType::Int, 0ll, 2147483647ll },
				{ _T("Defense"), EFieldType::Int, 0ll, 2147483647ll },
				{ _T("AttackRange"), EFieldType::Number },
				{ _T("AttackIntervalMs"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Speed"), EFieldType::Number },
				{ _T("AggroRange"), EFieldType::Number },
				{ _T("LeashRange"), EFieldType::Number },
				{ _T("RespawnMs"), EFieldType::UInt, 0ll, 4294967295ll },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("Id"), this->id, allocator);
			value.AddMember(_T("Name"), _tValue(this->name.c_str(), allocator), allocator);
			value.AddMember(_T("MaxHp"), this->maxHp, allocator);
			value.AddMember(_T("Attack"), this->attack, allocator);
			value.AddMember(_T("Defense"), this->defense, allocator);
			value.AddMember(_T("AttackRange"), this->attackRange, allocator);
			value.AddMember(_T("AttackIntervalMs"), this->attackIntervalMs, allocator);
			value.AddMember(_T("Speed"), this->speed, allocator);
			value.AddMember(_T("AggroRange"), this->aggroRange, allocator);
			value.AddMember(_T("LeashRange"), this->leashRange, allocator);
			value.AddMember(_T("RespawnMs"), this->respawnMs, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->id = value[_T("Id")].GetUint();
			this->name = value[_T("Name")].GetString();
			this->maxHp = value[_T("MaxHp")].GetInt();
			this->attack = value[_T("Attack")].GetInt();
			this->defense = value[_T("Defense")].GetInt();
			this->attackRange = value[_T("AttackRange")].GetDouble();
			this->attackIntervalMs = value[_T("AttackIntervalMs")].GetUint();
			this->speed = value[_T("Speed")].GetDouble();
			this->aggroRange = value[_T("AggroRange")].GetDouble();
			this->leashRange = value[_T("LeashRange")].GetDouble();
			this->respawnMs = value[_T("RespawnMs")].GetUint();
		}
	};
}

#endif // ndef GAMEDATAROWS_MONSTER_GEN_H
