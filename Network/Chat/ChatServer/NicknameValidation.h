
//***************************************************************************
// NicknameValidation.h : 닉네임 형식 검증(UTF-8 구조 + 허용 문자셋)
//
//***************************************************************************

#ifndef UC_NICKNAMEVALIDATION_H
#define UC_NICKNAMEVALIDATION_H

#include "ChatPacket.h"		// kNicknameBytes

#include <cctype>
#include <cstdint>
#include <cstddef>

//***************************************************************************
// [설계 노트] 원래 AccountDBHandler.cpp 안에 있던 걸 여기로 뺐다 —
// ChangeNicknameDBHandler.cpp(닉네임 변경)도 정확히 같은 검증 규칙을
// 써야 하는데, 익명 네임스페이스 안에 있으면 파일 밖에서 재사용할 수
// 없었기 때문이다.
//***************************************************************************
namespace NicknameValidation
{
	//***************************************************************************
	// @brief 코드포인트 하나가 닉네임에 허용되는 문자인지 판정합니다.
	// @details 허용: ASCII 영문/숫자/밑줄, 한글 완성형 음절(U+AC00~U+D7A3),
	//          한글 호환 자모(U+3131~U+318E — "ㄱ", "ㅏ"처럼 낱자만 입력하는
	//          경우까지 지원하기 위함).
	//***************************************************************************
	inline bool IsAllowedCodepoint(uint32_t cp)
	{
		if( cp < 0x80 )
			return std::isalnum(static_cast<int>(cp)) != 0 || cp == '_';

		if( cp >= 0xAC00 && cp <= 0xD7A3 )   // 한글 완성형 음절
			return true;

		if( cp >= 0x3131 && cp <= 0x318E )   // 한글 호환 자모
			return true;

		return false;
	}

	//***************************************************************************
	// @brief 닉네임 형식을 검증합니다: UTF-8로 인코딩된 영문/숫자/밑줄/한글,
	//        1~16글자(코드포인트 기준 — 바이트 수가 아님).
	// @details SQL 인젝션 방어의 1차 방어선이기도 하다. 실제 방어는 호출부의
	//          PrepareQuery+BindParamInput(파라미터 바인딩)이 담당한다 —
	//          이 화이트리스트는 형식이 이상한 닉네임을 조기에 걸러 DB
	//          워커 부하를 아끼기 위한 것.
	//          UTF-8 자체의 구조적 유효성(리딩/연속 바이트 개수, 오버롱
	//          인코딩, 서로게이트 범위 등)도 여기서 함께 검증한다.
	//***************************************************************************
	inline bool IsValidNickname(const char* nickname, size_t byteLen)
	{
		if( byteLen == 0 || byteLen >= kNicknameBytes )
			return false;

		constexpr size_t kMaxCodepoints = 16;
		size_t codepointCount = 0;
		size_t i = 0;

		while( i < byteLen )
		{
			if( ++codepointCount > kMaxCodepoints )
				return false;

			const unsigned char b0 = static_cast<unsigned char>(nickname[i]);
			uint32_t cp = 0;
			size_t seqLen = 0;

			if( (b0 & 0x80) == 0x00 ) { cp = b0;        seqLen = 1; }
			else if( (b0 & 0xE0) == 0xC0 ) { cp = b0 & 0x1F; seqLen = 2; }
			else if( (b0 & 0xF0) == 0xE0 ) { cp = b0 & 0x0F; seqLen = 3; }
			else if( (b0 & 0xF8) == 0xF0 ) { cp = b0 & 0x07; seqLen = 4; }
			else
				return false; // 잘못된 리딩 바이트

			if( i + seqLen > byteLen )
				return false; // 시퀀스가 버퍼 끝에서 잘림(경계값)

			for( size_t j = 1; j < seqLen; ++j )
			{
				const unsigned char bn = static_cast<unsigned char>(nickname[i + j]);
				if( (bn & 0xC0) != 0x80 )
					return false; // 연속 바이트 형식이 아님
				cp = (cp << 6) | (bn & 0x3F);
			}

			// 오버롱 인코딩 및 서로게이트 범위/유효 범위(0x10FFFF) 초과 거부.
			static constexpr uint32_t kMinForLen[5] = { 0, 0, 0x80, 0x800, 0x10000 };
			if( cp < kMinForLen[seqLen] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) )
				return false;

			if( !IsAllowedCodepoint(cp) )
				return false;

			i += seqLen;
		}

		return codepointCount > 0;
	}
}

#endif // ndef UC_NICKNAMEVALIDATION_H