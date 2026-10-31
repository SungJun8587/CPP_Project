
//***************************************************************************
// ZonePacket.h : 존 서버 분리 구조(Gateway / ZoneServer) 공용 패킷 프로토콜 정의
//
//***************************************************************************

#ifndef ZONEPACKET_H
#define ZONEPACKET_H

#include <Network/Packet.h>
#include "ZonePacketTypes.h"

#include <chrono>
#include <cstring>

//***************************************************************************
// 프레이밍 규칙: ChatPacket.h와 동일하게 모든 패킷은 PacketHeader로 시작하며,
// header.size는 "헤더를 포함한" 패킷 전체 크기입니다.
//
// 중계 패킷(ZoneRelayToZone / ZoneRelayToClient)은 고정 필드 뒤에 클라이언트 패킷 원문
// (자체 PacketHeader 포함)이 그대로 이어지는 가변 길이 패킷입니다.
//***************************************************************************
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
	uint8	success;			// 1: 성공, 0: 실패
	uint8	reason;				// EGatewayResult
	uint64	clientSessionId;	// success==1일 때 유효한 Gateway 세션 ID
};

//***************************************************************************
// @brief 존 입장 요청 구조체 (Client -> Gateway).
//***************************************************************************
struct GwEnterZoneReqPacket : PacketHeader
{
	uint32	zoneId;
};

//***************************************************************************
// @brief 존 입장 응답 구조체 (Gateway -> Client).
//***************************************************************************
struct GwEnterZoneResPacket : PacketHeader
{
	uint8	success;
	uint8	reason;				// EGatewayResult
	uint32	zoneId;
	float	spawnX;				// success==1일 때 유효한 서버 확정 스폰 위치
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
	uint8	reason;				// EGatewayResult
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
	uint32	protocolVersion;				// kZoneProtocolVersion
	uint32	zoneId;
	uint8	kind;							// EZoneKind
	uint32	capacity;						// 최대 수용 인원
	char	serverName[kServerNameBytes];	// UTF-8, 로그/모니터링용
};

//***************************************************************************
// @brief 존 서버 등록 응답 (Gateway -> Zone).
// @details 거부(success==0)되면 존 서버는 스스로 종료해야 한다(재시도해도 같은 결과).
//***************************************************************************
struct ZoneRegisterResPacket : PacketHeader
{
	uint8	success;
	uint8	reason;						// EZoneRegisterResult
	uint32	heartbeatIntervalMs;		// 존 서버가 하트비트를 보낼 주기
};

//***************************************************************************
// @brief 존 서버 하트비트 (Zone -> Gateway).
//***************************************************************************
struct ZoneHeartbeatPacket : PacketHeader
{
	uint32	zoneId;
	uint32	userCount;		// 존이 보고하는 현재 플레이어 수(모니터링용, 입장 제한은 Gateway 집계 기준)
	uint32	lastTickUs;		// 마지막 틱 처리 시간(us)
	uint32	maxTickUs;		// 최대 틱 처리 시간(us)
};

//***************************************************************************
// @brief 입장 요청 (Gateway -> Zone).
//***************************************************************************
struct ZoneEnterReqPacket : PacketHeader
{
	uint64	clientSessionId;
	uint64	charId;
};

//***************************************************************************
// @brief 입장 응답 (Zone -> Gateway).
//***************************************************************************
struct ZoneEnterResPacket : PacketHeader
{
	uint64	clientSessionId;
	uint64	charId;
	uint8	success;
	uint8	reason;			// EZoneEnterResult
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
	uint8	reason;			// EZoneKickReason
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
	uint16	recipientCount;		// 수신자 수 (1 이상)
};

// ---------------------------------------------------------------------------
// 클라이언트 <-> ZoneServer (Gateway가 중계)
// ---------------------------------------------------------------------------

//***************************************************************************
// @brief 이동 요청 (Client -> Zone). 속도는 서버 스탯을 사용하므로 실리지 않는다.
//***************************************************************************
struct ZcMoveReqPacket : PacketHeader
{
	float	posX;		// 클라이언트가 보고하는 현재 위치 (서버가 허용 오차로 검증)
	float	posY;
	float	destX;		// 목적지
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
	float	speed;			// units/sec
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
	uint8	arrived;		// 1: 목적지 도착, 0: 중단/순간이동
};

