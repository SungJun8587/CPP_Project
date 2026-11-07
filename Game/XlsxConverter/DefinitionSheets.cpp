
//***************************************************************************
// DefinitionSheets.cpp : Enum, Const 시트 해석 구현
//
//***************************************************************************

#include "pch.h"
#include "DefinitionSheets.h"
#include "Utf8Literal.h"

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
	// @struct SDataRow
	// @brief 데이터 행 하나. (행 번호는 엑셀의 1-base 번호)
	//***************************************************************************
	struct SDataRow
	{
		int			first;
		const TRow& second;
	};

	//***************************************************************************
	// @brief 헤더(1행)와 주석 행, 빈 행을 뺀 데이터 행들을 시트 순서대로 돌려줍니다.
	// @details 정의 시트마다 되풀이하던 "kv.first < 2 || IsSkipRow" 규칙을 한곳에 둔다. (SheetConverter::Convert도 같은 규칙을 쓴다)
	//***************************************************************************
	std::vector<SDataRow> DataRows(const TRows& rows)
	{
		std::vector<SDataRow> v;
		v.reserve(rows.size());
		for( const auto& kv : rows )
			if( kv.first >= 2 && !SheetConverter::IsSkipRow(kv.second) )
				v.push_back(SDataRow{ kv.first, kv.second });
		return v;
	}

	std::string Text(const SCell& c) { return ValueFormat::Trim(ValueFormat::CellToString(c)); }
}

bool DefinitionSheets::IsReservedSheet(const std::string& name)
{
	// 생성 파일 이름(Enums.gen.h 등)은 Windows에서 대소문자를 구분하지 않으므로 이름도 대소문자 없이 비교한다.
	const std::string lower = AsciiChar::Lower(name);
	for( const char* r : { "enum", "const", "enums", "consts", "enumstrings", "generatedcommon", "packetenum", "packetconst", "packetgroup", "packet", "packetfield", "zonepackettypes", "zonepacketstructs" } )
		if( lower == r ) return true;
	return false;
}

bool DefinitionSheets::IsPacketSheet(const std::string& name)
{
	return name == kPacketEnumSheet || name == kPacketConstSheet || name == kPacketGroupSheet || name == kPacketSheet || name == kPacketFieldSheet;
}

namespace
{
	//***************************************************************************
	// @brief Enum 계열 시트(Enum, PacketEnum)를 읽어 enums에 채웁니다.
	// @param withEnumComment true면 EnumComment 열(enum 설명)을 허용한다 (PacketEnum 전용)
	//***************************************************************************
	bool ParseEnumRows(const std::string& sheet, bool withEnumComment, const TRows& rows, std::vector<SEnumDef>& enums, std::vector<std::string>& errors);
}

bool DefinitionSheets::ParseEnumSheet(const TRows& rows, SDefinitions& defs, std::vector<std::string>& errors)
{
	return ParseEnumRows(kEnumSheet, false, rows, defs.enums, errors);
}

namespace
{
	bool ParseEnumRows(const std::string& sheet, bool withEnumComment, const TRows& rows, std::vector<SEnumDef>& enums, std::vector<std::string>& errors)
	{
		const size_t before = errors.size();

		std::map<std::string, size_t> cols;
		std::set<std::string> allowed = { "Enum", "Name", "Value", "Base", "Comment" };
		if( withEnumComment ) allowed.insert("EnumComment");
		if( !ReadColumns(sheet, rows, allowed, { "Enum", "Name" }, cols, errors) )
			return false;

		for( const SDataRow& kv : DataRows(rows) )
		{
			const TRow& row = kv.second;
			const std::string where = SheetConverter::RowLabel(sheet, kv.first);

			const std::string enumName = Text(CellAt(row, cols, "Enum")), memberName = Text(CellAt(row, cols, "Name"));
			if( !IsPlainIdentifier(enumName) ) { errors.push_back(where + U8("Enum: C++/C# 식별자여야 합니다 ('") + enumName + "')"); continue; }
			if( !IsPlainIdentifier(memberName) ) { errors.push_back(where + U8("Name: C++/C# 식별자여야 합니다 ('") + memberName + "')"); continue; }

			auto it = std::find_if(enums.begin(), enums.end(), [&](const SEnumDef& e) { return e.name == enumName; });
			if( it == enums.end() )
			{
				enums.push_back(SEnumDef());
				enums.back().name = enumName;
				it = enums.end() - 1;
			}
			SEnumDef& e = *it;

			if( withEnumComment )
			{
				const std::string ec = Text(CellAt(row, cols, "EnumComment"));
				if( !ec.empty() )
				{
					if( e.comment.empty() ) e.comment = ec;
					else errors.push_back(where + "EnumComment: " + enumName + U8("의 설명은 첫 행에만 쓸 수 있습니다"));
				}
			}

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
				if( !ValueFormat::ParseInt64(valueCell, m.value, why) ) { errors.push_back(where + "Value: " + why); continue; }
			}

			for( const SEnumMember& other : e.members )
			{
				if( other.name == m.name ) { errors.push_back(where + "Name: " + enumName + U8("에 같은 이름의 멤버가 이미 있습니다 ('") + m.name + "')"); m.name.clear(); break; }
				if( other.value == m.value ) { errors.push_back(where + "Value: " + enumName + U8("에 같은 값(") + std::to_string(m.value) + U8(")이 이미 있습니다 ('") + other.name + "')"); m.name.clear(); break; }
			}
			if( !m.name.empty() ) e.members.push_back(std::move(m));
		}

