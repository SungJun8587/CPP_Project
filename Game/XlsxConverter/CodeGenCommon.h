
//***************************************************************************
// CodeGenCommon.h : RowGenerator / PacketGenerator가 함께 쓰는 코드 생성 도우미
//
// C#/C++ 예약어와 이름 변환, 줄 단위 문자열 처리, enum 선언 출력, 줄 끝 주석 접미사를 한곳에 둔다.
// 두 생성기가 따로 들고 있으면 한쪽만 고쳐져 어긋나기 쉽다.
//***************************************************************************

#ifndef CODEGENCOMMON_H
#define CODEGENCOMMON_H

#include "RowSchema.h"
#include "Utf8Literal.h"

#include <string>
#include <vector>

namespace CodeGenCommon
{
	//***************************************************************************
	// @brief C# 예약어인지 확인합니다. (식별자로 쓰려면 @ 접두가 필요하고, 매개변수 이름으로는 쓸 수 없다)
	//***************************************************************************
	inline bool IsCsKeyword(const std::string& s)
	{
		static const char* const kCsKeywords[] =
		{
			"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
			"decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
			"fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
			"long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected",
			"public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string",
			"struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
			"using", "virtual", "void", "volatile", "while",
		};
		for( const char* k : kCsKeywords )
			if( s == k )
				return true;
		return false;
	}

	//***************************************************************************
	// @brief 식별자로 쓰려면 @ 접두가 필요한 C# 이름이면 붙여 돌려줍니다.
	//***************************************************************************
	inline std::string CsIdent(const std::string& s)
	{
		return IsCsKeyword(s) ? "@" + s : s;
	}

	//***************************************************************************
	// @brief C++ 예약어(C++20)와, Windows 헤더가 매크로로 정의하는 소문자 이름(min, max, near, far, interface, small)인지 확인합니다.
	// @details 멤버 이름은 이 이름에 걸리면 '_'를 붙여 쓰고(RowGenerator::MemberName), enum/필드 이름처럼 그대로 써야 하는 곳은 IsCppReservedName으로 막는다.
	//***************************************************************************
	inline bool IsCppKeyword(const std::string& s)
	{
		static const char* const kKeywords[] =
		{
			"alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t",
			"char16_t", "char32_t", "class", "co_await", "co_return", "co_yield", "compl", "concept", "const", "const_cast", "consteval",
			"constexpr", "constinit", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
			"explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
			"namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
			"register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
			"struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid", "typename", "union",
			"unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",
			"min", "max", "near", "far", "interface", "small",
		};
		for( const char* k : kKeywords )
			if( s == k )
				return true;
		return false;
	}

	//***************************************************************************
	// @brief Windows 헤더(windows.h)가 매크로로 정의하는 이름인지 확인합니다. (대소문자를 구분한다: ERROR는 걸리고 Error는 안 걸린다)
	// @details enum 멤버나 패킷 이름이 이 이름이면 매크로가 먼저 치환되어 컴파일이 깨진다.
	//***************************************************************************
	inline bool IsCppMacroName(const std::string& s)
	{
		static const char* const kMacros[] =
		{
			"ERROR", "NO_ERROR", "IN", "OUT", "OPTIONAL", "TRUE", "FALSE", "NULL", "DELETE", "CONST", "VOID", "NEAR", "FAR",
			"ABSOLUTE", "RELATIVE", "TRANSPARENT", "OPAQUE", "INFINITE", "IGNORE", "CALLBACK", "PASCAL", "CDECL", "WINAPI", "APIENTRY",
			// 문자셋에 따라 A/W 이름으로 바뀌는 API 이름
			"GetMessage", "SendMessage", "PostMessage", "DrawText", "LoadString", "CreateFile", "DeleteFile", "CopyFile", "MoveFile",
			"FindResource", "GetObject", "GetClassName", "CreateEvent", "CreateMutex",
		};
		for( const char* k : kMacros )
			if( s == k )
				return true;
		return false;
	}

	//***************************************************************************
	// @brief 생성 C++ 코드에서 enum, 멤버, 구조체 필드, 패킷 이름으로 그대로 쓸 수 없는 이름인지 확인합니다. (키워드 또는 Windows 매크로)
	//***************************************************************************
	inline bool IsCppReservedName(const std::string& s)
	{
		return IsCppKeyword(s) || IsCppMacroName(s);
	}

	//***************************************************************************
	// @brief 첫 글자를 대문자로 바꿉니다. (C# 멤버 이름)
	//***************************************************************************
	inline std::string Pascal(const std::string& s)
	{
		std::string r = s;
		if( !r.empty() ) r[0] = AsciiChar::ToUpper(static_cast<unsigned char>(r[0]));
		return r;
	}

	//***************************************************************************
	// @brief 열거형 이름의 C# 형태: 앞의 'E' + 대문자(EGatewayResult)이면 E를 뗍니다.
	//***************************************************************************
	inline std::string CsEnumName(const std::string& name)
	{
		if( name.size() > 1 && name[0] == 'E' && AsciiChar::IsUpper(static_cast<unsigned char>(name[1])) ) return name.substr(1);
		return name;
	}

