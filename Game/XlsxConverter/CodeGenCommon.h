
//***************************************************************************
// CodeGenCommon.h : RowGenerator / PacketGenerator가 함께 쓰는 코드 생성 도우미
//
// C# 예약어 목록과 줄 끝 주석 접미사를 한곳에 둔다. 두 생성기가 따로 들고 있으면 한쪽만 고쳐져 어긋나기 쉽다.
//***************************************************************************

#ifndef CODEGENCOMMON_H
#define CODEGENCOMMON_H

#include <string>

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
	// @brief 줄 끝 주석("\t// 설명")을 만듭니다. 주석이 비어 있으면 빈 문자열입니다.
	//***************************************************************************
	inline std::string CommentTail(const std::string& comment)
	{
		return comment.empty() ? std::string() : "\t// " + comment;
	}
}

#endif // ndef CODEGENCOMMON_H