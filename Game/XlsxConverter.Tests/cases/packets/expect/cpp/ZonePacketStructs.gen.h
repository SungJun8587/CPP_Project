//***************************************************************************
// ZonePacketStructs.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter이 GameData.xlsx의 Packet, PacketField 시트로부터 만듭니다.
//***************************************************************************

#ifndef ZONEPACKETSTRUCTS_GEN_H
#define ZONEPACKETSTRUCTS_GEN_H

#include <Network/Packet.h>
#include "ZonePacketTypes.gen.h"

#pragma pack(push, 1)

// ---------------------------------------------------------------------------
// 클라이언트 <-> 서버
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 로그인 요청
//***************************************************************************
struct DemoLoginReqPacket : PacketHeader
{
	uint64	charId;
	uint32	heroId;	// 선택한 영웅
};

//***************************************************************************
// @brief 로그인 응답
// @details success==1일 때만 sessionId가 유효
//***************************************************************************
struct DemoLoginResPacket : PacketHeader
{
	uint8	success;	// 1: 성공
	uint8	reason;	// EDemoResult
	uint64	sessionId;
	int32	delta;
};

//***************************************************************************
// @brief 핑
//***************************************************************************
struct DemoPingPacket : PacketHeader
{
	uint16	seq;
	float	x;
	float	y;
};

//***************************************************************************
// @brief 빈 패킷
//***************************************************************************
struct DemoLeavePacket : PacketHeader
{
};

// ---------------------------------------------------------------------------
// 서버 <-> 서버
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 서버 등록 요청
//***************************************************************************
struct DemoRegisterPacket : PacketHeader
{
	uint32	version;	// kDemoVersion
	char	serverName[kDemoNameBytes];	// UTF-8
	char	tag[8];
};

#pragma pack(pop)

static_assert(sizeof(DemoLoginReqPacket) == 16, "DemoLoginReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(DemoLoginResPacket) == 18, "DemoLoginResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(DemoPingPacket) == 14, "DemoPingPacket: wire size differs from the Packet sheet");
static_assert(sizeof(DemoLeavePacket) == 4, "DemoLeavePacket: wire size differs from the Packet sheet");
static_assert(sizeof(DemoRegisterPacket) == 32, "DemoRegisterPacket: wire size differs from the Packet sheet");

#endif // ndef ZONEPACKETSTRUCTS_GEN_H