	//***************************************************************************
	// @brief 상수 이름의 C# 형태: 앞의 'k' + 대문자(kMaxClientPacketBytes)이면 k를 뗍니다.
	//***************************************************************************
	inline std::string CsConstName(const std::string& name)
	{
		if( name.size() > 1 && name[0] == 'k' && AsciiChar::IsUpper(static_cast<unsigned char>(name[1])) ) return name.substr(1);
		return name;
	}

	//***************************************************************************
	// @brief C# 네임스페이스 형식(점으로 구분된 식별자들)인지 확인합니다.
	//***************************************************************************
	inline bool IsValidNamespace(const std::string& ns)
	{
		if( ns.empty() ) return false;
		size_t b = 0;
		while( b <= ns.size() )
		{
			size_t e = ns.find('.', b);
			if( e == std::string::npos ) e = ns.size();
			if( !IsPlainIdentifier(ns.substr(b, e - b)) ) return false;
			b = e + 1;
		}
		return true;
	}

	//***************************************************************************
	// @brief 줄 단위로 나눕니다. (끝의 빈 줄도 한 줄로 센다: "a\nb" -> {"a", "b"}, "" -> {""})
	//***************************************************************************
	inline std::vector<std::string> Lines(const std::string& text)
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
	// @brief 생성 파일의 머리말에 들어가는 표식. 낡은 생성 파일을 지울 때(XlsxConverter.cpp) 이 표식이 있는 파일만 대상으로 삼는다.
	//***************************************************************************
	inline const char* GeneratedMarker()
	{
		return U8("자동 생성 파일");
	}

	//***************************************************************************
	// @brief "XlsxConverter가 GameData 엑셀의 <what> 시트로부터 만듭니다." 안내 줄.
	// @param what 시트 이름(들)
	//***************************************************************************
	inline std::string SourceNote(const std::string& what)
	{
		return std::string(U8("XlsxConverter가 GameData 엑셀의 ")) + what + U8(" 시트로부터 만듭니다.");
	}

	namespace Detail
	{
		// "a\nb" -> prefix + "a\n" + prefix + "b\n"
		inline std::string PrefixLines(const std::string& text, const char* prefix)
		{
			std::string out;
			for( const std::string& line : Lines(text) )
				out += prefix + line + "\n";
			return out;
		}
	}

	//***************************************************************************
	// @brief C++ 생성 파일의 머리말 블록. 구분선 사이에 "<file> : 자동 생성 파일 ..." 한 줄, 빈 주석 줄, 설명 줄들이 온다.
	// @param description 설명. 여러 줄은 \n으로 구분한다
	//***************************************************************************
	inline std::string CppBanner(const std::string& file, const std::string& description)
	{
		const std::string divider = "//***************************************************************************\n";
		return divider + "// " + file + " : " + GeneratedMarker() + U8(" - 직접 수정하지 마세요.\n//\n") + Detail::PrefixLines(description, "// ") + divider;
	}

	//***************************************************************************
	// @brief C# 생성 파일의 머리말 블록. (<auto-generated> 표식)
	//***************************************************************************
	inline std::string CsBanner(const std::string& file, const std::string& description)
	{
		return "// <auto-generated>\n// " + file + " : " + GeneratedMarker() + U8(" - 직접 수정하지 마세요.\n") + Detail::PrefixLines(description, "// ") + "// </auto-generated>\n";
	}

	//***************************************************************************
	// @brief 줄 끝 주석("\t// 설명")을 만듭니다. 주석이 비어 있으면 빈 문자열입니다.
	//***************************************************************************
	inline std::string CommentTail(const std::string& comment)
	{
		return comment.empty() ? std::string() : "\t// " + comment;
	}

	//***************************************************************************
	// @brief C++ enum class 선언 한 개를 만듭니다. (Enum 시트와 PacketEnum 시트가 같은 모양을 쓴다)
	// @param indent 선언 앞의 들여쓰기(탭). 멤버는 한 단계 더 들여쓴다
	//***************************************************************************
	inline std::string CppEnum(const SEnumDef& e, const std::string& indent)
	{
		const SBaseInfo* bi = FindBaseInfo(e.base);
		std::string s = indent + "enum class " + e.name + " : " + bi->cppType + "\n" + indent + "{\n";
		for( const SEnumMember& m : e.members )
			s += indent + "\t" + m.name + " = " + std::to_string(m.value) + "," + CommentTail(m.comment) + "\n";
		return s + indent + "};\n";
	}

	//***************************************************************************
	// @brief C# enum 선언 한 개를 만듭니다. 멤버 이름이 C# 예약어면 @를 붙인다.
	// @param csName C# 쪽 enum 이름 (호출하는 쪽이 정한다: Enum 시트는 그대로, PacketEnum은 CsEnumName)
	//***************************************************************************
	inline std::string CsEnum(const SEnumDef& e, const std::string& csName, const std::string& indent)
	{
		const SBaseInfo* bi = FindBaseInfo(e.base);
		std::string s = indent + "public enum " + csName + " : " + bi->csType + "\n" + indent + "{\n";
		for( const SEnumMember& m : e.members )
			s += indent + "\t" + CsIdent(m.name) + " = " + std::to_string(m.value) + "," + CommentTail(m.comment) + "\n";
		return s + indent + "}\n";
	}
}

#endif // ndef CODEGENCOMMON_H