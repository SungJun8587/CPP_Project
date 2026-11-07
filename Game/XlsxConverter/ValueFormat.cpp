
//***************************************************************************
// ValueFormat.cpp : 값 표기 규칙 구현
//
//***************************************************************************

#include "pch.h"
#include "ValueFormat.h"
#include "Utf8Literal.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using Xlnt::SCell;
using Xlnt::TRow;
using Xlnt::TRows;

namespace
{
	//***************************************************************************
	// @brief s[pos..)가 공백(ASCII 공백, NBSP, 전각 공백, BOM)으로 시작하면 그 바이트 수, 아니면 0을 돌려줍니다.
	// @param cp [out] 공백이 ASCII가 아니면 유니코드 값 (오류 메시지에서 눈에 보이게 쓴다)
	//***************************************************************************
	size_t BlankLenAt(const std::string& s, size_t pos, unsigned* cp = nullptr)
	{
		if( pos >= s.size() ) return 0;
		const unsigned char c = static_cast<unsigned char>(s[pos]);
		if( AsciiChar::IsSpace(c) ) return 1;
		const auto at = [&](size_t i) { return pos + i < s.size() ? static_cast<unsigned char>(s[pos + i]) : 0u; };
		if( c == 0xC2 && at(1) == 0xA0 ) { if( cp ) *cp = 0x00A0; return 2; }
		if( c == 0xE3 && at(1) == 0x80 && at(2) == 0x80 ) { if( cp ) *cp = 0x3000; return 3; }
		if( c == 0xEF && at(1) == 0xBB && at(2) == 0xBF ) { if( cp ) *cp = 0xFEFF; return 3; }
		return 0;
	}

	// s[..end)가 공백으로 끝나면 그 바이트 수, 아니면 0. (begin 앞은 보지 않는다)
	size_t BlankLenBefore(const std::string& s, size_t begin, size_t end)
	{
		if( end <= begin ) return 0;
		if( AsciiChar::IsSpace(static_cast<unsigned char>(s[end - 1])) ) return 1;
		for( size_t n = 2; n <= 3 && end - begin >= n; ++n )
			if( BlankLenAt(s, end - n) == n ) return n;
		return 0;
	}

	//***************************************************************************
	// @brief 십진/지수 표기를 로캘과 무관하게 읽습니다. (strtod는 로캘의 소수점 기호를 따르고 16진수와 앞쪽 공백도 받아서 쓰지 않는다)
	// @details '+' 부호는 허용한다. 범위를 벗어나면 ±무한대(너무 큼) 또는 0(너무 작음)을 돌려주고 true이므로 호출자가 유한성을 따로 본다.
	// @return 문자열 전체가 숫자 표기이면 true. "0x10", "5abc", ""는 false. inf/nan은 읽히므로 호출자가 거른다
	//***************************************************************************
	bool ParseDouble(const std::string& t, double& value)
	{
		const char* b = t.data();
		const char* e = b + t.size();
		if( b != e && *b == '+' ) ++b;
		if( b == e || *b == '+' || (b != t.data() && *b == '-') ) return false;		// 부호는 하나만 ("+-5", "++5" 거부)
		const auto r = std::from_chars(b, e, value);
		if( r.ec == std::errc::result_out_of_range && r.ptr == e )
		{
			const bool neg = *b == '-';
			const size_t x = t.find_first_of("eE");
			const bool tiny = x != std::string::npos && x + 1 < t.size() && t[x + 1] == '-';
			value = tiny ? (neg ? -0.0 : 0.0) : (neg ? -HUGE_VAL : HUGE_VAL);
			return true;
		}
		return r.ec == std::errc() && r.ptr == e;
	}
}

namespace ValueFormat
{
	//***************************************************************************
	// @brief double을 Python repr(float)과 같은 최단 왕복 표기로 바꿉니다. (예: 5.0, 0.1, 1e-07, 1.5e+20)
	// @param value 변환할 값 (유한한 수)
	// @return 표기 문자열. 정수 값도 ".0"을 붙여 실수임을 드러낸다
	//***************************************************************************
	std::string FloatRepr(double v)
	{
		if( v == 0.0 ) return std::signbit(v) ? "-0.0" : "0.0";
		char buf[64];
		const auto r = std::to_chars(buf, buf + sizeof(buf), std::fabs(v), std::chars_format::scientific);
		const std::string sci(buf, r.ptr);						// d.ddde+XX (최단 왕복 표현)
		const size_t e = sci.find('e');
		std::string digits;
		for( size_t i = 0; i < e; ++i ) if( sci[i] != '.' ) digits += sci[i];
		const int exp10 = std::atoi(sci.c_str() + e + 1);

		std::string out = v < 0 ? "-" : "";
		if( exp10 >= -4 && exp10 < 16 )
		{
			if( exp10 >= 0 )
			{
				if( static_cast<int>(digits.size()) <= exp10 + 1 ) out += digits + std::string(exp10 + 1 - digits.size(), '0') + ".0";
				else out += digits.substr(0, exp10 + 1) + "." + digits.substr(exp10 + 1);
			}
			else out += "0." + std::string(-exp10 - 1, '0') + digits;
		}
		else
		{
			out += digits.substr(0, 1);
			if( digits.size() > 1 ) out += "." + digits.substr(1);
			char eb[16];
			std::snprintf(eb, sizeof(eb), "e%c%02d", exp10 < 0 ? '-' : '+', std::abs(exp10));
			out += eb;
		}
		return out;
	}

