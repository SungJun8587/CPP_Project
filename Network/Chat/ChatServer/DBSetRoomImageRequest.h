
//***************************************************************************
// DBSetRoomImageRequest.h : 방 프로필 이미지 설정 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBSETROOMIMAGEREQUEST_H
#define UC_DBSETROOMIMAGEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"

#include <functional>
#include <string>

//***************************************************************************
// @struct ST_SET_ROOM_IMAGE_REQ
// @brief 방 프로필 이미지를 설정/교체/해제한다. requesterPublicId가 그 방의
//        현재 방장이어야 한다. 갤러리가 아니라 단일 값이라 — 새 값을
//        저장하면 예전 값은 그냥 덮어써진다(user_profile_images처럼 여러
//        행을 관리하지 않음).
//***************************************************************************
struct ST_SET_ROOM_IMAGE_REQ : public st_DBAsyncRq
{
	ST_SET_ROOM_IMAGE_REQ()
	{
		callIdent = kDbCallIdent_SetRoomImage;
		bReTry = false;
	}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상
	std::string	imageRef;									// 새 이미지 참조(상대경로 또는 외부 URL). 빈 문자열이면 해제

	// onComplete: oldImageRef는 교체되기 전 값(빈 문자열일 수 있음) — 호출부가
	// 그 값이 우리 파일 서버 소유면 실제 파일 삭제를 예약하는 데 쓴다.
	std::function<void(ERoomResult result, const std::string& oldImageRef)>	onComplete;
};

#endif // ndef UC_DBSETROOMIMAGEREQUEST_H