
//***************************************************************************
// ValueFormat.cpp : 값 표기 규칙 구현
//
//***************************************************************************

#include "pch.h"
#include "ValueFormat.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using Xlnt::SCell;
using Xlnt::TRow;
using Xlnt::TRows;

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
		return FloatRepr(std::strtod(c.text.c_str(), nullptr));
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
		for( char ch : s )
		{
			if( ch == q || ch == '\\' ) { out += '\\'; out += ch; }
			else if( ch == '\n' ) out += "\\n";
			else if( ch == '\r' ) out += "\\r";
			else if( ch == '\t' ) out += "\\t";
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
		while( b < e && isspace(static_cast<unsigned char>(s[b])) ) ++b;
		while( e > b && isspace(static_cast<unsigned char>(s[e - 1])) ) --e;
		return s.substr(b, e - b);
	}

	//***************************************************************************
	// @brief 영문 대문자를 소문자로 바꿉니다.
	//***************************************************************************
	std::string Lower(std::string text)
	{
		for( char& c : text )
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return text;
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
