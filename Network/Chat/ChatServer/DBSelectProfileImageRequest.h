
//***************************************************************************
// DBSelectProfileImageRequest.h : 대표 프로필 이미지 선택 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBSELECTPROFILEIMAGEREQUEST_H
#define UC_DBSELECTPROFILEIMAGEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>
#include <string>

//***************************************************************************
// @struct ST_SELECT_PROFILE_IMAGE_REQ
// @brief 갤러리에 이미 있는 이미지 중 하나(imageId)를 대표(status=1)로 지정.
// @details WHERE 절에 user_public_id도 같이 걸어 소유자 검증을 겸한다 —
// 남의 image_id를 넣어도 그 행이 매치되지 않아 아무 일도 안 일어난다.
//***************************************************************************
struct ST_SELECT_PROFILE_IMAGE_REQ : public st_DBAsyncRq
{
	ST_SELECT_PROFILE_IMAGE_REQ()
	{
		callIdent = kDbCallIdent_SelectProfileImage;
		bReTry = false;
	}

	BYTE	publicId[kPublicIdBytes] = {};
	int64	imageId = 0;

	// selectedImageRef: 성공 시 새로 대표가 된 이미지의 image_ref(세션의
	// 표시용 값을 곧바로 갱신하는 데 쓰인다 — 다시 SELECT 안 해도 되게).
	std::function<void(ELoginResult result, int64 imageId, const std::string& selectedImageRef)>	onComplete;
};

#endif // ndef UC_DBSELECTPROFILEIMAGEREQUEST_H