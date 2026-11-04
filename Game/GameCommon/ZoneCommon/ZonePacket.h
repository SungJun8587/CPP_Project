
//***************************************************************************
// ZonePacket.h : 존 서버 분리 구조(Gateway / ZoneServer) 공용 패킷 프로토콜
//
// 패킷 구조체와 패킷 번호 열거형은 GameData.xlsx의 Packet / PacketField / PacketGroup 시트에서
// XlsxConverter가 만든다. (GameCommon/Generated/ZonePacketStructs.gen.h) 이 파일에는 손으로 쓰는
// 도우미(프레이밍, 중계 패킷 조립 등)만 둔다. 패킷을 추가/변경하려면 엑셀 시트를 고치고 XlsxConverter를 다시 실행한다.
//***************************************************************************

#ifndef ZONEPACKET_H
#define ZONEPACKET_H

#include <Network/Packet.h>
#include "ZonePacketTypes.h"
#include "../Generated/ZonePacketStructs.gen.h"

#include <chrono>
#include <cstring>

//***************************************************************************
// 프레이밍 규칙: ChatPacket.h와 동일하게 모든 패킷은 PacketHeader로 시작하며,
// header.size는 "헤더를 포함한" 패킷 전체 크기입니다.
//
// 중계 패킷(ZoneRelayToZone / ZoneRelayToClient)은 고정 필드 뒤에 클라이언트 패킷 원문
// (자체 PacketHeader 포함)이 그대로 이어지는 가변 길이 패킷입니다.
//***************************************************************************

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