
//***************************************************************************
// ZonePacketTypes.h : 존 서버 분리 구조(Gateway / ZoneServer) 공용 패킷 상수 및 열거형 정의
//
//***************************************************************************

#ifndef ZONEPACKETTYPES_H
#define ZONEPACKETTYPES_H

#include <cstddef>
#include <cstdint>

//***************************************************************************
// @brief 서버 간(Zone <-> Gateway) 프로토콜 버전. 등록 요청에 실려 가며 불일치하면 등록이 거부됩니다.
//***************************************************************************
constexpr uint32_t kZoneProtocolVersion = 1;

//***************************************************************************
// @brief 패킷 타입 번호 구간.
// @details    1 ~  999 : 클라이언트 <-> Gateway 패킷 (EGatewayPacketType)
//          1000 ~ 1999 : Gateway <-> ZoneServer 패킷 (EZoneServerPacketType)
//          2000 ~      : 클라이언트 <-> ZoneServer 패킷 (EZoneClientPacketType).
//                        Gateway는 이 구간의 패킷 내용을 해석하지 않고 현재 존으로 중계만 한다.
//***************************************************************************
constexpr uint16_t kClientRelayTypeMin = 2000;

//***************************************************************************
// @brief 패킷 크기 상한.
// @details 클라이언트 패킷은 중계 헤더가 붙어도 서버 간 상한(8192)을 넘지 않도록 4096으로 제한한다.
//***************************************************************************
constexpr uint16_t kMaxClientPacketBytes = 4096;
constexpr uint16_t kMaxServerPacketBytes = 8192;

//***************************************************************************
// @brief 존 서버 이름 필드 바이트 크기 (UTF-8, NUL 포함)
//***************************************************************************
constexpr size_t kServerNameBytes = 32;

//***************************************************************************
// @brief Gateway가 존 서버에 지시하는 기본 하트비트 주기(ms).
//        Gateway는 하트비트가 이 주기의 몇 배 이상 끊기면 존 서버를 죽은 것으로 보고 연결을 끊는다.
//***************************************************************************
constexpr uint32_t kDefaultHeartbeatIntervalMs = 2000;
constexpr uint32_t kDefaultHeartbeatTimeoutMs = 10000;

//***************************************************************************
// @brief 존 종류.
//***************************************************************************
enum class EZoneKind : uint8_t
{
	Field = 1,		// 상시 열려 있는 필드
	Dungeon = 2,	// 던전(인스턴스)
};

//***************************************************************************
// @brief 존 서버 등록 결과. ZoneRegisterResPacket::reason에 설정됩니다.
//***************************************************************************
enum class EZoneRegisterResult : uint8_t
{
	Ok = 0,
	DuplicateZone = 1,		// 같은 zoneId가 이미 살아있는 존 서버에 의해 등록됨
	InvalidZone = 2,		// zoneId 또는 capacity가 0
	VersionMismatch = 3,	// 프로토콜 버전 불일치
};

//***************************************************************************
// @brief Gateway 요청 처리 결과. Gw*Res 패킷의 reason에 설정됩니다.
//***************************************************************************
enum class EGatewayResult : uint8_t
{
	Ok = 0,
	NotLoggedIn = 1,
	AlreadyLoggedIn = 2,	// 이 세션이 이미 로그인했거나, 같은 캐릭터가 다른 세션으로 접속 중
	InvalidCharId = 3,
	AlreadyInZone = 4,		// 이미 존에 있거나 입장 진행 중
	NotInZone = 5,
	ZoneNotFound = 6,		// 해당 zoneId의 존 서버가 등록돼 있지 않음
	ZoneFull = 7,			// 존 정원 초과
	ZoneError = 8,			// 존 서버가 입장을 거부했거나 전달에 실패
};

//***************************************************************************
// @brief 존 서버의 입장 처리 결과. ZoneEnterResPacket::reason에 설정됩니다.
//***************************************************************************
enum class EZoneEnterResult : uint8_t
{
	Ok = 0,
	DuplicateEntity = 1,	// 같은 캐릭터/세션이 이미 존에 있음
	ZoneError = 2,
};

//***************************************************************************
// @brief 존 서버가 플레이어를 존에서 내보낸 사유. ZoneKickNotifyPacket::reason에 설정됩니다.
//***************************************************************************
enum class EZoneKickReason : uint8_t
{
	ZoneError = 1,
	ZoneStopping = 2,
};

//***************************************************************************
// @brief 위치 보정 사유. ZcCorrectPosNotifyPacket::reason에 설정됩니다.
//***************************************************************************
enum class ECorrectPosReason : uint8_t
{
	PositionMismatch = 1,	// 클라이언트 보고 위치가 서버 위치와 허용 오차 이상 차이
	OutOfBounds = 2,		// 목적지가 존 영역 밖
	Immobile = 3,			// 이동할 수 없는 상태
	Teleported = 4,			// 서버가 순간이동시킴
	Dead = 5,				// 사망 상태에서는 이동할 수 없음
};

