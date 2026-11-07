
//***************************************************************************
// Enums.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Enum 시트로부터 만듭니다. 다른 헤더에 의존하지 않습니다.
//***************************************************************************

#ifndef GAMEDATAENUMS_GEN_H
#define GAMEDATAENUMS_GEN_H

#include <cstdint>

namespace GameDataEnums
{
	enum class ESkillShape : std::uint8_t
	{
		Single = 0,	// 스킬 효과 범위 형태 - 대상 하나 (대상 ID를 지정)
		Area = 1,	// 지정한 지점을 중심으로 한 원형 범위 (좌표를 지정)
	};

	enum class EStatusKind : std::uint8_t
	{
		Dot = 0,	// 지속 피해: tickMs마다 고정 피해(방어력 무시)를 준다
		Buff = 1,	// 능력치 보정: 지속시간 동안 공격력/방어력/이동속도를 %로 바꾼다 (음수면 디버프)
	};

	enum class EStatusTarget : std::uint8_t
	{
		Hit = 0,	// 스킬에 맞은 대상에게 건다
		Self = 1,	// 시전자 본인에게 건다 (효과가 발동하는 시점에 한 번)
	};
}

#endif // ndef GAMEDATAENUMS_GEN_H
