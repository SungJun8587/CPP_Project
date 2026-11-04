
//***************************************************************************
// Spawn.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData.xlsx의 Spawn 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxConverter를 다시 실행하세요.
// SSpawnRow는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.
//***************************************************************************

#ifndef GAMEDATAROWS_SPAWN_GEN_H
#define GAMEDATAROWS_SPAWN_GEN_H

#include <initializer_list>
#include <limits>

namespace GameDataRows
{
	// Spawn.json의 한 행.
	struct SSpawnRow
	{
		uint32	zoneId = 0;
		uint32	monsterId = 0;
		uint32	count = 0;
		double	x = 0;
		double	y = 0;
		double	radius = 0;

		static constexpr const TCHAR* kFile = _T("Spawn.json");
		static constexpr const TCHAR* kTable = _T("Spawn");

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("ZoneId"), EFieldType::UInt, 0ll, 4294967295ll },
				{ _T("MonsterId"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("Count"), EFieldType::UInt, 1ll, 4294967295ll },
				{ _T("X"), EFieldType::Number },
				{ _T("Y"), EFieldType::Number },
				{ _T("Radius"), EFieldType::Number },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("ZoneId"), this->zoneId, allocator);
			value.AddMember(_T("MonsterId"), this->monsterId, allocator);
			value.AddMember(_T("Count"), this->count, allocator);
			value.AddMember(_T("X"), this->x, allocator);
			value.AddMember(_T("Y"), this->y, allocator);
			value.AddMember(_T("Radius"), this->radius, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			this->zoneId = value[_T("ZoneId")].GetUint();
			this->monsterId = value[_T("MonsterId")].GetUint();
			this->count = value[_T("Count")].GetUint();
			this->x = value[_T("X")].GetDouble();
			this->y = value[_T("Y")].GetDouble();
			this->radius = value[_T("Radius")].GetDouble();
		}
	};
}

#endif // ndef GAMEDATAROWS_SPAWN_GEN_H
