//***************************************************************************
// ZonePacketStructs.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData 엑셀의 Packet, PacketField 시트로부터 만듭니다.
//***************************************************************************

#ifndef ZONEPACKETSTRUCTS_GEN_H
#define ZONEPACKETSTRUCTS_GEN_H

#include <Network/Packet.h>
#include "ZonePacketTypes.gen.h"

#pragma pack(push, 1)

// ---------------------------------------------------------------------------
// 클라이언트 <-> Gateway
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 로그인 요청 구조체 (Client -> Gateway).
// @details [임시] 현재는 클라이언트가 보낸 charId를 그대로 신뢰한다.
//          실제 서비스에서는 인증 토큰 검증(CGatewayServerMain::AuthenticateClient)으로 대체해야 한다.
//***************************************************************************
struct GwLoginReqPacket : PacketHeader
{
	uint64	charId;
};

//***************************************************************************
// @brief 로그인 응답 구조체 (Gateway -> Client).
//***************************************************************************
struct GwLoginResPacket : PacketHeader
{
	uint8	success;	// 1: 성공, 0: 실패
	uint8	reason;	// EGatewayResult
	uint64	clientSessionId;	// success==1일 때 유효한 Gateway 세션 ID
};

//***************************************************************************
// @brief 존 입장 요청 구조체 (Client -> Gateway).
//***************************************************************************
struct GwEnterZoneReqPacket : PacketHeader
{
	uint32	zoneId;
	uint32	heroId;	// 선택한 영웅 ID (0 = 영웅 데이터의 기본 영웅)
};

//***************************************************************************
// @brief 존 입장 응답 구조체 (Gateway -> Client).
//***************************************************************************
struct GwEnterZoneResPacket : PacketHeader
{
	uint8	success;
	uint8	reason;	// EGatewayResult
	uint32	zoneId;
	float	spawnX;	// success==1일 때 유효한 서버 확정 스폰 위치
	float	spawnY;
};

//***************************************************************************
// @brief 존 퇴장 요청 구조체 (Client -> Gateway). 바디 없음.
//***************************************************************************
struct GwLeaveZoneReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 존 퇴장 응답 구조체 (Gateway -> Client).
//***************************************************************************
struct GwLeaveZoneResPacket : PacketHeader
{
	uint8	success;
	uint8	reason;	// EGatewayResult
};

//***************************************************************************
// @brief 존 소실 알림 (Gateway -> Client). 존 서버가 죽었거나 존이 플레이어를 내보냈을 때.
//***************************************************************************
struct GwZoneLostNotifyPacket : PacketHeader
{
	uint32	zoneId;
};

// ---------------------------------------------------------------------------
// Gateway <-> ZoneServer
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 존 서버 등록 요청 (Zone -> Gateway). 접속 직후 첫 패킷이어야 한다.
//***************************************************************************
struct ZoneRegisterReqPacket : PacketHeader
{
	uint32	protocolVersion;	// kZoneProtocolVersion
	uint32	zoneId;
	uint8	kind;	// EZoneKind
	uint32	capacity;	// 최대 수용 인원
	char	serverName[kServerNameBytes];	// UTF-8, 로그/모니터링용
};

//***************************************************************************
// @brief 존 서버 등록 응답 (Gateway -> Zone).
// @details 거부(success==0)되면 존 서버는 스스로 종료해야 한다(재시도해도 같은 결과).
//***************************************************************************
struct ZoneRegisterResPacket : PacketHeader
{
	uint8	success;
	uint8	reason;	// EZoneRegisterResult
	uint32	heartbeatIntervalMs;	// 존 서버가 하트비트를 보낼 주기
};

//***************************************************************************
// @brief 존 서버 하트비트 (Zone -> Gateway).
//***************************************************************************
struct ZoneHeartbeatPacket : PacketHeader
{
	uint32	zoneId;
	uint32	userCount;	// 존이 보고하는 현재 플레이어 수(모니터링용, 입장 제한은 Gateway 집계 기준)
	uint32	lastTickUs;	// 마지막 틱 처리 시간(us)
	uint32	maxTickUs;	// 최대 틱 처리 시간(us)
};

//***************************************************************************
// @brief 입장 요청 (Gateway -> Zone).
//***************************************************************************
struct ZoneEnterReqPacket : PacketHeader
{
	uint64	clientSessionId;
	uint64	charId;
	uint32	heroId;	// 선택한 영웅 ID (0 = 영웅 데이터의 기본 영웅)
};

//***************************************************************************
// @brief 입장 응답 (Zone -> Gateway).
//***************************************************************************
struct ZoneEnterResPacket : PacketHeader
{
	uint64	clientSessionId;
	uint64	charId;
	uint8	success;
	uint8	reason;	// EZoneEnterResult
	float	spawnX;
	float	spawnY;
};

