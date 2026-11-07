
//***************************************************************************
// Consts.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Const 시트로부터 만듭니다. 문자열 상수가 있으면 TCHAR, _T가 필요하다. (pch가 제공)
//***************************************************************************

#ifndef GAMEDATACONSTS_GEN_H
#define GAMEDATACONSTS_GEN_H

#include <cstdint>

namespace GameDataConsts
{
	constexpr std::uint32_t kMaxUsers = 1000;	// 최대 접속자
	constexpr double kGravity = 9.81;
	constexpr const TCHAR* kTitle = _T("따옴표 \" 와 백슬래시 \\ 와 한글");
	constexpr bool kEnabled = true;
	constexpr std::int32_t kOffset = -5;
	constexpr std::uint32_t kBig = 4000000000;
}

#endif // ndef GAMEDATACONSTS_GEN_H
