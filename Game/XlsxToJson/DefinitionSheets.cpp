
//***************************************************************************
// DefinitionSheets.cpp : Enum, Const 시트 해석 구현
//
//***************************************************************************

#include "pch.h"
#include "DefinitionSheets.h"
#include "SheetConverter.h"
#include "ValueFormat.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>

using Xlnt::SCell;
using Xlnt::TRow;
using Xlnt::TRows;

namespace
{
	//***************************************************************************
	// @brief 헤더 칸에서 열 이름만 꺼냅니다. ("Value:int?" -> "Value")
	//***************************************************************************
	std::string ColumnName(const SCell& cell)
	{
		const std::string text = ValueFormat::Trim(ValueFormat::CellToString(cell));
		return ValueFormat::Trim(text.substr(0, text.find(':')));
	}

	//***************************************************************************
	// @brief 시트 헤더를 읽어 열 이름 -> 0-base 열 번호 표를 만듭니다.
	// @param allowed 허용하는 열 이름
	// @param needed  반드시 있어야 하는 열 이름
	//***************************************************************************
	bool ReadColumns(const std::string& sheet, const TRows& rows, const std::set<std::string>& allowed, const std::set<std::string>& needed,
		std::map<std::string, size_t>& cols, std::vector<std::string>& errors)
	{
		const size_t before = errors.size();
		auto it = rows.find(1);
		if( it == rows.end() ) { errors.push_back(sheet + U8(": 헤더가 비어 있습니다")); return false; }

		for( size_t i = 0; i < it->second.size(); ++i )
		{
			if( SheetConverter::IsBlank(it->second[i]) ) continue;
			const std::string name = ColumnName(it->second[i]);
			if( !allowed.count(name) ) { errors.push_back(sheet + "!" + std::to_string(i + 1) + U8("열 헤더 '") + name + U8("': 알 수 없는 열입니다")); continue; }
			if( !cols.emplace(name, i).second ) errors.push_back(sheet + "!" + std::to_string(i + 1) + U8("열 헤더 '") + name + U8("': 열 이름 중복"));
		}
		for( const std::string& n : needed )
			if( !cols.count(n) ) errors.push_back(sheet + ": '" + n + U8("' 열이 없습니다"));
		return errors.size() == before;
	}

	const SCell& CellAt(const TRow& row, const std::map<std::string, size_t>& cols, const char* name)
	{
		static const SCell empty;
		auto it = cols.find(name);
		return (it == cols.end() || it->second >= row.size()) ? empty : row[it->second];
	}

	//***************************************************************************
	// @brief 건너뛸 행(주석 행, 빈 행)인지 확인합니다.
	//***************************************************************************
	bool SkipRow(const TRow& row)
	{
		if( !row.empty() && row[0].kind == SCell::Str )
		{
			const std::string t = ValueFormat::Trim(row[0].text);
			if( !t.empty() && t[0] == '#' ) return true;
		}
		return std::all_of(row.begin(), row.end(), [](const SCell& c) { return SheetConverter::IsBlank(c); });
	}

	//***************************************************************************
	// @brief 셀을 64비트 정수로 읽습니다. (uint32 기반 enum의 2^31 이상 값도 표현하기 위해 int 변환 대신 쓴다)
	// @param why [out] 실패 사유
	//***************************************************************************
	bool ReadInt64(const SCell& cell, int64_t& value, std::string& why)
	{
		if( cell.kind == SCell::Bool ) { why = U8("숫자가 아님: ") + ValueFormat::CellToString(cell); return false; }

		const std::string t = ValueFormat::Trim(cell.text);
		char* end = nullptr;
		const double v = std::strtod(t.c_str(), &end);
		if( t.empty() || *end != '\0' || t.find_first_of("xXpP") != std::string::npos ) { why = U8("숫자가 아님: ") + ValueFormat::Quote(cell.text); return false; }
		if( v != std::floor(v) ) { why = U8("정수가 아님: ") + t; return false; }
		if( std::fabs(v) > 9007199254740992.0 ) { why = U8("값이 너무 큼: ") + t; return false; }	// 2^53. 이보다 크면 double이 정수를 정확히 표현하지 못한다
		value = static_cast<int64_t>(v);
		return true;
	}

	std::string Text(const SCell& c) { return ValueFormat::Trim(ValueFormat::CellToString(c)); }
}

bool DefinitionSheets::IsReservedSheet(const std::string& name)
{
	return name == kEnumSheet || name == kConstSheet || name == "Enums" || name == "Consts" || name == "EnumStrings" || name == "GeneratedCommon";
}

