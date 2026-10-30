
//***************************************************************************
// ChatRelayFrame.h : 채널(인스턴스) 간 전달 계층의 Pub/Sub 프레임 형식 (header-only)
//
// @details
// 채팅 서버 인스턴스가 Redis Pub/Sub 토픽(Chat:{group}:{channel})으로 주고받는 바이너리 페이로드다.
// 서버 객체에 의존하지 않아 단독으로 만들고 해석(테스트)할 수 있다. 숫자는 모두 리틀엔디안으로 고정한다.
//
//   [uint8 version=1][uint8 kind][uint8 flags(bit0: 발신자 있음)][16B 발신자 publicId]
//   [uint16 수신자 수][수신자 publicId 16B × 수신자 수][본문]
//
//   kind = Packet          본문 = 수신자에게 그대로 보낼 패킷(PacketHeader.size 길이와 정확히 일치해야 함)
//   kind = PresenceOffline 본문 없음. "수신자" 목록 = 접속을 종료하는 계정들 — 받은 서버가 각 계정의 친구 중
//                          자기 프로세스에 접속한 사람에게 종료를 알린다(서버 종료 시 일괄 통지용).
//***************************************************************************

#ifndef CHATRELAYFRAME_H
#define CHATRELAYFRAME_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ChatRelay
{
	inline constexpr uint8_t kFrameVersion = 1;
	inline constexpr uint8_t kFlagHasSender = 0x01;
	inline constexpr size_t kPublicIdSize = 16;
	inline constexpr size_t kFixedHeaderSize = 3 + kPublicIdSize + 2;	// version + kind + flags + sender + count
	inline constexpr size_t kMaxRecipients = 0xFFFF;					// 프레임의 수신자 수 필드 한계

	enum class EKind : uint8_t
	{
		Packet = 0,
		PresenceOffline = 1,
	};

	using Id = std::array<uint8_t, kPublicIdSize>;

	//***************************************************************************
	// @brief 프레임을 만든다.
	// @param recipients 수신자(최대 kMaxRecipients). 더 많으면 호출부가 나눠서 호출한다.
	// @param packet/packetSize kind==Packet일 때의 본문(그 외에는 nullptr/0)
	// @param sender 발신자(차단 필터용). nullptr이면 필터 없음
	//***************************************************************************
	inline std::string BuildFrame(EKind kind, const std::vector<Id>& recipients,
		const void* packet, size_t packetSize, const Id* sender)
	{
		std::string frame;
		frame.reserve(kFixedHeaderSize + recipients.size() * kPublicIdSize + packetSize);

		frame.push_back(static_cast<char>(kFrameVersion));
		frame.push_back(static_cast<char>(kind));
		frame.push_back(static_cast<char>(sender != nullptr ? kFlagHasSender : 0));
		if( sender != nullptr )
			frame.append(reinterpret_cast<const char*>(sender->data()), kPublicIdSize);
		else
			frame.append(kPublicIdSize, '\0');

		const size_t count = recipients.size();
		frame.push_back(static_cast<char>(count & 0xFF));
		frame.push_back(static_cast<char>((count >> 8) & 0xFF));
		for( const Id& id : recipients )
			frame.append(reinterpret_cast<const char*>(id.data()), kPublicIdSize);
		if( packet != nullptr && packetSize > 0 )
			frame.append(static_cast<const char*>(packet), packetSize);
		return frame;
	}

	//***************************************************************************
	// @struct SFrameView
	// @brief ParseFrame()이 돌려주는 읽기 전용 뷰. payload가 가리키는 메모리는 프레임 문자열이 소유한다.
	//***************************************************************************
	struct SFrameView
	{
		EKind				kind = EKind::Packet;
		bool				hasSender = false;
		Id					sender{};
		size_t				count = 0;
		const uint8_t* ids = nullptr;			// count × kPublicIdSize 바이트
		const uint8_t* packet = nullptr;		// kind==Packet일 때의 본문
		size_t				packetSize = 0;

		Id RecipientAt(size_t index) const
		{
			Id id{};
			std::memcpy(id.data(), ids + index * kPublicIdSize, kPublicIdSize);
			return id;
		}
	};

	//***************************************************************************
	// @brief 프레임을 검증하며 해석한다. 버전/종류/길이가 어긋나면 false.
	// @details Packet 본문은 첫 2바이트(PacketHeader.size, 리틀엔디안)가 본문 길이와 정확히 같아야 하고
	//          헤더(4바이트)보다 커야 한다. PresenceOffline은 본문이 없어야 한다.
	//***************************************************************************
	inline bool ParseFrame(const std::string& payload, SFrameView& out)
	{
		if( payload.size() < kFixedHeaderSize )
			return false;

		const uint8_t* p = reinterpret_cast<const uint8_t*>(payload.data());
		if( p[0] != kFrameVersion || p[1] > static_cast<uint8_t>(EKind::PresenceOffline) )
			return false;

		out.kind = static_cast<EKind>(p[1]);
		out.hasSender = (p[2] & kFlagHasSender) != 0;
		std::memcpy(out.sender.data(), p + 3, kPublicIdSize);
		out.count = static_cast<size_t>(p[3 + kPublicIdSize]) | (static_cast<size_t>(p[3 + kPublicIdSize + 1]) << 8);
		if( out.count == 0 )
			return false;

		const size_t bodyOffset = kFixedHeaderSize + out.count * kPublicIdSize;
		if( payload.size() < bodyOffset )
			return false;

		out.ids = p + kFixedHeaderSize;
		const size_t bodySize = payload.size() - bodyOffset;
		if( out.kind == EKind::PresenceOffline )
		{
			out.packet = nullptr;
			out.packetSize = 0;
			return bodySize == 0;
		}

		if( bodySize < 4 )	// PacketHeader(size, type)
			return false;
		const size_t declared = static_cast<size_t>(p[bodyOffset]) | (static_cast<size_t>(p[bodyOffset + 1]) << 8);
		if( declared < 4 || declared != bodySize )
			return false;

		out.packet = p + bodyOffset;
		out.packetSize = bodySize;
		return true;
	}
}

#endif // ndef CHATRELAYFRAME_H