
//***************************************************************************
// FileServerKeys.h : 채팅 서버 / 파일 서버가 Redis로 주고받는 키 규약 (공유, header-only)
//
// @details
// 두 서버는 직접 통신하지 않고 같은 Redis만 바라본다. 키 이름과 TTL이 양쪽에서
// 어긋나면 조용히 깨지므로 한 곳에서만 정의한다.
//   - 업로드 토큰: 채팅 서버가 SET {kUploadTokenPrefix}{token} {public_id 16진} EX {TTL},
//                  파일 서버가 업로드 시 한 번에 조회·삭제(1회용)한다.
//   - 삭제 예약  : 채팅 서버가 RPUSH, 파일 서버가 LPOP 폴링으로 소비한다.
//                  삭제에 실패한 항목은 파일 서버가 실패 목록에 보관한다.
//***************************************************************************

#ifndef FILESERVERKEYS_H
#define FILESERVERKEYS_H

#include <string_view>

namespace FileServerKeys
{
	inline constexpr const char* kUploadTokenPrefix = "UploadToken:";
	inline constexpr int kUploadTokenTtlSec = 60;
	inline constexpr size_t kUploadTokenHexLen = 64;	// 무작위 32바이트의 16진 표기

	// 저장 파일 상대 경로 규약: "{ownerId}/{무작위 16진 32자}.{영숫자 1~8자}"
	inline constexpr size_t kStoredStemHexLen = 32;
	inline constexpr size_t kStoredExtMaxLen = 8;
	inline constexpr size_t kOwnerIdMaxLen = 64;

	inline bool IsHexDigit(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	}

	inline bool IsAlnum(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	// 정확히 len자의 16진 문자열인지 확인한다.
	inline bool IsHexString(std::string_view s, size_t len)
	{
		if( s.size() != len )
			return false;
		for( char c : s )
			if( !IsHexDigit(c) )
				return false;
		return true;
	}

	// 채팅 서버가 발급하는 토큰 형식(16진 64자)인지 확인한다. Redis를 부르기 전에 거르는 용도.
	inline bool IsValidUploadToken(std::string_view token)
	{
		return IsHexString(token, kUploadTokenHexLen);
	}

	// 영숫자/'_'/'-'로만 이뤄진 1~kOwnerIdMaxLen자 (계정 public_id 16진 등)
	inline bool IsValidOwnerId(std::string_view owner)
	{
		if( owner.empty() || owner.size() > kOwnerIdMaxLen )
			return false;
		for( char c : owner )
			if( !IsAlnum(c) && c != '_' && c != '-' )
				return false;
		return true;
	}

	// 저장 파일명: "{16진 32자}.{영숫자 1~8자}". 경로 조작과 Windows 예약어(NUL.png 등)가 모두 걸러진다.
	inline bool IsValidStoredName(std::string_view name)
	{
		if( name.size() < kStoredStemHexLen + 2 || name.size() > kStoredStemHexLen + 1 + kStoredExtMaxLen )
			return false;
		if( !IsHexString(name.substr(0, kStoredStemHexLen), kStoredStemHexLen) || name[kStoredStemHexLen] != '.' )
			return false;
		for( size_t i = kStoredStemHexLen + 1; i < name.size(); ++i )
			if( !IsAlnum(name[i]) )
				return false;
		return true;
	}

	// "{ownerId}/{저장 파일명}" 형태인지 확인한다. 파일 서버 저장소와 채팅 서버가 같은 규칙을 쓴다.
	inline bool IsValidRelativePath(std::string_view path)
	{
		const size_t slash = path.find('/');
		if( slash == std::string_view::npos )
			return false;
		return IsValidOwnerId(path.substr(0, slash)) && IsValidStoredName(path.substr(slash + 1));
	}

	inline constexpr const char* kPendingDeletionsKey = "FileServer:PendingDeletions";
	inline constexpr const char* kFailedDeletionsKey = "FileServer:FailedDeletions";
	// 소비자가 꺼내 처리하는 중인 항목. 꺼내는 것과 여기 넣는 것이 한 스크립트로 원자적이라, 응답이 유실되거나
	// 처리 도중 종료돼도 항목이 사라지지 않고 다음 기동 때 PendingDeletions 앞으로 되돌려진다.
	inline constexpr const char* kProcessingDeletionsKey = "FileServer:ProcessingDeletions";
}

#endif // ndef FILESERVERKEYS_H