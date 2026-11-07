
//***************************************************************************
// PacketSchema.h : 패킷 프로토콜 정의 시트(PacketEnum, PacketConst, PacketGroup, Packet, PacketField)에서 읽은 내용
//
//***************************************************************************

#ifndef PACKETSCHEMA_H
#define PACKETSCHEMA_H

#include "RowSchema.h"

#include <cstdint>
#include <string>
#include <vector>

//***************************************************************************
// @struct SPacketTypeInfo
// @brief PacketField 시트의 Type 하나에 대한 모든 매핑. 타입을 추가/변경할 때는 이 표만 고친다. (와이어 크기, C++/C# 타입, C# 읽기/쓰기 식)
// @details csWrite/csWriteEnum의 {v}는 값 식으로 치환된다. csWriteEnum은 enum으로 선언한 필드의 쓰기 식이며 nullptr이면 csWrite와 같다.
//          char 배열은 C# 생성을 지원하지 않으므로 C# 항목이 nullptr이다.
//***************************************************************************
struct SPacketTypeInfo
{
	const char* name;
	size_t			bytes;
	const char* cppType;
	const char* csType;
	const char* csRead;
	const char* csWrite;
	const char* csWriteEnum;
};

inline const SPacketTypeInfo* FindPacketTypeInfo(const std::string& name)
{
	static const SPacketTypeInfo kTable[] =
	{
		//	name		bytes	cpp			cs			read			write							writeEnum
		{ "uint8",		1,		"uint8",	"byte",		"r.U8()",		"w.U8((byte){v});",				nullptr },
		{ "bool8",		1,		"uint8",	"bool",		"r.U8() != 0",	"w.U8({v} ? (byte)1 : (byte)0);",	nullptr },
		{ "uint16",		2,		"uint16",	"ushort",	"r.U16()",		"w.U16((ushort){v});",			nullptr },
		{ "uint32",		4,		"uint32",	"uint",		"r.U32()",		"w.U32({v});",					"w.U32((uint){v});" },
		{ "int32",		4,		"int32",	"int",		"r.I32()",		"w.U32(unchecked((uint){v}));",	nullptr },
		{ "uint64",		8,		"uint64",	"ulong",	"r.U64()",		"w.U64({v});",					nullptr },
		{ "float",		4,		"float",	"float",	"r.F32()",		"w.F32({v});",					nullptr },
		{ "char",		1,		"char",		nullptr,	nullptr,		nullptr,						nullptr },
	};
	for( const SPacketTypeInfo& t : kTable )
		if( name == t.name ) return &t;
	return nullptr;
}

//***************************************************************************
// @struct SPacketConst
// @brief PacketConst 시트의 한 행. 프로토콜 전역 상수(버전, 크기 상한, 이름 필드 길이 등).
//***************************************************************************
struct SPacketConst
{
	std::string		name;
	std::string		type;				// uint16, uint32, size
	uint64_t		value = 0;
	std::string		comment;			// 비우면 앞 상수의 설명에 이어서 쓴다 (여러 줄은 \n)
};

//***************************************************************************
// @struct SPacketGroup
// @brief PacketGroup 시트의 한 행. 패킷 번호 구간 하나와 그 구간의 번호 열거형.
//***************************************************************************
struct SPacketGroup
{
	std::string		name;				// Packet 시트의 Group 열이 가리키는 이름
	std::string		enumName;			// C++ 패킷 번호 열거형 이름 (예: EGatewayPacketType)
	std::string		csEnum;				// C# 열거형 이름
	int64_t			minId = 0;			// 이 구간의 번호 범위
	int64_t			maxId = 65535;
	std::string		section;			// 구조체 파일에서 이 구간을 시작하는 구분 주석
	std::string		comment;			// 열거형 설명
};

//***************************************************************************
// @struct SPacketField
// @brief PacketField 시트의 한 행. 패킷 본문의 필드 하나(PacketHeader 다음부터 시트 순서대로 이어 붙는다).
//***************************************************************************
struct SPacketField
{
	std::string		name;				// C++ 멤버 이름 (C#에서는 첫 글자만 대문자로)
	std::string		type;				// FindPacketTypeInfo의 이름: uint8, bool8, uint16, uint32, int32, uint64, float, char
	std::string		len;				// char 배열 길이: 숫자 또는 PacketConst 이름
	std::string		enumName;			// 값의 의미를 나타내는 PacketEnum 이름 (C++ 멤버는 정수형 그대로, C#은 enum으로 변환)
	std::string		comment;
	size_t			bytes = 0;			// 계산된 크기
};

//***************************************************************************
// @struct SPacketDef
// @brief Packet 시트의 한 행과 그 패킷의 필드들.
//***************************************************************************
struct SPacketDef
{
	std::string		name;				// 예: GwLoginReq (C++ 구조체는 <name>Packet)
	std::string		group;
	int64_t			id = 0;
	std::string		cs;					// None(기본), Send(클라이언트가 보냄), Recv(클라이언트가 받음), Both
	std::string		summary;			// 패킷 번호 열거형의 멤버 설명
	std::string		comment;			// 구조체 @brief (여러 줄은 \n)
	std::string		detail;				// 구조체 @details (선택, 여러 줄은 \n)
	std::vector<SPacketField>	fields;
	size_t			size = 0;			// 헤더를 포함한 전체 크기 (계산됨)
};

//***************************************************************************
// @struct SPacketSchema
// @brief 패킷 프로토콜 정의 전체. 시트가 하나도 없으면 empty()다.
//***************************************************************************
struct SPacketSchema
{
	std::vector<SEnumDef>		enums;
	std::vector<SPacketConst>	consts;
	std::vector<SPacketGroup>	groups;
	std::vector<SPacketDef>		packets;

	bool empty() const { return enums.empty() && consts.empty() && groups.empty() && packets.empty(); }

	const SPacketGroup* FindGroup(const std::string& name) const
	{
		for( const SPacketGroup& g : groups ) if( g.name == name ) return &g;
		return nullptr;
	}
	const SEnumDef* FindEnum(const std::string& name) const
	{
		for( const SEnumDef& e : enums ) if( e.name == name ) return &e;
		return nullptr;
	}
	const SPacketConst* FindConst(const std::string& name) const
	{
		for( const SPacketConst& c : consts ) if( c.name == name ) return &c;
		return nullptr;
	}
	SPacketDef* FindPacket(const std::string& name)
	{
		for( SPacketDef& p : packets ) if( p.name == name ) return &p;
		return nullptr;
	}
};

#endif // ndef PACKETSCHEMA_H