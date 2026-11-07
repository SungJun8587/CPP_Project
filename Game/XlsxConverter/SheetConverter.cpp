
//***************************************************************************
// SheetConverter.cpp : 시트 -> JSON 변환 구현
//
//***************************************************************************

#include "pch.h"
#include "SheetConverter.h"
#include "Utf8Literal.h"

#include <algorithm>
#include <cerrno>
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


	//***************************************************************************
	// @brief 범위 한쪽 끝("-1000", "")을 읽습니다.
	// @param text  좌우 공백이 제거된 문자열. 비어 있으면 지정 안 함
	// @param has   [out] 값이 있으면 true
	// @param value [out] 정수 값
	// @return 형식이 맞으면 true
	//***************************************************************************
	bool ParseBound(const std::string& text, bool& has, int64_t& value)
	{
		has = false;
		if( text.empty() ) return true;
		errno = 0;
		char* end = nullptr;
		const long long v = std::strtoll(text.c_str(), &end, 10);
		if( errno != 0 || *end != '\0' ) return false;
		has = true;
		value = v;
		return true;
	}

	//***************************************************************************
	// @brief 헤더 칸 하나("Name:type[min..max]?")를 해석합니다.
	// @param text 칸 문자열 (앞뒤 공백 제거됨)
	// @param def  [out] 해석 결과 (col은 호출자가 채운다)
	// @param why  [out] 실패 사유
	// @return 형식이 맞으면 true
	//***************************************************************************
	bool ParseHeaderCell(const std::string& text, const SDefinitions* defs, SFieldDef& def, std::string& why)
	{
		const size_t colon = text.find(':');
		if( colon == std::string::npos ) { why = U8("'Name:type' 형식이어야 합니다"); return false; }

		def.name = ValueFormat::Trim(text.substr(0, colon));
		std::string rest = ValueFormat::Trim(text.substr(colon + 1));

		if( !rest.empty() && rest.back() == '?' )
		{
			def.optional = true;
			rest = ValueFormat::Trim(rest.substr(0, rest.size() - 1));
		}

		std::string range;
		bool hasRange = false;
		const size_t open = rest.find('[');
		if( open != std::string::npos )
		{
			if( rest.back() != ']' ) { why = U8("범위는 [min..max] 형식이어야 합니다"); return false; }
			range = rest.substr(open + 1, rest.size() - open - 2);
			rest = ValueFormat::Trim(rest.substr(0, open));
			hasRange = true;
		}
		def.type = rest;

		const bool typeOk = FindTypeInfo(def.type) != nullptr;
		if( def.name.empty() ) { why = U8("필드명이 비어 있습니다"); return false; }
		if( !typeOk )
		{
			// 기본 타입이 아니면 Enum 시트의 enum 이름이어야 한다. (범위는 못 쓴다)
			if( !hasRange && defs && defs->FindEnum(def.type) )
			{
				def.enumName = def.type;
				def.type = "enum";
				return true;
			}
			why = U8("타입은 int, uint, float, string, bool 또는 Enum 시트에 정의한 enum 이름이어야 합니다 ('") + def.type + "')";
			return false;
		}

		if( !hasRange ) return true;
		if( def.type != "int" && def.type != "uint" ) { why = U8("범위 [min..max]는 int, uint에만 쓸 수 있습니다"); return false; }

		const size_t dots = range.find("..");
		if( dots == std::string::npos ) { why = U8("범위는 [min..max] 형식이어야 합니다 (한쪽은 생략 가능)"); return false; }
		if( !ParseBound(ValueFormat::Trim(range.substr(0, dots)), def.hasMin, def.minV)
			|| !ParseBound(ValueFormat::Trim(range.substr(dots + 2)), def.hasMax, def.maxV) )
		{
			why = U8("범위 끝은 정수여야 합니다");
			return false;
		}

		const STypeInfo* info = FindTypeInfo(def.type);
		const int64_t lo = info->minV;
		const int64_t hi = info->maxV;
		if( (def.hasMin && (def.minV < lo || def.minV > hi)) || (def.hasMax && (def.maxV < lo || def.maxV > hi)) )
		{
			why = U8("범위가 ") + def.type + U8("의 표현 범위(") + std::to_string(lo) + ".." + std::to_string(hi) + U8(")를 벗어났습니다");
			return false;
		}
		if( def.hasMin && def.hasMax && def.minV > def.maxV ) { why = U8("범위의 min이 max보다 큽니다"); return false; }
		return true;
	}

}

