
//***************************************************************************
// PacketGenerator.cpp : 패킷 프로토콜 코드 생성 구현
//
//***************************************************************************

#include "pch.h"
#include "PacketGenerator.h"

#include <algorithm>
#include <set>

namespace
{
	const char* const kDivider = "//***************************************************************************\n";


	using CodeGenCommon::CsConstName;
	using CodeGenCommon::CsEnumName;
	using CodeGenCommon::Lines;
	using CodeGenCommon::Pascal;

	//***************************************************************************
	// @brief "@brief ... / @details ..." 블록 주석. 둘째 줄부터는 태그 뒤 글자에 맞춰 들여쓴다.
	//***************************************************************************
	std::string DocBlock(const std::string& brief, const std::string& detail)
	{
		if( brief.empty() && detail.empty() ) return std::string();		// 빈 설명 블록(구분선만 두 줄)을 만들지 않는다
		std::string s = kDivider;
		const auto emit = [&](const char* tag, const std::string& text, size_t indent)
			{
				bool first = true;
				for( const std::string& l : Lines(text) )
				{
					s += first ? std::string("// ") + tag + " " + l : "//" + std::string(indent, ' ') + l;
					s += "\n";
					first = false;
				}
			};
		if( !brief.empty() ) emit("@brief", brief, 8);
		if( !detail.empty() ) emit("@details", detail, 10);
		return s + kDivider;
	}

	// C++ 필드 타입 (Network의 uint8 같은 별칭을 쓴다. 서버 코드가 이미 쓰는 이름)
	// SPacketField.type은 ParsePacketSheets가 FindPacketTypeInfo로 검증한 값이므로 항상 표에 있다.
	const SPacketTypeInfo& TypeOf(const SPacketField& f)
	{
		return *FindPacketTypeInfo(f.type);
	}

	std::string CppFieldType(const SPacketField& f)
	{
		return TypeOf(f).cppType;
	}

	std::string CppConstType(const std::string& t)
	{
		if( t == "uint16" ) return "std::uint16_t";
		if( t == "uint32" ) return "std::uint32_t";
		return "std::size_t";
	}

	std::string CsConstType(const std::string& t)
	{
		if( t == "uint16" ) return "ushort";
		if( t == "uint32" ) return "uint";
		return "int";
	}

	//***************************************************************************
	// @brief 생성 C++ 코드에 이름 그대로 들어가는 것들(상수, enum과 멤버, 패킷 번호 enum, 패킷, 필드)이 C++ 키워드나 Windows 매크로와 겹치는지 확인합니다.
	// @details 겹치면 변환은 성공하는데 컴파일에서 터지므로 여기서 막는다. (Enum 시트는 RowGenerator가 같은 규칙으로 검사한다)
	//***************************************************************************
	void ValidateCppNames(const SPacketSchema& schema, std::vector<std::string>& errors)
	{
		const auto check = [&](const std::string& what, const std::string& name)
			{
				if( CodeGenCommon::IsCppReservedName(name) )
					errors.push_back(what + U8(": '") + name + U8("'은(는) C++ 키워드이거나 Windows 헤더가 매크로로 정의하는 이름이라 생성 코드에 쓸 수 없습니다"));
			};
		for( const SPacketConst& c : schema.consts ) check("PacketConst " + c.name, c.name);
		for( const SEnumDef& e : schema.enums )
		{
			check("PacketEnum " + e.name, e.name);
			for( const SEnumMember& m : e.members ) check("PacketEnum " + e.name + "." + m.name, m.name);
		}
		for( const SPacketGroup& g : schema.groups ) check("PacketGroup " + g.name + " Enum", g.enumName);
		for( const SPacketDef& p : schema.packets )
		{
			check("Packet " + p.name, p.name);
			for( const SPacketField& f : p.fields ) check("PacketField " + p.name + "." + f.name, f.name);
		}
	}

	std::string Header(const char* file, const char* desc)
	{
		return CodeGenCommon::CppBanner(file, CodeGenCommon::SourceNote(desc));
	}

	// 같은 입력이면 같은 출력: 정렬 없이 시트 순서 그대로 쓴다.

