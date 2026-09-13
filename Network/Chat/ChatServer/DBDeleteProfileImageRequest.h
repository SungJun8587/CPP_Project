
//***************************************************************************
// DBDeleteProfileImageRequest.h : 프로필 이미지 삭제 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBDELETEPROFILEIMAGEREQUEST_H
#define UC_DBDELETEPROFILEIMAGEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>
#include <string>

constexpr BYTE kDbCallIdent_DeleteProfileImage = 205;

//***************************************************************************
// @struct ST_DELETE_PROFILE_IMAGE_REQ
// @brief 갤러리에서 이미지 하나(imageId)를 삭제. WHERE에 user_public_id도
// 같이 걸어 소유자 검증을 겸한다.
//***************************************************************************
struct ST_DELETE_PROFILE_IMAGE_REQ : public st_DBAsyncRq
{
	ST_DELETE_PROFILE_IMAGE_REQ()
	{
		callIdent = kDbCallIdent_DeleteProfileImage;
		bReTry = false;
	}

	BYTE	publicId[kPublicIdBytes] = {};
	int64	imageId = 0;

	// wasActive/deletedImageRef: 삭제 "전" 시점의 값을 그대로 돌려준다 —
	// 호출부가 이걸로 대표 이미지를 지운 거면 세션의 표시용 값을 비우는
	// 데 쓴다. deletedImageRef가 가리키던 실제 파일을 지우는 건 이제 이
	// 서버의 책임이 아니다(이미지 저장을 별도 파일 서버로 분리했으므로) —
	// 그래도 "무엇을 지웠었는지" 정보 자체는 로그/추후 확장을 위해 그대로
	// 콜백에 실어 보낸다.
	std::function<void(ELoginResult result, int64 imageId, bool wasActive, const std::string& deletedImageRef)>	onComplete;
};

#endif // ndef UC_DBDELETEPROFILEIMAGEREQUEST_H