//***************************************************************************
// @brief 엔티티 출현 알림 (Zone -> Client). 시야(AOI)에 들어온 엔티티의 현재 상태.
//***************************************************************************
struct ZcSpawnNotifyPacket : PacketHeader
{
	uint64	entityId;
	uint8	entityType;		// EntityType
	float	posX;
	float	posY;
	float	destX;
	float	destY;
	float	speed;
	uint8	moving;			// 1이면 dest를 향해 이동 중
	int32	hp;				// 현재 체력
	int32	maxHp;			// 최대 체력
	uint8	dead;			// 1이면 사망 상태(시체)
	uint32	templateId;		// 몬스터 종류 ID (플레이어는 0). 클라이언트가 모델/이름을 고르는 키
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
	uint8	reason;			// ECorrectPosReason
	float	posX;
	float	posY;
};

//***************************************************************************
// @brief 기본 공격 요청 (Client -> Zone). 공격자는 세션으로 식별되므로 대상만 싣는다.
//***************************************************************************
struct ZcAttackReqPacket : PacketHeader
{
	uint64	targetId;		// 공격 대상 엔티티 ID (서버가 존재/생존/사거리/공격 가능 여부를 모두 검증)
};

//***************************************************************************
// @brief 피해 알림 (Zone -> Client). 공격자·피해자 주변 전원에게 전송된다.
//***************************************************************************
struct ZcDamageNotifyPacket : PacketHeader
{
	uint64	attackerId;		// 공격자 ID, 공격자가 없는 피해(DoT/환경)면 0
	uint64	targetId;
	int32	damage;			// 적용된 피해량
	int32	hp;				// 피해 적용 후 피해자 체력
	int32	maxHp;
};

//***************************************************************************
// @brief 사망 알림 (Zone -> Client). 피해자 주변 전원에게 전송된다.
//***************************************************************************
struct ZcDeathNotifyPacket : PacketHeader
{
	uint64	entityId;		// 사망한 엔티티
	uint64	killerId;		// 처치한 엔티티, 없으면 0
};

//***************************************************************************
// @brief 부활 알림 (Zone -> Client). 리스폰 위치 주변 전원에게 전송된다.
//***************************************************************************
struct ZcRespawnNotifyPacket : PacketHeader
{
	uint64	entityId;
	float	posX;			// 리스폰 위치
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
	uint64	targetId;		// 요청에 실렸던 대상 ID
	uint8	reason;			// EAttackRejectReason
};

//***************************************************************************
// @brief 체력 회복 알림 (Zone -> Client). 엔티티 주변 플레이어 전원에게 전송된다.
//***************************************************************************
struct ZcHealNotifyPacket : PacketHeader
{
	uint64	entityId;
	int32	amount;			// 실제 회복량
	int32	hp;				// 회복 후 체력
	int32	maxHp;
};

#pragma pack(pop)

//***************************************************************************
// @brief 패킷 헤더(size/type)를 초기화합니다.
// @tparam TPacket PacketHeader를 상속한 패킷 구조체
// @tparam TEnum   패킷 타입 열거형
// @param packet 초기화할 패킷
// @param type   패킷 타입
//***************************************************************************
template <typename TPacket, typename TEnum>
inline void InitZonePacket(TPacket& packet, TEnum type)
{
	packet.size = static_cast<uint16>(sizeof(TPacket));
	packet.type = static_cast<uint16>(type);
}

//***************************************************************************
// @brief 중계 패킷(고정 헤더 + 페이로드 원문)을 버퍼에 만듭니다.
// @tparam TRelay ZoneRelayToZonePacket 또는 ZoneRelayToClientPacket
// @param type            중계 패킷 타입
// @param clientSessionId 대상(또는 출처) 클라이언트 세션 ID
// @param payload         중계할 패킷 원문(자체 PacketHeader 포함)
// @param payloadSize     원문 크기
// @param outBuf          결과를 만들 버퍼
// @param outCapacity     버퍼 크기
// @param outSize         [out] 만들어진 패킷 전체 크기
// @return 성공 시 true, 크기가 버퍼/uint16 범위를 넘으면 false
//***************************************************************************
template <typename TRelay>
inline bool BuildRelayPacket(EZoneServerPacketType type, uint64 clientSessionId,
	const void* payload, size_t payloadSize, BYTE* outBuf, size_t outCapacity, uint16& outSize)
{
	const size_t total = sizeof(TRelay) + payloadSize;
	if( payload == nullptr || total > outCapacity || total > 0xFFFF )
		return false;

	TRelay* relay = reinterpret_cast<TRelay*>(outBuf);
	*relay = TRelay{};
	InitZonePacket(*relay, type);
	relay->size = static_cast<uint16>(total);
	relay->clientSessionId = clientSessionId;

	std::memcpy(outBuf + sizeof(TRelay), payload, payloadSize);
	outSize = static_cast<uint16>(total);
	return true;
}