//***************************************************************************
// @brief 퇴장 요청 (Gateway -> Zone). 클라이언트 연결 종료 시에도 전송된다.
//***************************************************************************
struct ZoneLeaveReqPacket : PacketHeader
{
	uint64	clientSessionId;
	uint64	charId;
};

//***************************************************************************
// @brief 추방 알림 (Zone -> Gateway).
//***************************************************************************
struct ZoneKickNotifyPacket : PacketHeader
{
	uint64	clientSessionId;
	uint8	reason;	// EZoneKickReason
};

//***************************************************************************
// @brief 클라이언트 패킷 중계 (Gateway -> Zone). 이 구조체 바로 뒤에 클라이언트 패킷 원문이 이어진다.
//***************************************************************************
struct ZoneRelayToZonePacket : PacketHeader
{
	uint64	clientSessionId;
};

//***************************************************************************
// @brief 클라이언트로 보낼 패킷 중계 (Zone -> Gateway). 이 구조체 바로 뒤에 패킷 원문이 이어진다.
//***************************************************************************
struct ZoneRelayToClientPacket : PacketHeader
{
	uint64	clientSessionId;
};

//***************************************************************************
// @brief 같은 패킷을 여러 클라이언트로 보내는 멀티캐스트 중계 (Zone -> Gateway).
// @details 이 구조체 바로 뒤에 uint64 clientSessionId[recipientCount], 이어서 패킷 원문이 온다.
//          AOI 브로드캐스트(이동/피해/사망 통지)에서 수신자마다 중계 패킷을 따로 만들던 것을 한 패킷으로 줄인다.
//          전체 크기는 sizeof(이 구조체) + recipientCount * 8 + 원문 크기이며 Gateway가 이를 검증한다.
//***************************************************************************
struct ZoneRelayToClientsPacket : PacketHeader
{
	uint16	recipientCount;	// 수신자 수 (1 이상)
};

// ---------------------------------------------------------------------------
// 클라이언트 <-> ZoneServer (Gateway가 중계)
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 이동 요청 (Client -> Zone). 속도는 서버 스탯을 사용하므로 실리지 않는다.
//***************************************************************************
struct ZcMoveReqPacket : PacketHeader
{
	float	posX;	// 클라이언트가 보고하는 현재 위치 (서버가 허용 오차로 검증)
	float	posY;
	float	destX;	// 목적지
	float	destY;
};

//***************************************************************************
// @brief 이동 시작/목적지 갱신 알림 (Zone -> Client).
//***************************************************************************
struct ZcMoveNotifyPacket : PacketHeader
{
	uint64	entityId;
	float	startX;
	float	startY;
	float	destX;
	float	destY;
	float	speed;	// units/sec
	uint64	serverTimeMs;	// 존 논리 시간(이동 보간 기준)
};

//***************************************************************************
// @brief 이동 종료 알림 (Zone -> Client). 순간이동 후 새 위치 통지에도 쓰인다.
//***************************************************************************
struct ZcStopNotifyPacket : PacketHeader
{
	uint64	entityId;
	float	posX;
	float	posY;
	uint8	arrived;	// 1: 목적지 도착, 0: 중단/순간이동
};

//***************************************************************************
// @brief 엔티티 출현 알림 (Zone -> Client). 시야(AOI)에 들어온 엔티티의 현재 상태.
//***************************************************************************
struct ZcSpawnNotifyPacket : PacketHeader
{
	uint64	entityId;
	uint8	entityType;	// EntityType
	float	posX;
	float	posY;
	float	destX;
	float	destY;
	float	speed;
	uint8	moving;	// 1이면 dest를 향해 이동 중
	int32	hp;	// 현재 체력
	int32	maxHp;	// 최대 체력
	uint8	dead;	// 1이면 사망 상태(시체)
	uint32	templateId;	// 몬스터는 몬스터 종류 ID, 플레이어는 영웅 ID (entityType으로 구분). 클라이언트가 모델/이름을 고르는 키
};

//***************************************************************************
// @brief 엔티티 소멸 알림 (Zone -> Client). 시야(AOI)에서 벗어났거나 존에서 제거됨.
//***************************************************************************
struct ZcDespawnNotifyPacket : PacketHeader
{
	uint64	entityId;
};

//***************************************************************************
// @brief 위치 보정 알림 (Zone -> Client). 이동 요청이 거부됐거나 서버가 순간이동시켰을 때.
//***************************************************************************
struct ZcCorrectPosNotifyPacket : PacketHeader
{
	uint8	reason;	// ECorrectPosReason
	float	posX;
	float	posY;
};

//***************************************************************************
// @brief 기본 공격 요청 (Client -> Zone). 공격자는 세션으로 식별되므로 대상만 싣는다.
//***************************************************************************
struct ZcAttackReqPacket : PacketHeader
{
	uint64	targetId;	// 공격 대상 엔티티 ID (서버가 존재/생존/사거리/공격 가능 여부를 모두 검증)
};

