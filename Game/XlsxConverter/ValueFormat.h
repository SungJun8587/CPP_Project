
//***************************************************************************
// ValueFormat.h : 값 표기 규칙
//
//***************************************************************************

#ifndef VALUEFORMAT_H
#define VALUEFORMAT_H

#include <Excel/XlntDefine.h>
#include <cstdint>
#include <string>

namespace ValueFormat
{
	std::string FloatRepr(double value);
	std::string CellToString(const Xlnt::SCell& cell);
	std::string Quote(const std::string& text);
	std::string JsonString(const std::string& text);
	// 앞뒤 공백을 지운다. 엑셀에서 복사해 온 NBSP(U+00A0), 전각 공백(U+3000), BOM(U+FEFF)도 공백으로 본다.
	std::string Trim(const std::string& text);
	std::string Lower(std::string text);

	// 셀을 유한한 숫자(double)로 읽는다. Num 셀은 그대로, Str 셀은 앞뒤 공백을 지운 십진/지수 표기만 받는다. (0x.., 16진 실수, inf/nan 거부)
	// 실패하면 why에 '숫자가 아님: ..' 같은 사유를 채운다. 빈 칸 검사는 호출자 몫이다.
	bool ParseNumber(const Xlnt::SCell& cell, double& value, std::string& why);

	// 셀을 정수(|값| <= 2^53)로 읽는다. 실패하면 why에 사유를 채운다. (Enum, Const, 패킷 정의 시트가 쓴다)
	bool ParseInt64(const Xlnt::SCell& cell, int64_t& value, std::string& why);
}

#endif // ndef VALUEFORMAT_H