
//***************************************************************************
// DBListProfileImagesRequest.h : 프로필 이미지 갤러리 목록 조회 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBLISTPROFILEIMAGESREQUEST_H
#define UC_DBLISTPROFILEIMAGESREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>
#include <string>
#include <vector>

constexpr BYTE kDbCallIdent_ListProfileImages = 203;

//***************************************************************************
// @brief 갤러리 항목 하나를 DB 왕복용으로 담는 평범한 구조체(프로토콜의
//        ListProfileImagesItemResPacket과는 별개 — 이쪽은 고정폭 char 배열이
//        아니라 std::string을 쓴다).
//***************************************************************************
struct SProfileImageEntry
{
	int64		imageId = 0;
	std::string	imageRef;
	bool		isActive = false;
};

//***************************************************************************
// @struct ST_LIST_PROFILE_IMAGES_REQ
// @brief 로그인된 계정(publicId로 식별)이 갖고 있는 프로필 이미지 전체 목록을 조회.
//***************************************************************************
struct ST_LIST_PROFILE_IMAGES_REQ : public st_DBAsyncRq
{
	ST_LIST_PROFILE_IMAGES_REQ()
	{
		callIdent = kDbCallIdent_ListProfileImages;
		bReTry = false;
	}

	BYTE	publicId[kPublicIdBytes] = {};

	std::function<void(ELoginResult result, const std::vector<SProfileImageEntry>& images)>	onComplete;
};

#endif // ndef UC_DBLISTPROFILEIMAGESREQUEST_H