	std::string GenTypes(const SPacketSchema& schema)
	{
		std::string s = Header("ZonePacketTypes.gen.h", "PacketConst, PacketEnum, PacketGroup, Packet");
		s += "\n#ifndef ZONEPACKETTYPES_GEN_H\n#define ZONEPACKETTYPES_GEN_H\n\n#include <cstddef>\n#include <cstdint>\n";

		// 상수. Comment가 비어 있는 상수는 앞 상수의 설명 블록에 이어 붙는다.
		bool openBlock = false;
		s += "\n";
		for( size_t i = 0; i < schema.consts.size(); ++i )
		{
			const SPacketConst& c = schema.consts[i];
			if( !c.comment.empty() )
			{
				if( i > 0 ) s += "\n";
				s += DocBlock(c.comment, "");
				openBlock = true;
			}
			else if( !openBlock && i > 0 ) s += "\n";
			s += "constexpr " + CppConstType(c.type) + " " + c.name + " = " + std::to_string(c.value) + ";\n";
		}

		// 결과 코드 enum
		for( const SEnumDef& e : schema.enums )
		{
			s += "\n";
			if( !e.comment.empty() ) s += DocBlock(e.comment, "");
			s += CodeGenCommon::CppEnum(e, "");
		}

		// 패킷 번호 열거형 (그룹마다)
		for( const SPacketGroup& g : schema.groups )
		{
			s += "\n";
			if( !g.comment.empty() ) s += DocBlock(g.comment, "");
			s += "enum class " + g.enumName + " : std::uint16_t\n{\n";
			for( const SPacketDef& p : schema.packets )
				if( p.group == g.name ) s += "\t" + p.name + " = " + std::to_string(p.id) + "," + CodeGenCommon::CommentTail(p.summary) + "\n";
			s += "};\n";
		}
		return s + "\n#endif // ndef ZONEPACKETTYPES_GEN_H\n";
	}

	std::string GenStructs(const SPacketSchema& schema)
	{
		std::string s = Header("ZonePacketStructs.gen.h", "Packet, PacketField");
		s += "\n#ifndef ZONEPACKETSTRUCTS_GEN_H\n#define ZONEPACKETSTRUCTS_GEN_H\n\n#include <Network/Packet.h>\n#include \"ZonePacketTypes.gen.h\"\n\n";
		s += "#pragma pack(push, 1)\n";

		std::string lastGroup;
		for( const SPacketDef& p : schema.packets )
		{
			if( p.group != lastGroup )
			{
				lastGroup = p.group;
				const SPacketGroup* g = schema.FindGroup(p.group);
				if( g && !g->section.empty() )
				{
					s += "\n// ---------------------------------------------------------------------------\n";
					for( const std::string& l : Lines(g->section) ) s += "// " + l + "\n";
					s += "// ---------------------------------------------------------------------------\n";
				}
			}
			s += "\n" + DocBlock(p.comment, p.detail);
			s += "struct " + p.name + "Packet : PacketHeader\n{\n";
			for( const SPacketField& f : p.fields )
			{
				s += "\t" + CppFieldType(f) + "\t" + f.name;
				if( f.type == "char" ) s += "[" + f.len + "]";
				s += ";";
				std::string tail = f.comment;
				if( !f.enumName.empty() ) tail = tail.empty() ? f.enumName : tail;		// 설명이 없으면 의미를 나타내는 enum 이름을 적는다
				s += CodeGenCommon::CommentTail(tail) + "\n";
			}
			s += "};\n";
		}
		s += "\n#pragma pack(pop)\n\n";

		// 와이어 크기 고정: PacketField 시트와 구조체가 어긋나면 컴파일이 멈춘다.
		for( const SPacketDef& p : schema.packets )
			s += "static_assert(sizeof(" + p.name + "Packet) == " + std::to_string(p.size) + ", \"" + p.name + "Packet: wire size differs from the Packet sheet\");\n";
		return s + "\n#endif // ndef ZONEPACKETSTRUCTS_GEN_H\n";
	}

	// ---------------------------------------------------------------- C#

	std::string CsFieldType(const SPacketField& f)
	{
		if( !f.enumName.empty() ) return CsEnumName(f.enumName);
		return TypeOf(f).csType;
	}

	std::string CsReadExpr(const SPacketField& f)
	{
		const std::string raw = TypeOf(f).csRead;
		return f.enumName.empty() ? raw : "(" + CsFieldType(f) + ")" + raw;
	}

	std::string CsWriteStmt(const SPacketField& f, const std::string& value)
	{
		const SPacketTypeInfo& ti = TypeOf(f);
		std::string pattern = (!f.enumName.empty() && ti.csWriteEnum) ? ti.csWriteEnum : ti.csWrite;
		const size_t pos = pattern.find("{v}");
		return pattern.replace(pos, 3, value);		// 패턴마다 {v}는 하나다. (식이 값을 두 번 평가하지 않도록 표를 만든다)
	}

	std::string CsHeader(const char* file, const char* desc)
	{
		return CodeGenCommon::CsBanner(file, CodeGenCommon::SourceNote(desc));
	}

	// C# 주석은 한 줄씩 //로 쓴다.
	std::string CsComment(const std::string& text, const std::string& indent)
	{
		std::string s;
		for( const std::string& l : Lines(text) ) s += indent + "// " + l + "\n";
		return s;
	}

