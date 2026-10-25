
//***************************************************************************
// ImageRef.h : 이미지 URL 규약 공용 유틸 (ChatServer / FileServer 공유, header-only)
//
// @details
// 파일 서버가 노출하는 이미지의 공개 URL은 "{publicBaseUrl}/images/{상대경로}" 형식입니다.
// DB에는 호스트가 바뀌어도 유효하도록 "/images/{상대경로}"(=이미지 참조)만 저장하고,
// 클라이언트에 보낼 때 다시 완전한 URL로 복원합니다.
//  - FileServer : BuildPublicUrl  (업로드 응답에 실을 공개 URL 조립)
//  - ChatServer : ToStorable      (DB 저장 직전 호스트 제거)
//                 ToDisplay       (클라이언트 전송 직전 호스트 복원)
//                 ExtractOwnedRelativePath (우리 파일 서버 소유 이미지인지 판별)
// 이 규약이 한 곳에 있어야 양쪽 서버의 URL 조립/판별 규칙이 어긋나지 않습니다.
//  - <string>/<string_view>만 사용합니다(플랫폼/프로토콜 비의존).
//***************************************************************************

#ifndef UC_IMAGEREF_H
#define UC_IMAGEREF_H

#include <string>
#include <string_view>

namespace ImageRef
{
	// 파일 서버의 이미지 경로 접두사(요청 라우팅과 공개 URL 조립에 공통으로 사용)
	inline constexpr std::string_view kImagesPath = "/images/";

	//***************************************************************************
	// @brief 베이스 URL 끝의 "/"를 제거한다.
	// @param baseUrl 파일 서버 공개 베이스 URL (예: "http://host:8081/")
	// @return 끝 "/"가 없는 베이스 URL
	//***************************************************************************
	inline std::string TrimBaseUrl(std::string_view baseUrl)
	{
		if( !baseUrl.empty() && baseUrl.back() == '/' )
			baseUrl.remove_suffix(1);
		return std::string(baseUrl);
	}

	//***************************************************************************
	// @brief 상대 경로로 접근 가능한 공개 URL을 만든다.
	// @param baseUrl 파일 서버 공개 베이스 URL
	// @param relativePath "/images/" 뒤에 붙는 상대 경로
	// @return "{baseUrl}/images/{relativePath}"
	//***************************************************************************
	inline std::string BuildPublicUrl(std::string_view baseUrl, std::string_view relativePath)
	{
		std::string url = TrimBaseUrl(baseUrl);
		url.append(kImagesPath);
		url.append(relativePath);
		return url;
	}

	//***************************************************************************
	// @brief DB 저장용 이미지 참조로 변환한다. "{baseUrl}/..." 형태면 호스트를 잘라
	//        "/..."만 남기고, 그 외(외부 URL, 형식 불일치)는 그대로 돌려준다.
	// @param baseUrl 파일 서버 공개 베이스 URL
	// @param url 클라이언트가 보낸 URL
	// @return "/"로 시작하는 상대 경로 또는 원본 URL
	//***************************************************************************
	inline std::string ToStorable(std::string_view baseUrl, const std::string& url)
	{
		if( url.empty() || baseUrl.empty() )
			return url;

		const std::string hostPrefix = TrimBaseUrl(baseUrl);
		if( url.rfind(hostPrefix, 0) == 0 )
		{
			const std::string remainder = url.substr(hostPrefix.size());
			if( !remainder.empty() && remainder.front() == '/' )
				return remainder;
		}
		return url;
	}

	//***************************************************************************
	// @brief 클라이언트 전송용 완전한 URL로 복원한다(ToStorable의 역방향).
	//        "/"로 시작하지 않는 값(외부 URL, 예전 값)은 그대로 돌려준다.
	// @param baseUrl 파일 서버 공개 베이스 URL
	// @param imageRef DB에 저장된 이미지 참조
	// @return 완전한 URL 또는 원본 값
	//***************************************************************************
	inline std::string ToDisplay(std::string_view baseUrl, const std::string& imageRef)
	{
		if( imageRef.empty() || baseUrl.empty() || imageRef.front() != '/' )
			return imageRef;

		return TrimBaseUrl(baseUrl) + imageRef;
	}

	//***************************************************************************
	// @brief "{baseUrl}/images/{상대경로}" 형식(=우리 파일 서버 소유)이면 상대 경로를 꺼낸다.
	// @param baseUrl 파일 서버 공개 베이스 URL
	// @param url 저장된 이미지 URL
	// @return 상대 경로. 외부 URL이거나 형식이 맞지 않거나 상대 경로가 비면 빈 문자열.
	//***************************************************************************
	inline std::string ExtractOwnedRelativePath(std::string_view baseUrl, const std::string& url)
	{
		if( url.empty() || baseUrl.empty() )
			return std::string();

		std::string prefix = TrimBaseUrl(baseUrl);
		prefix.append(kImagesPath);
		if( url.rfind(prefix, 0) != 0 )
			return std::string();

		return url.substr(prefix.size());
	}
}

#endif // ndef UC_IMAGEREF_H