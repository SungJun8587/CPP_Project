
//***************************************************************************
// DBSetProfileImageUrlRequest.h : 프로필 이미지 URL 설정 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBSETPROFILEIMAGEURLREQUEST_H
#define UC_DBSETPROFILEIMAGEURLREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes, kProfileImageUrlBytes

#include <functional>
#include <string>
#include <array>

//***************************************************************************
// @brief 이 DB 비동기 시스템 안에서 프로필 이미지 URL 설정 요청을 식별할 callIdent.
// @details kDbCallIdent_Signup(200)/kDbCallIdent_ChangeNickname(201)과
//          겹치지 않게 배정.
//***************************************************************************
constexpr BYTE kDbCallIdent_SetProfileImageUrl = 202;

//***************************************************************************
// @struct ST_SET_PROFILE_IMAGE_URL_REQ
// @brief 로그인된 계정(publicId로 식별)에 외부 URL을 새 갤러리 항목으로
//        등록하고 대표(status=1)로 지정하는 요청.
// @details 빈 문자열이면 "대표 이미지 해제"(기존 status=1 행을 0으로만
// 내림, 새로 등록하지 않음)로 처리한다. ChangeNicknameDBHandler.cpp와는
// 달리 UPDATE 한 번이 아니라 "기존 대표 해제 + 신규 등록"의 두 단계
// 쿼리다(SetProfileImageUrlDBHandler.cpp 참고).
//***************************************************************************
struct ST_SET_PROFILE_IMAGE_URL_REQ : public st_DBAsyncRq
{
	ST_SET_PROFILE_IMAGE_URL_REQ()
	{
		callIdent = kDbCallIdent_SetProfileImageUrl;
		bReTry = false;
	}

	BYTE	publicId[kPublicIdBytes] = {};				// 대상 계정의 안정 식별자
	char	url[kProfileImageUrlBytes] = {};			// 새 프로필 이미지 URL(UTF-8). 빈 문자열이면 해제

	std::function<void(
		ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newUrl,
		int64 newImageId)>	onComplete;					// newImageId: 새로 등록된 행의 image_id(해제 요청이면 0)
};

#endif // ndef UC_DBSETPROFILEIMAGEURLREQUEST_H