//***************************************************************************
// @brief 셀 하나를 헤더에 선언된 타입으로 변환해 JSON 값 문자열로 만듭니다. (SheetConverter.h 참고)
//***************************************************************************
bool SheetConverter::ConvertCellValue(const SCell& cell, const SFieldDef& def, const SDefinitions* defs, std::string& json, std::string& why)
{
	{
		const std::string& type = def.type;
		if( SheetConverter::IsBlank(cell) ) { why = U8("빈 칸"); return false; }

		if( type == "string" ) { json = ValueFormat::JsonString(ValueFormat::CellToString(cell)); return true; }

		if( type == "enum" )
		{
			// JSON에는 멤버 이름을 그대로 넣는다. 오타는 여기서 잡는다.
			const std::string name = ValueFormat::Trim(ValueFormat::CellToString(cell));
			const SEnumDef* e = defs ? defs->FindEnum(def.enumName) : nullptr;
			if( !e ) { why = U8("enum 정의를 찾을 수 없음: ") + def.enumName; return false; }
			for( const SEnumMember& m : e->members )
				if( m.name == name ) { json = ValueFormat::JsonString(name); return true; }

			std::string allowed;
			for( const SEnumMember& m : e->members )
				allowed += (allowed.empty() ? "" : ", ") + m.name;
			why = def.enumName + U8("의 멤버가 아님: ") + ValueFormat::Quote(name) + U8(" (허용: ") + allowed + ")";
			return false;
		}

		if( type == "bool" )
		{
			if( cell.kind == SCell::Bool ) { json = cell.boolean ? "true" : "false"; return true; }
			const std::string s = ValueFormat::Lower(ValueFormat::Trim(ValueFormat::CellToString(cell)));
			if( s == "1" || s == "true" || s == "yes" || s == "y" ) { json = "true"; return true; }
			if( s == "0" || s == "false" || s == "no" || s == "n" ) { json = "false"; return true; }
			why = U8("bool이 아님: ") + (cell.kind == SCell::Str ? ValueFormat::Quote(cell.text) : ValueFormat::CellToString(cell));
			return false;
		}

		if( cell.kind == SCell::Bool ) { why = U8("숫자가 아님: ") + ValueFormat::CellToString(cell); return false; }

		double num;
		if( !ValueFormat::ParseNumber(cell, num, why) ) return false;

		if( type == "float" ) { json = ValueFormat::FloatRepr(num); return true; }

		// 오류 메시지에 인용할 원래 값 (성공 경로에서는 만들지 않는다)
		const auto shown = [&]() { return cell.kind == SCell::Str ? ValueFormat::Quote(cell.text) : ValueFormat::CellToString(cell); };
		if( num != std::floor(num) ) { why = U8("정수가 아님: ") + shown(); return false; }
		if( type == "uint" && num < 0 ) { why = U8("음수는 uint가 될 수 없음: ") + shown(); return false; }

		// 헤더에 범위가 없어도 C++ 행 구조체(int32/uint32)에 담기는 값이어야 한다.
		const STypeInfo* info = FindTypeInfo(type);
		const double lo = static_cast<double>(def.hasMin ? def.minV : info->minV);
		const double hi = static_cast<double>(def.hasMax ? def.maxV : info->maxV);
		if( num < lo || num > hi )
		{
			why = shown() + U8("은(는) 허용 범위 ") + std::to_string(static_cast<long long>(lo)) + ".." + std::to_string(static_cast<long long>(hi)) + U8("를 벗어남");
			return false;
		}

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
bool SheetConverter::IsBlank(const SCell& cell)
{
	return cell.kind == SCell::Empty || (cell.kind == SCell::Str && ValueFormat::Trim(cell.text).empty());
}

bool SheetConverter::IsCommentRow(const TRow& row)
{
	if( row.empty() || row[0].kind != SCell::Str ) return false;
	const std::string t = ValueFormat::Trim(row[0].text);
	return !t.empty() && t[0] == '#';
}

bool SheetConverter::IsSkipRow(const TRow& row)
{
	return IsCommentRow(row) || std::all_of(row.begin(), row.end(), SheetConverter::IsBlank);
}

std::string SheetConverter::RowLabel(const std::string& sheet, size_t row)
{
	return sheet + "!" + std::to_string(row) + U8("행 ");
}

bool SheetConverter::Convert(const std::string& sheet, const TRows& rows, std::vector<std::string>& errors, std::string& jsonOut, size_t& rowCount, SSheetSchema* schemaOut, const SDefinitions* defs, std::vector<std::string>* warnings)
{
	auto headerIt = rows.find(1);
	std::vector<SFieldDef> fields;
	const size_t errBefore = errors.size();

	if( headerIt == rows.end() || std::all_of(headerIt->second.begin(), headerIt->second.end(), SheetConverter::IsBlank) )
	{
		errors.push_back(headerIt == rows.end() && rows.empty() ? sheet + U8(": 시트가 비어 있습니다") : sheet + U8(": 헤더가 비어 있습니다"));
		return false;
	}

	std::set<std::string> seen;
	const TRow& header = headerIt->second;
	for( size_t i = 0; i < header.size(); ++i )
	{
		if( SheetConverter::IsBlank(header[i]) ) continue;
		const std::string text = ValueFormat::Trim(ValueFormat::CellToString(header[i]));
		const std::string where = sheet + "!" + std::to_string(i + 1) + U8("열 헤더 '") + text + "'";

		SFieldDef def;
		std::string why;
		if( !ParseHeaderCell(text, defs, def, why) ) { errors.push_back(where + ": " + why); continue; }
		if( !seen.insert(def.name).second ) { errors.push_back(where + U8(": 필드명 중복")); continue; }
		def.col = i;
		fields.push_back(def);
	}
	if( fields.empty() ) { if( errors.size() == errBefore ) errors.push_back(sheet + U8(": 헤더가 비어 있습니다")); return false; }

	// 필드마다 JSON 키 부분("      \"이름\": ")은 한 번만 만든다. (행 x 열 만큼 되풀이해 만들 필요가 없다)
	std::vector<std::string> keys;
	keys.reserve(fields.size());
	for( const SFieldDef& f : fields )
		keys.push_back("      " + ValueFormat::JsonString(f.name) + ": ");

	// 본문은 jsonOut에 바로 쓴다. 실패하면 비운다.
	jsonOut = "{\n  " + ValueFormat::JsonString(sheet) + ": ";
	rowCount = 0;
	const SCell emptyCell;
	for( const auto& kv : rows )
	{
		if( kv.first < 2 ) continue;
		const TRow& row = kv.second;
		if( IsSkipRow(row) )
		{
			// 첫 칸만 '#'이고 나머지는 비어 있어야 주석이다. 다른 칸에 값이 있으면 데이터를 주석으로 잘못 본 것일 수 있다.
			if( warnings && IsCommentRow(row) && std::any_of(row.begin() + 1, row.end(), [](const SCell& c) { return !SheetConverter::IsBlank(c); }) )
				warnings->push_back(RowLabel(sheet, kv.first) + U8("첫 칸이 '#'로 시작해 주석으로 건너뜁니다. 다른 칸에도 값이 있으니 데이터가 맞는지 확인하세요"));
			continue;
		}

		jsonOut += rowCount ? ",\n    {\n" : "[\n    {\n";
		bool first = true;
		for( size_t i = 0; i < fields.size(); ++i )
		{
			const SFieldDef& f = fields[i];
			const SCell& cell = f.col < row.size() ? row[f.col] : emptyCell;
			if( f.optional && SheetConverter::IsBlank(cell) ) continue;

			std::string json, why;
			if( !ConvertCellValue(cell, f, defs, json, why) )
			{
				errors.push_back(RowLabel(sheet, kv.first) + f.name + ": " + why);
				continue;
			}
			if( !first ) jsonOut += ",\n";
			first = false;
			jsonOut += keys[i];
			jsonOut += json;
		}
		jsonOut += "\n    }";
		++rowCount;
	}

	if( errors.size() != errBefore ) { jsonOut.clear(); return false; }
	jsonOut += rowCount ? "\n  ]" : "[]";
	jsonOut += "\n}\n";
	if( schemaOut )
	{
		schemaOut->name = sheet;
		schemaOut->fields = std::move(fields);
	}
	return true;
}