	std::string GenCsTypes(const SPacketSchema& schema, const std::string& ns)
	{
		std::string s = CsHeader("ProtocolTypes.gen.cs", "PacketConst, PacketEnum, PacketGroup, Packet");
		s += "\nnamespace " + ns + "\n{\n";

		// ProtocolConst는 partial: 시트에 없는 값(HeaderSize 등)은 손으로 쓴 파일이 같은 클래스에 더한다.
		s += "\tpublic static partial class ProtocolConst\n\t{\n";
		for( const SPacketConst& c : schema.consts )
		{
			if( !c.comment.empty() ) s += CsComment(c.comment, "\t\t");
			s += "\t\tpublic const " + CsConstType(c.type) + " " + CsConstName(c.name) + " = " + std::to_string(c.value) + ";\n";
		}
		s += "\t}\n";

		for( const SEnumDef& e : schema.enums )
		{
			s += "\n";
			if( !e.comment.empty() ) s += CsComment(e.comment, "\t");
			s += CodeGenCommon::CsEnum(e, CsEnumName(e.name), "\t");
		}
		for( const SPacketGroup& g : schema.groups )
		{
			s += "\n";
			if( !g.comment.empty() ) s += CsComment(g.comment, "\t");
			s += "\tpublic enum " + g.csEnum + " : ushort\n\t{\n";
			for( const SPacketDef& p : schema.packets )
				if( p.group == g.name ) s += "\t\t" + CodeGenCommon::CsIdent(p.name) + " = " + std::to_string(p.id) + "," + CodeGenCommon::CommentTail(p.summary) + "\n";
			s += "\t}\n";
		}
		return s + "}\n";
	}

	std::string GenCsPackets(const SPacketSchema& schema, const std::string& ns, std::vector<std::string>& errors)
	{
		std::string s = CsHeader("Packets.gen.cs", "Packet, PacketField");
		s += "\nusing System;\n\nnamespace " + ns + "\n{\n";

		const auto groupEnum = [&](const SPacketDef& p) { const SPacketGroup* g = schema.FindGroup(p.group); return g ? g->csEnum : std::string(); };

		// ---- Send: Build
		bool anySend = false;
		for( const SPacketDef& p : schema.packets ) if( p.cs == "Send" || p.cs == "Both" ) anySend = true;
		if( anySend )
		{
			s += "\t// 클라이언트가 서버로 보내는 패킷을 만든다.\n\tpublic static class Build\n\t{\n";
			for( const SPacketDef& p : schema.packets )
				if( p.cs == "Send" || p.cs == "Both" ) s += "\t\tpublic const int " + p.name + "Size = " + std::to_string(p.size) + ";\n";
			for( const SPacketDef& p : schema.packets )
			{
				if( p.cs != "Send" && p.cs != "Both" ) continue;
				std::string args;
				for( const SPacketField& f : p.fields )
				{
					if( CodeGenCommon::IsCsKeyword(f.name) ) { errors.push_back(std::string("PacketField: ") + p.name + "." + f.name + U8(": C# 예약어라 매개변수 이름으로 쓸 수 없습니다")); continue; }
					if( f.name == "w" ) { errors.push_back(std::string("PacketField: ") + p.name + "." + f.name + U8(": 매개변수 이름 w는 생성 코드의 PacketWriter 지역 변수와 겹칩니다")); continue; }
					args += (args.empty() ? "" : ", ") + CsFieldType(f) + " " + f.name;
				}
				s += "\n\t\tpublic static byte[] " + p.name + "(" + args + ")\n\t\t{\n";
				s += "\t\t\tPacketWriter w = new PacketWriter((ushort)" + groupEnum(p) + "." + p.name + ", " + p.name + "Size);\n";
				for( const SPacketField& f : p.fields ) s += "\t\t\t" + CsWriteStmt(f, f.name) + "\n";
				s += "\t\t\treturn w.Finish();\n\t\t}\n";
			}
			s += "\t}\n";
		}

		// ---- Recv: Parse / ToBytes
		for( const SPacketDef& p : schema.packets )
		{
			if( p.cs != "Recv" && p.cs != "Both" ) continue;
			std::set<std::string> members;
			s += "\n\tpublic sealed class " + p.name + "\n\t{\n";
			s += "\t\tpublic const int Size = " + std::to_string(p.size) + ";\n";
			// 같은 타입이 이어지는 필드는 한 줄에 선언한다.
			std::string run, runType;
			const auto flush = [&]() { if( !run.empty() ) { s += "\t\tpublic " + runType + " " + run + ";\n"; run.clear(); } };
			for( const SPacketField& f : p.fields )
			{
				const std::string m = Pascal(f.name), t = CsFieldType(f);
				if( m == p.name || m == "Size" || m == "Parse" || m == "ToBytes" || !members.insert(m).second )
				{
					errors.push_back(std::string("PacketField: ") + p.name + "." + f.name + U8(": C# 멤버 이름 ") + m + U8("이(가) 겹칩니다"));
					continue;
				}
				if( t != runType ) { flush(); runType = t; }
				run += (run.empty() ? "" : ", ") + m;
			}
			flush();
			s += "\n\t\tpublic static " + p.name + " Parse(byte[] d)\n\t\t{\n";
			s += "\t\t\tPacketReader r = new PacketReader(d, ProtocolConst.HeaderSize);\n";
			s += "\t\t\t" + p.name + " p = new " + p.name + "();\n";
			for( const SPacketField& f : p.fields ) s += "\t\t\tp." + Pascal(f.name) + " = " + CsReadExpr(f) + ";\n";
			s += "\t\t\treturn p;\n\t\t}\n";
			s += "\n\t\t// 서버 쪽 레이아웃으로 직렬화한다 (테스트의 가짜 서버와 오프라인 데모가 쓴다)\n\t\tpublic byte[] ToBytes()\n\t\t{\n";
			s += "\t\t\tPacketWriter w = new PacketWriter((ushort)" + groupEnum(p) + "." + p.name + ", Size);\n";
			for( const SPacketField& f : p.fields ) s += "\t\t\t" + CsWriteStmt(f, Pascal(f.name)) + "\n";
			s += "\t\t\treturn w.Finish();\n\t\t}\n\t}\n";
		}
		return s + "}\n";
	}
}

