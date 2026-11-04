
//***************************************************************************
// PacketGenerator.h : 패킷 정의 시트 -> C++ 헤더, C# 코드, 크기 표 생성
//
//***************************************************************************

#ifndef PACKETGENERATOR_H
#define PACKETGENERATOR_H

#include "PacketSchema.h"
#include "RowGenerator.h"

#include <string>
#include <vector>

//***************************************************************************
// @brief PacketEnum, PacketConst, PacketGroup, Packet, PacketField 시트로 프로토콜 코드를 만듭니다.
// @details 같은 입력이면 항상 같은 바이트가 나온다. 와이어 레이아웃(필드 순서, 크기)은 PacketField 시트가 유일한 원천이고,
//          C++ 구조체의 sizeof는 static_assert로, C# Size 상수는 같은 계산값으로 맞춰진다.
//***************************************************************************
namespace PacketGenerator
{
	//***************************************************************************
	// @brief C++ 파일 2개를 만듭니다.
	//   ZonePacketTypes.gen.h    상수(PacketConst), enum(PacketEnum), 패킷 번호 열거형(PacketGroup + Packet)
	//   ZonePacketStructs.gen.h  #pragma pack(1) 구조체 <Name>Packet : PacketHeader 와 sizeof static_assert
	// @return 오류가 없으면 true
	//***************************************************************************
	bool Generate(const SPacketSchema& schema, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors);

	//***************************************************************************
	// @brief C# 파일 2개를 만듭니다.
	//   ProtocolTypes.gen.cs  상수(ProtocolConst, partial), enum, 패킷 번호 열거형
	//   Packets.gen.cs        Cs=Send/Both: Build.<Name>(...),  Cs=Recv/Both: <Name>.Parse(byte[]) / ToBytes()
	// @param ns C# 네임스페이스
	//***************************************************************************
	bool GenerateCs(const SPacketSchema& schema, const std::string& ns, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors);

	//***************************************************************************
	// @brief "<구조체 이름> <바이트>" 줄 목록. C++ sizeof와 대조하는 용도(테스트 입력)다.
	//***************************************************************************
	std::string Sizes(const SPacketSchema& schema);
}

#endif // ndef PACKETGENERATOR_H
