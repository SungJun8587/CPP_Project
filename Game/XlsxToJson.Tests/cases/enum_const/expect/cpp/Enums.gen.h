
//***************************************************************************
// Enums.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxToJson이 GameData.xlsx의 Enum 시트로부터 만듭니다. 다른 헤더에 의존하지 않습니다.
//***************************************************************************

#ifndef GAMEDATAENUMS_GEN_H
#define GAMEDATAENUMS_GEN_H

#include <cstdint>

namespace GameDataEnums
{
	enum class EFlag : std::uint32_t
	{
		None = 0,	// 없음
		A = 1,
		B = 2,
		High = 2147483648,	// 2^31 이상 값
	};

	enum class EColor : std::uint8_t
	{
		Red = 0,	// 첫 값은 0
		Green = 1,	// 앞 값 + 1
		Blue = 10,
		Alpha = 11,	// 11
	};

	enum class ESigned : std::int32_t
	{
		Neg = -5,
		Zero = 0,
	};
}

#endif // ndef GAMEDATAENUMS_GEN_H
