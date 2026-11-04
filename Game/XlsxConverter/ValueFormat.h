
//***************************************************************************
// ValueFormat.h : 값 표기 규칙 (Python 도구와 동일한 출력을 내기 위한 문자열 변환)
//
//***************************************************************************

#ifndef UC_VALUEFORMAT_H
#define UC_VALUEFORMAT_H

#include <Excel/XlntDefine.h>

#include <string>

namespace ValueFormat
{
	std::string FloatRepr(double value);
	std::string CellToString(const Xlnt::SCell& cell);
	std::string Quote(const std::string& text);
	std::string JsonString(const std::string& text);
	std::string Trim(const std::string& text);
	std::string Lower(std::string text);
}

#endif // ndef UC_VALUEFORMAT_H