//***************************************************************************
// @brief 멀티캐스트 중계 패킷 한 개에 담을 수 있는 최대 수신자 수를 계산합니다.
// @param payloadSize 중계할 패킷 원문 크기
// @param capacity    패킷 전체 크기 상한(보통 kMaxServerPacketBytes)
// @return 최대 수신자 수. 원문이 너무 커서 한 명도 못 담으면 0
//***************************************************************************
inline size_t MaxMulticastRecipients(size_t payloadSize, size_t capacity)
{
	const size_t fixedSize = sizeof(ZoneRelayToClientsPacket) + payloadSize;
	if( fixedSize >= capacity || fixedSize > 0xFFFF )
		return 0;

	const size_t byCapacity = (capacity - fixedSize) / sizeof(uint64);
	return byCapacity < 0xFFFF ? byCapacity : 0xFFFF;
}

//***************************************************************************
// @brief 멀티캐스트 중계 패킷(고정 헤더 + 수신자 ID 배열 + 페이로드 원문)을 버퍼에 만듭니다.
// @param sessionIds   수신자 클라이언트 세션 ID 배열
// @param count        수신자 수 (1 이상, MaxMulticastRecipients 이하)
// @param payload      중계할 패킷 원문(자체 PacketHeader 포함)
// @param payloadSize  원문 크기
// @param outBuf       결과를 만들 버퍼
// @param outCapacity  버퍼 크기
// @param outSize      [out] 만들어진 패킷 전체 크기
// @return 성공 시 true, 인자가 잘못됐거나 크기가 버퍼/uint16 범위를 넘으면 false
//***************************************************************************
inline bool BuildMulticastRelayPacket(const uint64* sessionIds, size_t count,
	const void* payload, size_t payloadSize, BYTE* outBuf, size_t outCapacity, uint16& outSize)
{
	if( sessionIds == nullptr || payload == nullptr || count == 0 || count > MaxMulticastRecipients(payloadSize, outCapacity) )
		return false;

	const size_t idsBytes = count * sizeof(uint64);
	const size_t total = sizeof(ZoneRelayToClientsPacket) + idsBytes + payloadSize;

	ZoneRelayToClientsPacket relay{};
	InitZonePacket(relay, EZoneServerPacketType::ZoneRelayToClients);
	relay.size = static_cast<uint16>(total);
	relay.recipientCount = static_cast<uint16>(count);

	std::memcpy(outBuf, &relay, sizeof(relay));
	std::memcpy(outBuf + sizeof(relay), sessionIds, idsBytes);
	std::memcpy(outBuf + sizeof(relay) + idsBytes, payload, payloadSize);
	outSize = static_cast<uint16>(total);
	return true;
}

//***************************************************************************
// @brief 수신 버퍼에서 완전한 패킷 단위만 잘라 onPacket으로 전달합니다. CIocpSession::OnRecv() 구현용.
// @tparam TFn bool(const PacketHeader*) 형태의 호출 가능 객체. false를 반환하면 프로토콜 위반으로 본다.
// @param buffer         수신 버퍼
// @param len            수신 버퍼에 쌓인 바이트 수
// @param maxPacketBytes 허용하는 패킷 최대 크기(이보다 큰 size는 프로토콜 위반)
// @param onPacket       완전한 패킷 하나를 처리하는 함수
// @return 처리한 총 바이트 수. 프로토콜 위반이면 -1(CIocpSession이 연결을 종료한다).
//***************************************************************************
template <typename TFn>
inline int32 ProcessPacketFrames(const BYTE* buffer, int32 len, uint16 maxPacketBytes, TFn&& onPacket)
{
	int32 processed = 0;

	while( len - processed >= static_cast<int32>(sizeof(PacketHeader)) )
	{
		const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer + processed);
		const int32 size = static_cast<int32>(header->size);

		if( size < static_cast<int32>(sizeof(PacketHeader)) || size > static_cast<int32>(maxPacketBytes) )
			return -1;

		// 패킷 전체가 아직 도착하지 않음 — 다음 Recv 완료를 기다린다.
		if( len - processed < size )
			break;

		if( !onPacket(header) )
			return -1;

		processed += size;
	}

	return processed;
}

//***************************************************************************
// @brief 단조 증가 시각(ms)을 반환합니다. 하트비트 타임아웃 판정용.
// @return steady_clock 기준 밀리초
//***************************************************************************
inline int64 SteadyNowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

#endif // ndef ZONEPACKET_H