//***************************************************************************
// @brief 피해 알림 (Zone -> Client). 공격자·피해자 주변 전원에게 전송된다.
//***************************************************************************
struct ZcDamageNotifyPacket : PacketHeader
{
	uint64	attackerId;	// 공격자 ID, 공격자가 없는 피해(DoT/환경)면 0
	uint64	targetId;
	int32	damage;	// 적용된 피해량
	int32	hp;	// 피해 적용 후 피해자 체력
	int32	maxHp;
};

//***************************************************************************
// @brief 사망 알림 (Zone -> Client). 피해자 주변 전원에게 전송된다.
//***************************************************************************
struct ZcDeathNotifyPacket : PacketHeader
{
	uint64	entityId;	// 사망한 엔티티
	uint64	killerId;	// 처치한 엔티티, 없으면 0
};

//***************************************************************************
// @brief 부활 알림 (Zone -> Client). 리스폰 위치 주변 전원에게 전송된다.
//***************************************************************************
struct ZcRespawnNotifyPacket : PacketHeader
{
	uint64	entityId;
	float	posX;	// 리스폰 위치
	float	posY;
	int32	hp;
	int32	maxHp;
	uint32	invulnerableMs;	// 남은 무적 시간(ms), 0이면 없음
};

//***************************************************************************
// @brief 공격 거부 알림 (Zone -> Client). 공격을 요청한 본인에게만 전송된다.
//***************************************************************************
struct ZcAttackRejectNotifyPacket : PacketHeader
{
	uint64	targetId;	// 요청에 실렸던 대상 ID
	uint8	reason;	// EAttackRejectReason
};

//***************************************************************************
// @brief 체력 회복 알림 (Zone -> Client). 엔티티 주변 플레이어 전원에게 전송된다.
//***************************************************************************
struct ZcHealNotifyPacket : PacketHeader
{
	uint64	entityId;
	int32	amount;	// 실제 회복량
	int32	hp;	// 회복 후 체력
	int32	maxHp;
};

//***************************************************************************
// @brief 스킬 시전 요청 (Client -> Zone). 시전자는 세션으로 식별된다.
//        Single 스킬은 targetId를, Area 스킬은 (x, y) 중심 좌표를 쓴다. 쓰지 않는 쪽은 서버가 무시한다.
//***************************************************************************
struct ZcSkillReqPacket : PacketHeader
{
	uint32	skillId;
	uint64	targetId;	// Single의 대상 엔티티 ID
	float	x;	// Area의 중심 X
	float	y;	// Area의 중심 Y
};

//***************************************************************************
// @brief 스킬 시전 알림 (Zone -> Client). 시전자 주변 전원에게 전송된다. 맞은 대상의 피해는 ZcDamageNotify로 이어진다.
//***************************************************************************
struct ZcSkillNotifyPacket : PacketHeader
{
	uint64	casterId;
	uint32	skillId;
	uint64	targetId;	// Single의 대상, Area는 0
	float	x;	// 효과 중심 (Single은 대상 위치)
	float	y;
	uint32	hitCount;	// 맞은 대상 수
};

//***************************************************************************
// @brief 스킬 요청 거부 알림 (Zone -> Client). 요청한 본인에게만 전송된다.
//***************************************************************************
struct ZcSkillRejectNotifyPacket : PacketHeader
{
	uint32	skillId;	// 요청에 실렸던 스킬 ID
	uint8	reason;	// ESkillRejectReason
};

//***************************************************************************
// @brief 스킬 시전 시작 알림 (Zone -> Client). 시전 시간이 있는 스킬만 보낸다. 시전자 주변 전원에게 전송된다.
// @details 시전이 끝나면 ZcSkillNotify(즉시 스킬) 또는 ZcProjectileNotify(투사체 스킬)가 이어지고, 취소되면 ZcSkillCancelNotify가 온다.
//***************************************************************************
struct ZcSkillCastNotifyPacket : PacketHeader
{
	uint64	casterId;
	uint32	skillId;
	uint64	targetId;	// Single의 대상, Area는 0
	float	x;	// 효과 중심 (Single은 시전 시작 시점의 대상 위치)
	float	y;
	uint32	castTimeMs;
};

//***************************************************************************
// @brief 스킬 시전 취소 알림 (Zone -> Client). 시전자 주변 전원에게 전송된다.
//***************************************************************************
struct ZcSkillCancelNotifyPacket : PacketHeader
{
	uint64	casterId;
	uint32	skillId;
	uint8	reason;	// ESkillCancelReason
};