	//***************************************************************************
	// @brief 셀 값을 문자열로 바꿉니다. 정수 모양의 숫자는 정수로, 그 외 숫자는 FloatRepr로, 불리언은 True/False로 표기합니다.
	// @param cell 셀
	// @return 표기 문자열
	//***************************************************************************
	std::string CellToString(const SCell& c)
	{
		if( c.kind == SCell::Bool ) return c.boolean ? "True" : "False";
		if( c.kind == SCell::Str ) return c.text;
		if( c.text.find_first_of(".eE") == std::string::npos ) return c.text;
		double v = 0;
		if( !ParseDouble(c.text, v) ) v = 0;
		return FloatRepr(v);
	}

	//***************************************************************************
	// @brief 오류 메시지에 값을 인용할 때 쓰는 따옴표 표기입니다. ('abc', "it's")
	// @param text 인용할 문자열
	// @return 따옴표로 감싼 문자열
	//***************************************************************************
	std::string Quote(const std::string& s)
	{
		const char q = (s.find('\'') != std::string::npos && s.find('"') == std::string::npos) ? '"' : '\'';
		std::string out(1, q);
		for( size_t i = 0; i < s.size(); ++i )
		{
			const char ch = s[i];
			unsigned cp = 0;
			if( ch == q || ch == '\\' ) { out += '\\'; out += ch; }
			else if( ch == '\n' ) out += "\\n";
			else if( ch == '\r' ) out += "\\r";
			else if( ch == '\t' ) out += "\\t";
			else if( static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F ) { char b[8]; std::snprintf(b, sizeof(b), "\\x%02X", static_cast<unsigned char>(ch)); out += b; }
			else if( const size_t n = BlankLenAt(s, i, &cp); n > 1 )
			{
				// 눈에 안 보이는 공백(NBSP 등)은 코드 값으로 보여 준다. 오류 메시지에서 원인을 찾을 수 있게 한다.
				char b[16];
				std::snprintf(b, sizeof(b), "\\u%04X", cp);
				out += b;
				i += n - 1;
			}
			else out += ch;
		}
		return out + q;
	}

	//***************************************************************************
	// @brief 앞뒤 공백을 제거합니다.
	//***************************************************************************
	std::string Trim(const std::string& s)
	{
		size_t b = 0, e = s.size();
		for( size_t n; (n = BlankLenAt(s, b)) != 0 && b < e; ) b += n;
		for( size_t n; (n = BlankLenBefore(s, b, e)) != 0; ) e -= n;
		return s.substr(b, e - b);
	}

	//***************************************************************************
	// @brief 영문 대문자를 소문자로 바꿉니다.
	//***************************************************************************
	std::string Lower(std::string text)
	{
		return AsciiChar::Lower(std::move(text));
	}

	//***************************************************************************
	// @brief 셀을 유한한 숫자로 읽습니다. (SheetConverter와 DefinitionSheets가 같은 규칙을 쓰도록 한곳에 둔다)
	//***************************************************************************
	bool ParseNumber(const SCell& cell, double& value, std::string& why)
	{
		if( cell.kind == SCell::Bool ) { why = U8("숫자가 아님: ") + CellToString(cell); return false; }

		if( cell.kind == SCell::Num ) { if( !ParseDouble(cell.text, value) ) value = 0; }
		else
		{
			// 0x1p3 같은 16진 표기는 엑셀 사용자가 의도한 숫자가 아니므로 ParseDouble이 받지 않는다.
			if( !ParseDouble(Trim(cell.text), value) ) { why = U8("숫자가 아님: ") + Quote(cell.text); return false; }
		}
		if( std::isnan(value) || std::isinf(value) )
		{
			why = U8("유한한 숫자가 아님: ") + (cell.kind == SCell::Str ? Quote(cell.text) : CellToString(cell));
			return false;
		}
		return true;
	}

	//***************************************************************************
	// @brief 셀을 정수로 읽습니다. (uint32 기반 enum의 2^31 이상 값도 표현하기 위해 int 변환 대신 int64로 읽는다)
	// @param why [out] 실패 사유
	//***************************************************************************
	bool ParseInt64(const SCell& cell, int64_t& value, std::string& why)
	{
		double v;
		if( !ParseNumber(cell, v, why) ) return false;
		if( v != std::floor(v) ) { why = U8("정수가 아님: ") + Trim(cell.text); return false; }
		if( std::fabs(v) > 9007199254740992.0 ) { why = U8("값이 너무 큼: ") + Trim(cell.text); return false; }	// 2^53. 이보다 크면 double이 정수를 정확히 표현하지 못한다
		value = static_cast<int64_t>(v);
		return true;
	}

	//***************************************************************************
	// @brief JSON 문자열 리터럴(따옴표 포함)을 만듭니다. 비ASCII(UTF-8)는 그대로 두고 제어 문자만 이스케이프합니다.
	// @param text UTF-8 문자열
	// @return 따옴표로 감싼 JSON 문자열
	//***************************************************************************
	std::string JsonString(const std::string& s)
	{
		std::string out = "\"";
		for( unsigned char c : s )
		{
			switch( c )
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if( c < 0x20 ) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
				else out += static_cast<char>(c);
			}
		}
		return out + "\"";
	}
}