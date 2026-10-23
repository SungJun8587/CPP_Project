
//***************************************************************************
// ChatPacketUtil.h : 채팅 패킷 조립/해석 보조 함수 (header-only)
//
// @details
// 서버/클라이언트가 함께 쓰는 패킷 헤더 채움, public_id 변환·복사 등을 모은 것입니다.
// 고정폭 문자열 복사/읽기와 16진 변환 같은 프로토콜 비의존 기능은 UtilCore의
// Util/FixedBufUtil.h(FixedBuf::CopyStr / ReadStr / ToHex / FromHex)를 사용합니다.
//  - public_id는 와이어/세션에서는 raw 16바이트, DB 바인딩 시점에는 소문자 16진
//    32자(CHAR(32))로 다룹니다. 이 변환은 PublicIdToHex / HexToPublicId가 전담합니다.
//***************************************************************************

#ifndef UC_CHATPACKETUTIL_H
#define UC_CHATPACKETUTIL_H

#include <Util/FixedBufUtil.h>
#include "ChatPacketTypes.h"

#include <array>
#include <cstring>
#include <string>
#include <string_view>

// 계정 식별자(public_id) 원문 16바이트
using PublicId = std::array<BYTE, kPublicIdBytes>;

//***************************************************************************
// @brief std::array의 바이트를 같은 크기의 C 배열(패킷/요청 구조체 필드)로 복사합니다.
// @details memcpy(dst, src.data(), src.size())와 달리 두 크기가 컴파일 타임에 같아야
//          하므로, 필드 크기가 바뀌었는데 복사 길이가 어긋나는 실수를 막아 준다.
// @param dst 복사 대상 필드
// @param src 복사할 원본
//***************************************************************************
template<size_t N>
void CopyBytes(BYTE(&dst)[N], const std::array<BYTE, N>& src)
{
	::memcpy(dst, src.data(), N);
}

//***************************************************************************
// @brief public_id를 패킷/요청 구조체의 public_id 필드로 복사합니다.
// @param dst 복사 대상 필드(BYTE[kPublicIdBytes])
// @param src 복사할 계정 식별자
//***************************************************************************
inline void CopyPublicId(BYTE(&dst)[kPublicIdBytes], const PublicId& src)
{
	CopyBytes(dst, src);
}

//***************************************************************************
// @brief 패킷 헤더(size, type)를 채웁니다.
// @param p 대상 패킷 구조체(PacketHeader를 첫 멤버로 가진 구조체)
// @param type 패킷 종류
//***************************************************************************
template<typename TPacket>
void InitPacket(TPacket& p, EChatPacketType type)
{
	static_assert(sizeof(TPacket) <= 0xFFFF, "InitPacket: packet size exceeds uint16 range");
	p.size = static_cast<uint16>(sizeof(TPacket));
	p.type = static_cast<uint16>(type);
}

//***************************************************************************
// @brief 패킷 필드의 raw 바이트(kPublicIdBytes)를 PublicId로 복사합니다.
// @param raw 패킷의 public_id 필드. 최소 kPublicIdBytes 바이트가 유효해야 한다.
// @return 복사된 PublicId
//***************************************************************************
inline PublicId ToPublicId(const BYTE* raw)
{
	return FixedBuf::ToArray<kPublicIdBytes>(raw);
}

//***************************************************************************
// @brief public_id를 소문자 16진 문자열(32자)로 변환합니다.
// @param publicId 변환할 계정 식별자
// @return 소문자 16진 문자열
//***************************************************************************
inline std::string PublicIdToHex(const PublicId& publicId)
{
	return FixedBuf::ToHex(publicId);
}

//***************************************************************************
// @brief 패킷 필드의 raw public_id를 소문자 16진 문자열(32자)로 변환합니다.
// @param raw 패킷의 public_id 필드. 최소 kPublicIdBytes 바이트가 유효해야 한다.
// @return 소문자 16진 문자열
//***************************************************************************
inline std::string PublicIdToHex(const BYTE* raw)
{
	return FixedBuf::ToHex(raw, kPublicIdBytes);
}

//***************************************************************************
// @brief 16진 문자열(32자)을 public_id로 되돌립니다.
// @param hex 대소문자 모두 허용
// @param out 결과. 실패 시 변경되지 않는다.
// @return 성공 시 true. 길이가 32가 아니거나 16진수가 아닌 문자가 있으면 false.
//***************************************************************************
inline bool HexToPublicId(std::string_view hex, PublicId& out)
{
	return FixedBuf::FromHex(hex, out);
}

#endif // ndef UC_CHATPACKETUTIL_H