bool DefinitionSheets::ParseEnumSheet(const TRows& rows, SDefinitions& defs, std::vector<std::string>& errors)
{
	const size_t before = errors.size();
	const std::string sheet = kEnumSheet;

	std::map<std::string, size_t> cols;
	if( !ReadColumns(sheet, rows, { "Enum", "Name", "Value", "Base", "Comment" }, { "Enum", "Name" }, cols, errors) )
		return false;

	for( const auto& kv : rows )
	{
		if( kv.first < 2 || SkipRow(kv.second) ) continue;
		const TRow& row = kv.second;
		const std::string where = sheet + "!" + std::to_string(kv.first) + U8("행 ");

		const std::string enumName = Text(CellAt(row, cols, "Enum")), memberName = Text(CellAt(row, cols, "Name"));
		if( !IsPlainIdentifier(enumName) ) { errors.push_back(where + U8("Enum: C++/C# 식별자여야 합니다 ('") + enumName + "')"); continue; }
		if( !IsPlainIdentifier(memberName) ) { errors.push_back(where + U8("Name: C++/C# 식별자여야 합니다 ('") + memberName + "')"); continue; }

		auto it = std::find_if(defs.enums.begin(), defs.enums.end(), [&](const SEnumDef& e) { return e.name == enumName; });
		if( it == defs.enums.end() )
		{
			defs.enums.push_back(SEnumDef());
			defs.enums.back().name = enumName;
			it = defs.enums.end() - 1;
		}
		SEnumDef& e = *it;

		const SCell& baseCell = CellAt(row, cols, "Base");
		if( !SheetConverter::IsBlank(baseCell) )
		{
			const std::string base = Text(baseCell);
			if( !FindBaseInfo(base) ) { errors.push_back(where + U8("Base: uint8, uint16, uint32, int32 중 하나여야 합니다 ('") + base + "')"); continue; }
			if( e.members.empty() ) e.base = base;
			else if( e.base != base ) { errors.push_back(where + "Base: " + enumName + U8("의 기반형이 앞 행(") + e.base + U8(")과 다릅니다 ('") + base + "')"); continue; }
		}

		SEnumMember m;
		m.name = memberName;
		m.comment = Text(CellAt(row, cols, "Comment"));

		const SCell& valueCell = CellAt(row, cols, "Value");
		if( SheetConverter::IsBlank(valueCell) )
		{
			m.value = e.members.empty() ? 0 : e.members.back().value + 1;
		}
		else
		{
			std::string why;
			if( !ReadInt64(valueCell, m.value, why) ) { errors.push_back(where + "Value: " + why); continue; }
		}

		for( const SEnumMember& other : e.members )
		{
			if( other.name == m.name ) { errors.push_back(where + "Name: " + enumName + U8("에 같은 이름의 멤버가 이미 있습니다 ('") + m.name + "')"); m.name.clear(); break; }
			if( other.value == m.value ) { errors.push_back(where + "Value: " + enumName + U8("에 같은 값(") + std::to_string(m.value) + U8(")이 이미 있습니다 ('") + other.name + "')"); m.name.clear(); break; }
		}
		if( !m.name.empty() ) e.members.push_back(std::move(m));
	}

	// 기반형으로 표현되는지는 모든 행을 읽은 뒤에 확인한다. (Base가 첫 행에만 있어도 되므로)
	for( const SEnumDef& e : defs.enums )
	{
		const SBaseInfo* bi = FindBaseInfo(e.base);
		const int64_t lo = bi->minV, hi = bi->maxV;
		for( const SEnumMember& m : e.members )
			if( m.value < lo || m.value > hi )
				errors.push_back(sheet + ": " + e.name + "." + m.name + U8(" 값 ") + std::to_string(m.value) + U8("이(가) ") + e.base + U8(" 범위(") + std::to_string(lo) + ".." + std::to_string(hi) + U8(")를 벗어났습니다"));
	}
	return errors.size() == before;
}

bool DefinitionSheets::ParseConstSheet(const TRows& rows, SDefinitions& defs, std::vector<std::string>& errors)
{
	const size_t before = errors.size();
	const std::string sheet = kConstSheet;

	std::map<std::string, size_t> cols;
	if( !ReadColumns(sheet, rows, { "Name", "Type", "Value", "Comment" }, { "Name", "Type", "Value" }, cols, errors) )
		return false;

	std::set<std::string> seen;
	for( const auto& kv : rows )
	{
		if( kv.first < 2 || SkipRow(kv.second) ) continue;
		const TRow& row = kv.second;
		const std::string where = sheet + "!" + std::to_string(kv.first) + U8("행 ");

		SConstDef c;
		c.name = Text(CellAt(row, cols, "Name"));
		c.type = Text(CellAt(row, cols, "Type"));
		c.comment = Text(CellAt(row, cols, "Comment"));

		if( !IsPlainIdentifier(c.name) ) { errors.push_back(where + U8("Name: C++/C# 식별자여야 합니다 ('") + c.name + "')"); continue; }
		if( !seen.insert(c.name).second ) { errors.push_back(where + U8("Name: 이름 중복 ('") + c.name + "')"); continue; }
		if( !FindTypeInfo(c.type) )
		{
			errors.push_back(where + U8("Type: int, uint, float, string, bool 중 하나여야 합니다 ('") + c.type + "')");
			continue;
		}

		SFieldDef def;
		def.type = c.type;
		std::string why;
		if( !SheetConverter::ConvertCellValue(CellAt(row, cols, "Value"), def, nullptr, c.literal, why) ) { errors.push_back(where + "Value: " + why); continue; }
		defs.consts.push_back(std::move(c));
	}
	return errors.size() == before;
}