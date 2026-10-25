
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

#include "ChatPacketTypes.h"

#include <Util/FixedBufUtil.h>

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

//***************************************************************************
// @brief FixedBuf::CopyStr와 같되, 버퍼가 작아 잘려야 할 때 UTF-8 문자 중간이 아니라
//        문자 경계에서 자른다(깨진 문자가 남지 않는다). 남는 버퍼는 0으로 채운다.
// @details FixedBuf::CopyStr는 바이트 단위로 자르므로 닉네임/방 이름/메시지처럼 한글이
//          들어갈 수 있는 필드에는 이 함수를 쓴다. ASCII만 있으면 결과가 같다.
// @param dst 복사 대상 버퍼
// @param dstSize dst의 전체 크기(NUL 포함 바이트 수)
// @param src 복사할 UTF-8 문자열
// @return 전부 복사됐으면 true, 잘렸으면 false.
//***************************************************************************
inline bool CopyStrUtf8(char* dst, size_t dstSize, std::string_view src) noexcept
{
	if( dstSize == 0 )
		return src.empty();

	size_t len = (src.size() < dstSize - 1) ? src.size() : dstSize - 1;
	if( len < src.size() )
	{
		// 잘리는 지점의 첫 바이트가 연속 바이트(10xxxxxx)면 문자 중간이므로 그 문자의 시작까지 물린다.
		while( len > 0 && (static_cast<unsigned char>(src[len]) & 0xC0) == 0x80 )
			--len;
	}

	if( len > 0 )
		::memcpy(dst, src.data(), len);
	::memset(dst + len, 0, dstSize - len);
	return len == src.size();
}

//***************************************************************************
// @brief char[N] 버전(CopyStrUtf8(char*, size_t, string_view)의 배열 편의 함수).
//***************************************************************************
template<size_t N>
bool CopyStrUtf8(char(&dst)[N], std::string_view src) noexcept
{
	static_assert(N > 0, "CopyStrUtf8: zero-length buffer");
	return CopyStrUtf8(dst, N, src);
}

#endif // ndef UC_CHATPACKETUTIL_H