		// 기반형으로 표현되는지는 모든 행을 읽은 뒤에 확인한다. (Base가 첫 행에만 있어도 되므로)
		for( const SEnumDef& e : enums )
		{
			const SBaseInfo* bi = FindBaseInfo(e.base);
			const int64_t lo = bi->minV, hi = bi->maxV;
			for( const SEnumMember& m : e.members )
				if( m.value < lo || m.value > hi )
					errors.push_back(sheet + ": " + e.name + "." + m.name + U8(" 값 ") + std::to_string(m.value) + U8("이(가) ") + e.base + U8(" 범위(") + std::to_string(lo) + ".." + std::to_string(hi) + U8(")를 벗어났습니다"));
		}
		return errors.size() == before;
	}
}	// namespace

bool DefinitionSheets::ParseConstSheet(const TRows& rows, SDefinitions& defs, std::vector<std::string>& errors)
{
	const size_t before = errors.size();
	const std::string sheet = kConstSheet;

	std::map<std::string, size_t> cols;
	if( !ReadColumns(sheet, rows, { "Name", "Type", "Value", "Comment" }, { "Name", "Type", "Value" }, cols, errors) )
		return false;

	std::set<std::string> seen;
	for( const SDataRow& kv : DataRows(rows) )
	{
		const TRow& row = kv.second;
		const std::string where = SheetConverter::RowLabel(sheet, kv.first);

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

namespace
{
	const size_t kPacketHeaderBytes = 4;	// PacketHeader { uint16 size; uint16 type; }

	// PacketField 시트 Type 열에 쓸 수 있는 이름 목록 (오류 메시지용). FindPacketTypeInfo의 표와 같은 순서다.
	const char* const kPacketTypeNames = "uint8, bool8, uint16, uint32, int32, uint64, float, char";

	//***************************************************************************
	// @brief 셀 안의 줄바꿈(\r\n, \r)을 \n 하나로 통일하고 줄마다 앞뒤 공백을 지웁니다. (여러 줄 주석용)
	//***************************************************************************
	std::string MultiLine(const SCell& cell)
	{
		std::string t = ValueFormat::CellToString(cell), out, line;
		t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());
		size_t pos = 0;
		while( pos <= t.size() )
		{
			size_t e = t.find('\n', pos);
			if( e == std::string::npos ) e = t.size();
			line = ValueFormat::Trim(t.substr(pos, e - pos));
			if( !out.empty() || !line.empty() ) { if( !out.empty() ) out += '\n'; out += line; }
			pos = e + 1;
		}
		while( !out.empty() && out.back() == '\n' ) out.pop_back();
		return out;
	}

	bool OneLine(const std::string& s) { return s.find('\n') == std::string::npos; }
}

bool DefinitionSheets::ParsePacketSheets(const std::map<std::string, TRows>& sheets, SPacketSchema& schema, std::vector<std::string>& errors)
{
	const size_t before = errors.size();
	const auto find = [&](const char* name) -> const TRows* { auto it = sheets.find(name); return it == sheets.end() ? nullptr : &it->second; };

	// ---- PacketEnum
	if( const TRows* rows = find(kPacketEnumSheet) )
		ParseEnumRows(kPacketEnumSheet, true, *rows, schema.enums, errors);

	// ---- PacketConst
	if( const TRows* rows = find(kPacketConstSheet) )
	{
		const std::string sheet = kPacketConstSheet;
		std::map<std::string, size_t> cols;
		if( ReadColumns(sheet, *rows, { "Name", "Type", "Value", "Comment" }, { "Name", "Type", "Value" }, cols, errors) )
		{
			for( const SDataRow& kv : DataRows(*rows) )
			{
				const TRow& row = kv.second;
				const std::string where = SheetConverter::RowLabel(sheet, kv.first);
				SPacketConst c;
				c.name = Text(CellAt(row, cols, "Name"));
				c.type = Text(CellAt(row, cols, "Type"));
				c.comment = MultiLine(CellAt(row, cols, "Comment"));
				if( !IsPlainIdentifier(c.name) ) { errors.push_back(where + U8("Name: 식별자여야 합니다 ('") + c.name + "')"); continue; }
				if( schema.FindConst(c.name) ) { errors.push_back(where + U8("Name: 이름 중복 ('") + c.name + "')"); continue; }
				if( c.type != "uint16" && c.type != "uint32" && c.type != "size" ) { errors.push_back(where + U8("Type: uint16, uint32, size 중 하나여야 합니다 ('") + c.type + "')"); continue; }
				int64_t v; std::string why;
				if( !ValueFormat::ParseInt64(CellAt(row, cols, "Value"), v, why) ) { errors.push_back(where + "Value: " + why); continue; }
				const int64_t hi = c.type == "uint16" ? 65535 : c.type == "size" ? 2147483647ll : 4294967295ll;		// size는 C#에서 int로 나온다
				if( v < 0 || v > hi ) { errors.push_back(where + U8("Value: ") + c.type + U8(" 범위를 벗어났습니다 (") + std::to_string(v) + ")"); continue; }
				c.value = static_cast<uint64_t>(v);
				schema.consts.push_back(std::move(c));
			}
		}
	}

	// ---- PacketGroup
	if( const TRows* rows = find(kPacketGroupSheet) )
	{
		const std::string sheet = kPacketGroupSheet;
		std::map<std::string, size_t> cols;
		if( ReadColumns(sheet, *rows, { "Group", "Enum", "CsEnum", "Min", "Max", "Section", "Comment" }, { "Group", "Enum", "CsEnum", "Min", "Section" }, cols, errors) )
		{
			for( const SDataRow& kv : DataRows(*rows) )
			{
				const TRow& row = kv.second;
				const std::string where = SheetConverter::RowLabel(sheet, kv.first);
				SPacketGroup g;
				g.name = Text(CellAt(row, cols, "Group"));
				g.enumName = Text(CellAt(row, cols, "Enum"));
				g.csEnum = Text(CellAt(row, cols, "CsEnum"));
				g.section = MultiLine(CellAt(row, cols, "Section"));
				g.comment = MultiLine(CellAt(row, cols, "Comment"));
				if( !IsPlainIdentifier(g.name) || !IsPlainIdentifier(g.enumName) || !IsPlainIdentifier(g.csEnum) ) { errors.push_back(where + U8("Group/Enum/CsEnum: 식별자여야 합니다")); continue; }
				if( schema.FindGroup(g.name) ) { errors.push_back(where + U8("Group: 이름 중복 ('") + g.name + "')"); continue; }
				if( schema.FindEnum(g.enumName) ) { errors.push_back(where + U8("Enum: PacketEnum의 enum과 이름이 같습니다 ('") + g.enumName + "')"); continue; }
				std::string why;
				if( !ValueFormat::ParseInt64(CellAt(row, cols, "Min"), g.minId, why) ) { errors.push_back(where + "Min: " + why); continue; }
				if( !SheetConverter::IsBlank(CellAt(row, cols, "Max")) && !ValueFormat::ParseInt64(CellAt(row, cols, "Max"), g.maxId, why) ) { errors.push_back(where + "Max: " + why); continue; }
				if( g.minId < 1 || g.maxId > 65535 || g.minId > g.maxId ) { errors.push_back(where + U8("Min/Max: 1..65535 범위에서 Min <= Max 여야 합니다")); continue; }
				for( const SPacketGroup& o : schema.groups )
					if( !(g.maxId < o.minId || o.maxId < g.minId) ) { errors.push_back(where + U8("Min/Max: 그룹 '") + o.name + U8("'과 번호 구간이 겹칩니다")); break; }
				schema.groups.push_back(std::move(g));
			}
		}
	}

	// ---- Packet
	std::set<std::string> rejected;		// 오류로 빠진 패킷 이름. 그 패킷의 PacketField 행마다 "없는 패킷" 연쇄 오류를 내지 않는다
	if( const TRows* rows = find(kPacketSheet) )
	{
		const std::string sheet = kPacketSheet;
		std::map<std::string, size_t> cols;
		if( ReadColumns(sheet, *rows, { "Name", "Group", "Id", "Cs", "Summary", "Comment", "Detail" }, { "Name", "Group", "Id", "Comment" }, cols, errors) )
		{
			for( const SDataRow& kv : DataRows(*rows) )
			{
				const TRow& row = kv.second;
				const std::string where = SheetConverter::RowLabel(sheet, kv.first);
				SPacketDef p;
				p.name = Text(CellAt(row, cols, "Name"));
				p.group = Text(CellAt(row, cols, "Group"));
				p.cs = Text(CellAt(row, cols, "Cs"));
				if( p.cs.empty() ) p.cs = "None";
				p.summary = Text(CellAt(row, cols, "Summary"));
				p.comment = MultiLine(CellAt(row, cols, "Comment"));
				p.detail = MultiLine(CellAt(row, cols, "Detail"));
				if( !IsPlainIdentifier(p.name) ) { errors.push_back(where + U8("Name: 식별자여야 합니다 ('") + p.name + "')"); continue; }
				if( schema.FindPacket(p.name) ) { errors.push_back(where + U8("Name: 이름 중복 ('") + p.name + "')"); continue; }
				if( p.cs != "None" && p.cs != "Send" && p.cs != "Recv" && p.cs != "Both" ) { errors.push_back(where + U8("Cs: None, Send, Recv, Both 중 하나여야 합니다 ('") + p.cs + "')"); rejected.insert(p.name); continue; }
				if( !OneLine(p.summary) ) { errors.push_back(where + U8("Summary: 한 줄이어야 합니다")); rejected.insert(p.name); continue; }
				std::string why;
				if( !ValueFormat::ParseInt64(CellAt(row, cols, "Id"), p.id, why) ) { errors.push_back(where + "Id: " + why); rejected.insert(p.name); continue; }
				schema.packets.push_back(std::move(p));
			}
		}
	}

	// ---- PacketField (Packet 시트 뒤에 읽는다)
	if( const TRows* rows = find(kPacketFieldSheet) )
	{
		const std::string sheet = kPacketFieldSheet;
		std::map<std::string, size_t> cols;
		if( ReadColumns(sheet, *rows, { "Packet", "Name", "Type", "Len", "Enum", "Comment" }, { "Packet", "Name", "Type" }, cols, errors) )
		{
			for( const SDataRow& kv : DataRows(*rows) )
			{
				const TRow& row = kv.second;
				const std::string where = SheetConverter::RowLabel(sheet, kv.first);
				const std::string pname = Text(CellAt(row, cols, "Packet"));
				SPacketDef* p = schema.FindPacket(pname);
				if( !p ) { if( !rejected.count(pname) ) errors.push_back(where + U8("Packet: Packet 시트에 없는 패킷입니다 ('") + pname + "')"); continue; }

				SPacketField f;
				f.name = Text(CellAt(row, cols, "Name"));
				f.type = Text(CellAt(row, cols, "Type"));
				f.len = Text(CellAt(row, cols, "Len"));
				f.enumName = Text(CellAt(row, cols, "Enum"));
				f.comment = Text(CellAt(row, cols, "Comment"));
				if( !IsPlainIdentifier(f.name) ) { errors.push_back(where + U8("Name: 식별자여야 합니다 ('") + f.name + "')"); continue; }
				for( const SPacketField& o : p->fields )
					if( o.name == f.name ) { errors.push_back(where + U8("Name: ") + pname + U8("에 같은 이름의 필드가 있습니다 ('") + f.name + "')"); f.name.clear(); break; }
				if( f.name.empty() ) continue;
				if( !OneLine(f.comment) ) { errors.push_back(where + U8("Comment: 한 줄이어야 합니다")); continue; }
				const SPacketTypeInfo* ti = FindPacketTypeInfo(f.type);
				if( !ti ) { errors.push_back(where + U8("Type: ") + kPacketTypeNames + U8(" 중 하나여야 합니다 ('") + f.type + "')"); continue; }
				if( f.type == "char" )
				{
					if( f.len.empty() ) { errors.push_back(where + U8("Len: char 배열은 길이가 필요합니다")); continue; }
					size_t n = 0;
					if( std::all_of(f.len.begin(), f.len.end(), [](unsigned char c) { return AsciiChar::IsDigit(c); }) && f.len.size() < 6 ) n = static_cast<size_t>(std::stoul(f.len));
					else if( const SPacketConst* c = schema.FindConst(f.len) ) n = static_cast<size_t>(c->value);
					else { errors.push_back(where + U8("Len: 숫자 또는 PacketConst 이름이어야 합니다 ('") + f.len + "')"); continue; }
					if( n < 1 || n > 4096 ) { errors.push_back(where + U8("Len: 1..4096 이어야 합니다")); continue; }
					f.bytes = n;
				}
				else
				{
					if( !f.len.empty() ) { errors.push_back(where + U8("Len: char 이외에는 쓸 수 없습니다")); continue; }
					f.bytes = ti->bytes;
				}
				if( !f.enumName.empty() )
				{
					if( f.type != "uint8" && f.type != "uint16" && f.type != "uint32" ) { errors.push_back(where + U8("Enum: uint8, uint16, uint32 필드에만 쓸 수 있습니다")); continue; }
					const SEnumDef* e = schema.FindEnum(f.enumName);
					if( !e ) { errors.push_back(where + U8("Enum: PacketEnum에 없는 enum입니다 ('") + f.enumName + "')"); continue; }
					const SBaseInfo* eb = FindBaseInfo(e->base);
					if( !eb || eb->name != f.type ) { errors.push_back(where + U8("Enum: ") + f.enumName + U8("의 기반형(") + e->base + U8(")과 필드 타입(") + f.type + U8(")이 달라 와이어 크기가 어긋납니다")); continue; }
				}
				p->fields.push_back(std::move(f));
			}
		}
	}

	// ---- 서로의 참조와 크기
	std::map<int64_t, std::vector<const SPacketDef*>> byId;		// 번호 -> 앞서 나온 패킷들 (같은 번호 검사를 O(n log n)으로)
	for( SPacketDef& p : schema.packets )
	{
		const std::vector<const SPacketDef*> sameId = byId[p.id];
		byId[p.id].push_back(&p);
		const SPacketGroup* g = schema.FindGroup(p.group);
		if( !g ) { errors.push_back(std::string(kPacketSheet) + ": " + p.name + U8(": Group '") + p.group + U8("'이(가) PacketGroup 시트에 없습니다")); continue; }
		if( p.id < g->minId || p.id > g->maxId )
			errors.push_back(std::string(kPacketSheet) + ": " + p.name + U8(": Id ") + std::to_string(p.id) + U8("이(가) 그룹 ") + g->name + " (" + std::to_string(g->minId) + ".." + std::to_string(g->maxId) + U8(") 범위 밖입니다"));
		for( const SPacketDef* o : sameId )
			errors.push_back(std::string(kPacketSheet) + ": " + p.name + U8(": Id ") + std::to_string(p.id) + U8("이(가) ") + o->name + U8("과 같습니다"));
		p.size = kPacketHeaderBytes;
		for( const SPacketField& f : p.fields ) p.size += f.bytes;
		if( p.size > 65535 ) errors.push_back(std::string(kPacketSheet) + ": " + p.name + U8(": 패킷 크기가 65535를 넘습니다"));
		if( p.cs != "None" )
			for( const SPacketField& f : p.fields )
				if( f.type == "char" ) errors.push_back(std::string(kPacketFieldSheet) + ": " + p.name + "." + f.name + U8(": char 배열은 C# 생성(Cs=") + p.cs + U8(")을 지원하지 않습니다. Cs를 None으로 하세요"));
	}
	if( !schema.packets.empty() && schema.groups.empty() ) errors.push_back(std::string(kPacketGroupSheet) + U8(": Packet 시트가 있으면 PacketGroup 시트도 있어야 합니다"));
	return errors.size() == before;
}