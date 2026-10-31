
//***************************************************************************
// SheetConverter.cpp : 시트 -> JSON 변환 구현
//
//***************************************************************************

#include "pch.h"
#include "SheetConverter.h"
#include "ValueFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <set>

using Xlnt::SCell;
using Xlnt::TRow;
using Xlnt::TRows;

namespace
{
	struct Field
	{
		size_t		col;
		std::string	name;
		std::string	type;
	};

	const char* kTypes[] = { "int", "uint", "float", "string", "bool" };

	//***************************************************************************
	// @brief 비어 있는 셀인지 확인합니다. (값이 없거나 공백뿐인 문자열)
	//***************************************************************************
	bool IsEmptyCell(const SCell& c)
	{
		return c.kind == SCell::Empty || (c.kind == SCell::Str && ValueFormat::Trim(c.text).empty());
	}

	//***************************************************************************
	// @brief 셀 하나를 헤더에 선언된 타입으로 변환해 JSON 값 문자열로 만듭니다.
	// @param cell 셀
	// @param type 필드 타입 (int, uint, float, string, bool)
	// @param json [out] JSON 값 (이미 따옴표/이스케이프 포함)
	// @param why  [out] 실패 사유
	// @return 변환했으면 true
	//***************************************************************************
	bool ConvertCell(const SCell& cell, const std::string& type, std::string& json, std::string& why)
	{
		const bool blank = cell.kind == SCell::Empty || (cell.kind == SCell::Str && ValueFormat::Trim(cell.text).empty());
		if( blank ) { why = "빈 칸"; return false; }

		if( type == "string" ) { json = ValueFormat::JsonString(ValueFormat::CellToString(cell)); return true; }

		if( type == "bool" )
		{
			if( cell.kind == SCell::Bool ) { json = cell.boolean ? "true" : "false"; return true; }
			const std::string s = ValueFormat::Lower(ValueFormat::Trim(ValueFormat::CellToString(cell)));
			if( s == "1" || s == "true" || s == "yes" || s == "y" ) { json = "true"; return true; }
			if( s == "0" || s == "false" || s == "no" || s == "n" ) { json = "false"; return true; }
			why = "bool이 아님: " + (cell.kind == SCell::Str ? ValueFormat::Quote(cell.text) : ValueFormat::CellToString(cell));
			return false;
		}

		if( cell.kind == SCell::Bool ) { why = "숫자가 아님: " + ValueFormat::CellToString(cell); return false; }

		double num;
		if( cell.kind == SCell::Num ) num = std::strtod(cell.text.c_str(), nullptr);
		else
		{
			const std::string t = ValueFormat::Trim(cell.text);
			char* end = nullptr;
			num = std::strtod(t.c_str(), &end);
			const bool bad = t.empty() || *end != '\0' || t.find_first_of("xXpP") != std::string::npos;
			if( bad ) { why = "숫자가 아님: " + ValueFormat::Quote(cell.text); return false; }
		}
		if( std::isnan(num) || std::isinf(num) )
		{
			why = "유한한 숫자가 아님: " + (cell.kind == SCell::Str ? ValueFormat::Quote(cell.text) : ValueFormat::CellToString(cell));
			return false;
		}

		const std::string shown = cell.kind == SCell::Str ? ValueFormat::Quote(cell.text) : ValueFormat::CellToString(cell);
		if( type == "float" ) { json = ValueFormat::FloatRepr(num); return true; }

		if( num != std::floor(num) ) { why = "정수가 아님: " + shown; return false; }
		if( type == "uint" && num < 0 ) { why = "음수는 uint가 될 수 없음: " + shown; return false; }
		if( num == 0 ) { json = "0"; return true; }
		char buf[400];
		std::snprintf(buf, sizeof(buf), "%.0f", num);
		json = buf;
		return true;
	}
}

//***************************************************************************
// @brief 시트를 변환합니다. (SheetConverter.h 참고)
//***************************************************************************
bool SheetConverter::Convert(const std::string& sheet, const TRows& rows, std::vector<std::string>& errors, std::string& jsonOut, size_t& rowCount)
{
	auto headerIt = rows.find(1);
	std::vector<Field> fields;
	const size_t errBefore = errors.size();

	if( headerIt == rows.end() || std::all_of(headerIt->second.begin(), headerIt->second.end(), IsEmptyCell) )
	{
		errors.push_back(headerIt == rows.end() && rows.empty() ? sheet + ": 시트가 비어 있습니다" : sheet + ": 헤더가 비어 있습니다");
		return false;
	}

	std::set<std::string> seen;
	const TRow& header = headerIt->second;
	for( size_t i = 0; i < header.size(); ++i )
	{
		if( IsEmptyCell(header[i]) ) continue;
		const std::string text = ValueFormat::Trim(ValueFormat::CellToString(header[i]));
		const std::string where = sheet + "!" + std::to_string(i + 1) + "열 헤더 '" + text + "'";
		const size_t colon = text.find(':');
		if( colon == std::string::npos ) { errors.push_back(where + ": 'Name:type' 형식이어야 합니다"); continue; }
		const std::string name = ValueFormat::Trim(text.substr(0, colon)), type = ValueFormat::Trim(text.substr(colon + 1));
		const bool typeOk = std::find_if(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return type == t; }) != std::end(kTypes);
		if( name.empty() || !typeOk ) { errors.push_back(where + ": 타입은 int, uint, float, string, bool 중 하나"); continue; }
		if( !seen.insert(name).second ) { errors.push_back(where + ": 필드명 중복"); continue; }
		fields.push_back({ i, name, type });
	}
	if( fields.empty() ) { if( errors.size() == errBefore ) errors.push_back(sheet + ": 헤더가 비어 있습니다"); return false; }

	std::string body;
	rowCount = 0;
	const SCell emptyCell;
	for( const auto& kv : rows )
	{
		if( kv.first < 2 ) continue;
		const TRow& row = kv.second;
		if( !row.empty() && row[0].kind == SCell::Str )
		{
			const std::string t = ValueFormat::Trim(row[0].text);
			if( !t.empty() && t[0] == '#' ) continue;
		}
		if( std::all_of(row.begin(), row.end(), IsEmptyCell) ) continue;

		std::string item;
		bool first = true;
		for( const Field& f : fields )
		{
			const SCell& cell = f.col < row.size() ? row[f.col] : emptyCell;
			std::string json, why;
			if( !ConvertCell(cell, f.type, json, why) )
			{
				errors.push_back(sheet + "!" + std::to_string(kv.first) + "행 " + f.name + ": " + why);
				continue;
			}
			item += std::string(first ? "" : ",\n") + "      " + ValueFormat::JsonString(f.name) + ": " + json;
			first = false;
		}
		body += std::string(rowCount ? ",\n" : "") + "    {\n" + item + "\n    }";
		++rowCount;
	}

	if( errors.size() != errBefore ) return false;
	jsonOut = "{\n  " + ValueFormat::JsonString(sheet) + ": " + (rowCount ? "[\n" + body + "\n  ]" : "[]") + "\n}\n";
	return true;
}
