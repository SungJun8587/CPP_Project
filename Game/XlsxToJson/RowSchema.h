
//***************************************************************************
// RowSchema.h : 시트 헤더에서 읽은 필드 스키마 (JSON 변환기와 C++ 행 구조체 생성기가 공유)
//
//***************************************************************************

#ifndef ROWSCHEMA_H
#define ROWSCHEMA_H

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

//***************************************************************************
// @brief C++/C# 식별자로 쓸 수 있는 이름인지 확인합니다. (영문/숫자/_, 숫자로 시작 불가)
//***************************************************************************
inline bool IsPlainIdentifier(const std::string& s)
{
	if( s.empty() || std::isdigit(static_cast<unsigned char>(s[0])) ) return false;
	for( unsigned char c : s )
		if( !(std::isalnum(c) || c == '_') ) return false;
	return true;
}

//***************************************************************************
// @struct STypeInfo
// @brief 기본 타입(int, uint, float, string, bool) 하나에 대한 모든 매핑. 타입을 추가/변경할 때는 이 표만 고친다.
//***************************************************************************
struct STypeInfo
{
	const char* name;			// 엑셀 헤더/Const 시트에서 쓰는 이름
	const char* cppType;		// 행 구조체 멤버 타입
	const char* cppConstType;	// constexpr 상수 타입
	const char* csType;
	const char* getter;			// rapidjson 값 접근자
	const char* specType;		// EFieldType 열거자 (정수 계열은 범위와 함께 쓴다)
	const char* cppDefault;		// 멤버 기본 초기화 (빈 문자열이면 초기화 없음)
	bool			isInteger;		// 범위 [min..max]를 쓸 수 있는 타입
	int64_t			minV;			// isInteger일 때 표현 범위
	int64_t			maxV;
};

inline const STypeInfo* FindTypeInfo(const std::string& name)
{
	static const STypeInfo kTable[] =
	{
		//	name		cppType		cppConstType		csType		getter			specType	cppDefault		isInteger	min						max
		{ "int",	"int32",	"std::int32_t",		"int",		"GetInt()",		"Int",		" = 0",			true,		-2147483647ll - 1,		2147483647ll },
		{ "uint",	"uint32",	"std::uint32_t",	"uint",		"GetUint()",	"UInt",		" = 0",			true,		0,						4294967295ll },
		{ "float",	"double",	"double",			"double",	"GetDouble()",	"Number",	" = 0",			false,		0,						0 },
		{ "string",	"_tstring",	"const TCHAR*",		"string",	"GetString()",	"String",	"",				false,		0,						0 },
		{ "bool",	"bool",		"bool",				"bool",		"GetBool()",	"Bool",		" = false",		false,		0,						0 },
	};
	for( const STypeInfo& t : kTable )
		if( name == t.name ) return &t;
	return nullptr;
}

//***************************************************************************
// @struct SBaseInfo
// @brief Enum 기반형(Base 열) 하나에 대한 매핑.
//***************************************************************************
struct SBaseInfo
{
	const char* name;
	const char* cppType;
	const char* csType;
	int64_t			minV;
	int64_t			maxV;
};

inline const SBaseInfo* FindBaseInfo(const std::string& name)
{
	static const SBaseInfo kTable[] =
	{
		{ "uint8",	"std::uint8_t",		"byte",		0,						255 },
		{ "uint16",	"std::uint16_t",	"ushort",	0,						65535 },
		{ "uint32",	"std::uint32_t",	"uint",		0,						4294967295ll },
		{ "int32",	"std::int32_t",		"int",		-2147483647ll - 1,		2147483647ll },
	};
	for( const SBaseInfo& b : kTable )
		if( name == b.name ) return &b;
	return nullptr;
}

//***************************************************************************
// @struct SFieldDef
// @brief 헤더 칸 하나("Name:type[min..max]?")의 해석 결과.
//***************************************************************************
struct SFieldDef
{
	size_t			col = 0;			// 0-base 열 번호
	std::string		name;
	std::string		type;				// int, uint, float, string, bool, enum
	std::string		enumName;			// type이 enum일 때 Enum 시트에 정의된 enum 이름 (헤더에는 "Shape:ESkillShape"처럼 이름만 쓴다)
	bool			optional = false;	// '?' 접미사. 빈 칸이면 JSON 필드를 생략한다
	bool			hasMin = false;		// [min..max]의 min 지정 여부 (int, uint만)
	bool			hasMax = false;
	int64_t			minV = 0;
	int64_t			maxV = 0;
};

//***************************************************************************
// @struct SSheetSchema
// @brief 시트 하나의 스키마. 시트 이름이 곧 행 구조체 이름(S<Name>Row)과 JSON 파일 이름이 된다.
//***************************************************************************
struct SSheetSchema
{
	std::string				name;
	std::vector<SFieldDef>	fields;
};

//***************************************************************************
// @struct SEnumMember
// @brief Enum 시트의 한 행.
//***************************************************************************
struct SEnumMember
{
	std::string		name;
	int64_t			value = 0;
	std::string		comment;
};

//***************************************************************************
// @struct SEnumDef
// @brief Enum 시트에서 같은 Enum 이름을 가진 행들. 멤버 순서는 시트의 행 순서다.
//***************************************************************************
struct SEnumDef
{
	std::string					name;
	std::string					base = "uint8";		// uint8, uint16, uint32, int32
	std::vector<SEnumMember>	members;
};

//***************************************************************************
// @struct SConstDef
// @brief Const 시트의 한 행.
//***************************************************************************
struct SConstDef
{
	std::string		name;
	std::string		type;				// int, uint, float, string, bool
	std::string		literal;			// 값의 JSON 표기 (정수/실수/true/false는 C++·C# 리터럴과 같고, 문자열은 따옴표 포함)
	std::string		comment;
};

//***************************************************************************
// @struct SDefinitions
// @brief 데이터 시트와 별개로 엑셀이 정의하는 형식들. (Enum, Const 시트)
//***************************************************************************
struct SDefinitions
{
	std::vector<SEnumDef>	enums;
	std::vector<SConstDef>	consts;

	const SEnumDef* FindEnum(const std::string& name) const
	{
		for( const SEnumDef& e : enums )
			if( e.name == name ) return &e;
		return nullptr;
	}
};

#endif // ndef ROWSCHEMA_H