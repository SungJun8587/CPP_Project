
//***************************************************************************
// PacketGenerator.cpp : 패킷 프로토콜 코드 생성 구현
//
//***************************************************************************

#include "pch.h"
#include "PacketGenerator.h"
#include "Utf8Literal.h"
#include "CodeGenCommon.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace
{
	const char* const kDivider = "//***************************************************************************\n";


	std::string Pascal(const std::string& s)
	{
		std::string r = s;
		if( !r.empty() ) r[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[0])));
		return r;
	}

	// 열거형 이름의 C# 형태: 앞의 'E' + 대문자(EGatewayResult)이면 E를 뗀다.
	std::string CsEnumName(const std::string& name)
	{
		if( name.size() > 1 && name[0] == 'E' && std::isupper(static_cast<unsigned char>(name[1])) ) return name.substr(1);
		return name;
	}

	// 상수 이름의 C# 형태: 앞의 'k' + 대문자(kMaxClientPacketBytes)이면 k를 뗀다.
	std::string CsConstName(const std::string& name)
	{
		if( name.size() > 1 && name[0] == 'k' && std::isupper(static_cast<unsigned char>(name[1])) ) return name.substr(1);
		return name;
	}

	std::vector<std::string> Lines(const std::string& text)
	{
		std::vector<std::string> v;
		size_t pos = 0;
		while( pos <= text.size() )
		{
			size_t e = text.find('\n', pos);
			if( e == std::string::npos ) e = text.size();
			v.push_back(text.substr(pos, e - pos));
			pos = e + 1;
		}
		return v;
	}

	//***************************************************************************
	// @brief "@brief ... / @details ..." 블록 주석. 둘째 줄부터는 태그 뒤 글자에 맞춰 들여쓴다.
	//***************************************************************************
	std::string DocBlock(const std::string& brief, const std::string& detail)
	{
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
	std::string CppFieldType(const SPacketField& f)
	{
		if( f.type == "bool8" ) return "uint8";
		return f.type;
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

	std::string Header(const char* file, const char* desc)
	{
		return std::string(kDivider) + "// " + file + U8(" : 자동 생성 파일 - 직접 수정하지 마세요.\n//\n// XlsxConverter가 GameData.xlsx의 ") + desc + U8(" 시트로부터 만듭니다.\n") + kDivider;
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
			const SBaseInfo* bi = FindBaseInfo(e.base);
			s += "\n";
			if( !e.comment.empty() ) s += DocBlock(e.comment, "");
			s += "enum class " + e.name + " : " + bi->cppType + "\n{\n";
			for( const SEnumMember& m : e.members )
				s += "\t" + m.name + " = " + std::to_string(m.value) + "," + CodeGenCommon::CommentTail(m.comment) + "\n";
			s += "};\n";
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

	struct SCsType { const char* read; const char* csType; };

	std::string CsFieldType(const SPacketField& f, const SPacketSchema& schema)
	{
		if( !f.enumName.empty() ) return CsEnumName(f.enumName);
		if( f.type == "uint8" ) return "byte";
		if( f.type == "bool8" ) return "bool";
		if( f.type == "uint16" ) return "ushort";
		if( f.type == "uint32" ) return "uint";
		if( f.type == "int32" ) return "int";
		if( f.type == "uint64" ) return "ulong";
		return "float";
	}

	std::string CsReadExpr(const SPacketField& f, const SPacketSchema& schema)
	{
		const std::string type = CsFieldType(f, schema);
		std::string raw;
		if( f.type == "uint8" || f.type == "bool8" ) raw = "r.U8()";
		else if( f.type == "uint16" ) raw = "r.U16()";
		else if( f.type == "uint32" ) raw = "r.U32()";
		else if( f.type == "int32" ) raw = "r.I32()";
		else if( f.type == "uint64" ) raw = "r.U64()";
		else raw = "r.F32()";
		if( f.type == "bool8" ) return raw + " != 0";
		return f.enumName.empty() ? raw : "(" + type + ")" + raw;
	}

	std::string CsWriteStmt(const SPacketField& f, const std::string& value)
	{
		if( f.type == "bool8" ) return "w.U8(" + value + " ? (byte)1 : (byte)0);";
		if( f.type == "uint8" ) return "w.U8((byte)" + value + ");";
		if( f.type == "uint16" ) return "w.U16((ushort)" + value + ");";
		if( f.type == "uint32" ) return f.enumName.empty() ? "w.U32(" + value + ");" : "w.U32((uint)" + value + ");";
		if( f.type == "int32" ) return "w.U32(unchecked((uint)" + value + "));";
		if( f.type == "uint64" ) return "w.U64(" + value + ");";
		return "w.F32(" + value + ");";
	}

	std::string CsHeader(const char* file, const char* desc)
	{
		return std::string("// <auto-generated>\n// ") + file + U8(" : 자동 생성 파일 - 직접 수정하지 마세요.\n// XlsxConverter가 GameData.xlsx의 ") + desc + U8(" 시트로부터 만듭니다.\n// </auto-generated>\n");
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
			const SBaseInfo* bi = FindBaseInfo(e.base);
			s += "\n";
			if( !e.comment.empty() ) s += CsComment(e.comment, "\t");
			s += std::string("\tpublic enum ") + CsEnumName(e.name) + " : " + bi->csType + "\n\t{\n";
			for( const SEnumMember& m : e.members )
				s += "\t\t" + m.name + " = " + std::to_string(m.value) + "," + CodeGenCommon::CommentTail(m.comment) + "\n";
			s += "\t}\n";
		}
		for( const SPacketGroup& g : schema.groups )
		{
			s += "\n";
			if( !g.comment.empty() ) s += CsComment(g.comment, "\t");
			s += "\tpublic enum " + g.csEnum + " : ushort\n\t{\n";
			for( const SPacketDef& p : schema.packets )
				if( p.group == g.name ) s += "\t\t" + p.name + " = " + std::to_string(p.id) + "," + CodeGenCommon::CommentTail(p.summary) + "\n";
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
					args += (args.empty() ? "" : ", ") + CsFieldType(f, schema) + " " + f.name;
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
				const std::string m = Pascal(f.name), t = CsFieldType(f, schema);
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
			for( const SPacketField& f : p.fields ) s += "\t\t\tp." + Pascal(f.name) + " = " + CsReadExpr(f, schema) + ";\n";
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
		out.push_back({ "ZonePacketTypes.gen.h", GenTypes(schema) });
		out.push_back({ "ZonePacketStructs.gen.h", GenStructs(schema) });
		return errors.empty();
	}

	bool GenerateCs(const SPacketSchema& schema, const std::string& ns, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors)
	{
		if( schema.empty() ) return true;
		const size_t before = errors.size();
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