//***************************************************************************
// @brief 투사체 발사 알림 (Zone -> Client). 시전자 주변 전원에게 전송된다.
// @details 서버는 투사체를 엔티티로 두지 않는다. 클라이언트는 (x0,y0)에서 (x1,y1)까지 flightMs 동안 날아가는 연출만 한다.
//          Single은 대상을 추적하므로 실제 착탄 지점은 ZcSkillImpactNotify의 좌표를 따른다.
//***************************************************************************
struct ZcProjectileNotifyPacket : PacketHeader
{
	uint32	projectileId;	// 존 안에서 발사마다 늘어나는 번호 (착탄 알림과 짝)
	uint64	casterId;
	uint32	skillId;
	uint64	targetId;	// Single의 대상, Area는 0
	float	x0;
	float	y0;
	float	x1;
	float	y1;
	uint32	flightMs;
};

//***************************************************************************
// @brief 투사체 착탄 알림 (Zone -> Client). 시전자 주변 전원에게 전송된다. 맞은 대상의 피해는 ZcDamageNotify로 이어진다.
//***************************************************************************
struct ZcSkillImpactNotifyPacket : PacketHeader
{
	uint32	projectileId;
	uint64	casterId;
	uint32	skillId;
	float	x;	// 실제 착탄 지점
	float	y;
	uint32	hitCount;	// 맞은 대상 수 (빗나가면 0)
};

//***************************************************************************
// @brief 상태 효과 알림 (Zone -> Client). 엔티티 주변 플레이어 전원에게 전송된다.
// @details applied=1이면 걸렸거나 지속시간이 갱신된 것이고(durationMs만큼 남음), 0이면 지속시간이 다해 풀린 것이다.
//          엔티티가 사망하면 모든 상태 효과가 풀리지만 해제 알림은 보내지 않는다. ZcDeathNotify를 받으면 클라이언트가 비운다.
//***************************************************************************
struct ZcStatusNotifyPacket : PacketHeader
{
	uint64	entityId;
	uint32	statusId;
	uint64	casterId;	// 건 엔티티 (applied=1일 때만 유효)
	uint32	durationMs;	// 남은 지속시간 (applied=0이면 0)
	uint8	applied;
};

#pragma pack(pop)

static_assert(sizeof(GwLoginReqPacket) == 12, "GwLoginReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwLoginResPacket) == 14, "GwLoginResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwEnterZoneReqPacket) == 12, "GwEnterZoneReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwEnterZoneResPacket) == 18, "GwEnterZoneResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwLeaveZoneReqPacket) == 4, "GwLeaveZoneReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwLeaveZoneResPacket) == 6, "GwLeaveZoneResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(GwZoneLostNotifyPacket) == 8, "GwZoneLostNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneRegisterReqPacket) == 49, "ZoneRegisterReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneRegisterResPacket) == 10, "ZoneRegisterResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneHeartbeatPacket) == 20, "ZoneHeartbeatPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneEnterReqPacket) == 24, "ZoneEnterReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneEnterResPacket) == 30, "ZoneEnterResPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneLeaveReqPacket) == 20, "ZoneLeaveReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneKickNotifyPacket) == 13, "ZoneKickNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneRelayToZonePacket) == 12, "ZoneRelayToZonePacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneRelayToClientPacket) == 12, "ZoneRelayToClientPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZoneRelayToClientsPacket) == 6, "ZoneRelayToClientsPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcMoveReqPacket) == 20, "ZcMoveReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcMoveNotifyPacket) == 40, "ZcMoveNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcStopNotifyPacket) == 21, "ZcStopNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSpawnNotifyPacket) == 47, "ZcSpawnNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcDespawnNotifyPacket) == 12, "ZcDespawnNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcCorrectPosNotifyPacket) == 13, "ZcCorrectPosNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcAttackReqPacket) == 12, "ZcAttackReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcDamageNotifyPacket) == 32, "ZcDamageNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcDeathNotifyPacket) == 20, "ZcDeathNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcRespawnNotifyPacket) == 32, "ZcRespawnNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcAttackRejectNotifyPacket) == 13, "ZcAttackRejectNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcHealNotifyPacket) == 24, "ZcHealNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillReqPacket) == 24, "ZcSkillReqPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillNotifyPacket) == 36, "ZcSkillNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillRejectNotifyPacket) == 9, "ZcSkillRejectNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillCastNotifyPacket) == 36, "ZcSkillCastNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillCancelNotifyPacket) == 17, "ZcSkillCancelNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcProjectileNotifyPacket) == 48, "ZcProjectileNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcSkillImpactNotifyPacket) == 32, "ZcSkillImpactNotifyPacket: wire size differs from the Packet sheet");
static_assert(sizeof(ZcStatusNotifyPacket) == 29, "ZcStatusNotifyPacket: wire size differs from the Packet sheet");

#endif // ndef ZONEPACKETSTRUCTS_GEN_H