namespace PacketGenerator
{
	bool Generate(const SPacketSchema& schema, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors)
	{
		if( schema.empty() ) return true;
		const size_t before = errors.size();
		ValidateCppNames(schema, errors);
		if( errors.size() != before ) return false;
		out.push_back({ "ZonePacketTypes.gen.h", GenTypes(schema) });
		out.push_back({ "ZonePacketStructs.gen.h", GenStructs(schema) });
		return true;
	}

	bool GenerateCs(const SPacketSchema& schema, const std::string& ns, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors, const std::set<std::string>* otherTypeNames)
	{
		if( schema.empty() ) return true;
		const size_t before = errors.size();
		if( !CodeGenCommon::IsValidNamespace(ns) )
		{
			errors.push_back(U8("패킷 C# 네임스페이스 '") + ns + U8("': 형식이 아닙니다 (--proto-cs-namespace, 없으면 --cs-namespace)"));
			return false;
		}

		// 같은 네임스페이스에 만들어지는 형식 이름끼리 겹치면 C# 컴파일이 깨지므로 미리 막는다.
		{
			bool anySend = false;
			for( const SPacketDef& p : schema.packets ) if( p.cs == "Send" || p.cs == "Both" ) anySend = true;
			std::set<std::string> names = { "ProtocolConst" };
			if( anySend ) names.insert("Build");
			const auto add = [&](const std::string& name, const char* what)
				{
					if( !names.insert(name).second )
						errors.push_back(std::string(what) + " " + name + U8(": C# 형식 이름이 패킷 프로토콜의 다른 형식(ProtocolConst, Build, enum, 패킷 클래스)과 겹칩니다"));
					else if( otherTypeNames && otherTypeNames->count(name) )
						errors.push_back(std::string(what) + " " + name + U8(": C# 형식 이름이 같은 네임스페이스(") + ns + U8(")의 시트 클래스/GameConsts/Enum 시트의 enum과 겹칩니다"));
				};
			for( const SEnumDef& e : schema.enums ) add(CsEnumName(e.name), "PacketEnum");
			for( const SPacketGroup& g : schema.groups ) add(g.csEnum, "PacketGroup");
			for( const SPacketDef& p : schema.packets )
			{
				if( p.cs == "None" ) continue;
				if( CodeGenCommon::IsCsKeyword(p.name) ) errors.push_back(std::string("Packet ") + p.name + U8(": C# 예약어라 클래스/메서드 이름으로 쓸 수 없습니다"));
				if( p.cs == "Recv" || p.cs == "Both" ) add(p.name, "Packet");
			}
			if( errors.size() != before ) return false;
		}
		out.push_back({ "ProtocolTypes.gen.cs", GenCsTypes(schema, ns) });
		out.push_back({ "Packets.gen.cs", GenCsPackets(schema, ns, errors) });
		return errors.size() == before;
	}

	std::string Sizes(const SPacketSchema& schema)
	{
		std::string s;
		for( const SPacketDef& p : schema.packets ) s += p.name + "Packet " + std::to_string(p.size) + "\n";
		return s;
	}
}