//***************************************************************************
// @brief 공격 요청 거부 사유. ZcAttackRejectNotifyPacket::reason에 설정됩니다.
// @details 쿨다운 중이거나 같은 틱의 중복 요청은 통지 없이 무시되므로 여기에 없다.
//***************************************************************************
enum class EAttackRejectReason : uint8_t
{
	OutOfRange = 1,			// 대상이 없거나 사거리 밖 (엔티티 존재 여부가 새지 않도록 구분하지 않는다)
	AttackerDead = 2,		// 내가 사망 상태
	TargetDead = 3,			// 대상이 이미 사망 상태
	InvalidTarget = 4,		// 공격할 수 없는 대상 (자기 자신, PvP 금지 등)
	TargetInvulnerable = 5,	// 대상이 무적 상태
};

//***************************************************************************
// @brief 클라이언트 <-> Gateway 패킷 타입 (1 ~ 999)
//***************************************************************************
enum class EGatewayPacketType : uint16_t
{
	GwLoginReq = 1,			// Client -> Gateway, 로그인 요청(캐릭터 지정)
	GwLoginRes = 2,			// Gateway -> Client, 로그인 응답
	GwEnterZoneReq = 3,		// Client -> Gateway, 존 입장 요청
	GwEnterZoneRes = 4,		// Gateway -> Client, 존 입장 응답
	GwLeaveZoneReq = 5,		// Client -> Gateway, 존 퇴장 요청
	GwLeaveZoneRes = 6,		// Gateway -> Client, 존 퇴장 응답
	GwZoneLostNotify = 7,	// Gateway -> Client, 있던 존이 사라짐(존 서버 다운/추방)
};

//***************************************************************************
// @brief Gateway <-> ZoneServer 패킷 타입 (1000 ~ 1999)
//***************************************************************************
enum class EZoneServerPacketType : uint16_t
{
	ZoneRegisterReq = 1001,		// Zone -> Gateway, 존 서버 등록(접속 직후 첫 패킷)
	ZoneRegisterRes = 1002,		// Gateway -> Zone, 등록 결과 + 하트비트 주기
	ZoneHeartbeat = 1003,		// Zone -> Gateway, 생존 신고 + 부하 정보
	ZoneEnterReq = 1004,		// Gateway -> Zone, 클라이언트 입장 요청
	ZoneEnterRes = 1005,		// Zone -> Gateway, 입장 결과
	ZoneLeaveReq = 1006,		// Gateway -> Zone, 클라이언트 퇴장(연결 종료 포함)
	ZoneKickNotify = 1007,		// Zone -> Gateway, 존이 플레이어를 내보냄
	ZoneRelayToZone = 1008,		// Gateway -> Zone, 클라이언트 패킷 중계
	ZoneRelayToClient = 1009,	// Zone -> Gateway, 클라이언트로 보낼 패킷 중계
	ZoneRelayToClients = 1010,	// Zone -> Gateway, 같은 패킷을 여러 클라이언트로 보낼 때의 멀티캐스트 중계
};

//***************************************************************************
// @brief 클라이언트 <-> ZoneServer 패킷 타입 (2000 ~). Gateway가 내용을 해석하지 않고 중계한다.
//***************************************************************************
enum class EZoneClientPacketType : uint16_t
{
	ZcMoveReq = 2001,			// Client -> Zone, 이동 요청(현재 위치 + 목적지)
	ZcMoveNotify = 2002,		// Zone -> Client, 엔티티 이동 시작/목적지 갱신
	ZcStopNotify = 2003,		// Zone -> Client, 엔티티 이동 종료(또는 순간이동 후 위치)
	ZcSpawnNotify = 2004,		// Zone -> Client, 시야에 엔티티가 들어옴
	ZcDespawnNotify = 2005,		// Zone -> Client, 시야에서 엔티티가 벗어남
	ZcCorrectPosNotify = 2006,	// Zone -> Client, 내 위치를 서버 값으로 보정
	ZcAttackReq = 2007,			// Client -> Zone, 대상 지정 기본 공격 요청
	ZcDamageNotify = 2008,		// Zone -> Client, 피해 발생(공격자/피해자/피해량/남은 체력)
	ZcDeathNotify = 2009,		// Zone -> Client, 엔티티 사망
	ZcRespawnNotify = 2010,		// Zone -> Client, 엔티티 부활(리스폰 위치/체력)
	ZcAttackRejectNotify = 2011,// Zone -> Client, 내 공격 요청 거부 사유
};

#endif // ndef ZONEPACKETTYPES_H