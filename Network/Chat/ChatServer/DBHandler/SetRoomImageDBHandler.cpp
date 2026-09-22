
//***************************************************************************
// SetRoomImageDBHandler.cpp : DBASYNC_SET_ROOM_IMAGE_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBSetRoomImageRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방 프로필 이미지를 설정/교체/해제한다.
// @details DeleteRoomDBHandler.cpp/RenameRoomDBHandler.cpp와 동일한 이유로
//          소유권 확인(SELECT)과 실제 변경(UPDATE)을 2단계로 나눴다 — 이
//          SELECT가 동시에 "교체되기 전 이미지 값"도 가져와서 onComplete로
//          돌려준다(호출부가 예전 파일 삭제 예약에 쓸 수 있도록).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_SetRoomImage)
{
	ST_SET_ROOM_IMAGE_REQ* req = static_cast<ST_SET_ROOM_IMAGE_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> requesterPublicId{};
	::memcpy(requesterPublicId.data(), req->requesterPublicId, requesterPublicId.size());
	const std::string requesterPublicIdHex = Crypto::CCryptoUtil::ToHex(requesterPublicId.data(), requesterPublicId.size());
	_tstring requesterPublicIdHexT = Utf8ToTString(requesterPublicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_SetRoomImage: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, std::string());
		return EDBReturnType::INVALID;
	}

	// 1. 존재 여부 + 소유권 확인 + 교체되기 전 이미지 값 조회.
	if( !guard->PrepareQuery(_T("SELECT owner_public_id, image_ref FROM rooms WHERE room_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, std::string());
		return EDBReturnType::INVALID;
	}

	guard->BindParamInput(1, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, std::string());
		return EDBReturnType::INVALID;
	}

	bool found = false;
	_tstring currentOwnerHexT;
	_tstring oldImageRefT;
	if( guard->Fetch() )
	{
		found = true;
		TCHAR ownerBuf[64] = {};
		int32 ownerBufLen = static_cast<int32>(sizeof(ownerBuf));
		if( guard->GetData(1, ownerBuf, ownerBufLen) )
			currentOwnerHexT = ownerBuf;

		// image_ref는 NULL일 수 있다 — GetData()가 실패해도(NULL) 방 자체가
		// 없는 건 아니므로 found는 그대로 true 유지, oldImageRefT만 빈 채로 둔다.
		TCHAR imageRefBuf[512] = {};
		int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
		if( guard->GetData(2, imageRefBuf, imageRefBufLen) )
			oldImageRefT = imageRefBuf;
	}
	guard->ClearStmt();

	if( !found )
	{
		if( req->onComplete )
			req->onComplete(ERoomResult::RoomNotFound, std::string());
		return EDBReturnType::OK;
	}

	if( currentOwnerHexT != requesterPublicIdHexT )
	{
		if( req->onComplete )
			req->onComplete(ERoomResult::NotOwner, std::string());
		return EDBReturnType::OK;
	}

	// 2. 소유권 확인 통과 — 실제 이미지 값 갱신(빈 문자열이면 NULL로 해제).
	const bool clearing = req->imageRef.empty();

	if( clearing )
	{
		if( !guard->PrepareQuery(_T("UPDATE rooms SET image_ref = NULL WHERE room_id = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}

		guard->BindParamInput(1, req->roomId);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();
	}
	else
	{
		if( !guard->PrepareQuery(_T("UPDATE rooms SET image_ref = ? WHERE room_id = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}

		_tstring newImageRefT = Utf8ToTString(req->imageRef);

		SQLLEN imageRefLenInd = SQL_NTS;
		guard->BindParamInput(1, newImageRefT.c_str(), imageRefLenInd);
		guard->BindParamInput(2, req->roomId);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();
	}

	if( req->onComplete )
		req->onComplete(ERoomResult::Ok, TStringToUtf8(oldImageRefT));
	return EDBReturnType::OK;
}