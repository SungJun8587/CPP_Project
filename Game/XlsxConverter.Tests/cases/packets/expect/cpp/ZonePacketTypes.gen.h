//***************************************************************************
// ZonePacketTypes.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter이 GameData.xlsx의 PacketConst, PacketEnum, PacketGroup, Packet 시트로부터 만듭니다.
//***************************************************************************

#ifndef ZONEPACKETTYPES_GEN_H
#define ZONEPACKETTYPES_GEN_H

#include <cstddef>
#include <cstdint>

//***************************************************************************
// @brief 프로토콜 버전
//***************************************************************************
constexpr std::uint32_t kDemoVersion = 3;
constexpr std::size_t kDemoNameBytes = 16;

//***************************************************************************
// @brief 크기 상한
//***************************************************************************
constexpr std::uint16_t kDemoMax = 4096;

//***************************************************************************
// @brief 데모 결과 코드
//***************************************************************************
enum class EDemoResult : std::uint8_t
{
	Ok = 0,	// 성공
	Fail = 1,	// 실패
	Busy = 5,	// 바쁨
};

enum class EDemoKind : std::uint16_t
{
	Small = 1,
};

//***************************************************************************
// @brief 클라이언트 패킷 번호
//***************************************************************************
enum class EClientPacketType : std::uint16_t
{
	DemoLoginReq = 1,	// Client -> Server, 로그인
	DemoLoginRes = 2,	// Server -> Client, 로그인 응답
	DemoPing = 3,
	DemoLeave = 4,
};

//***************************************************************************
// @brief 서버 패킷 번호
//***************************************************************************
enum class EServerPacketType : std::uint16_t
{
	DemoRegister = 1001,	// 서버 등록
};

#endif // ndef ZONEPACKETTYPES_GEN_H
