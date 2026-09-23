
//***************************************************************************
// GetRoomInfoDBHandler.cpp : DBASYNC_GET_ROOM_INFO_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Util/EncodingConvert.h>

#include "DBGetRoomInfoRequest.h"

//***************************************************************************
// @brief 방 하나(roomId)의 이름/방장 닉네임/이미지를 조회한다.
// @details ListRoomsDBHandler.cpp와 정확히 같은 SELECT(JOIN으로 방장의
//          "지금" 닉네임을 같이 가져옴)에 WHERE room_id=?만 붙인 버전이다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_GetRoomInfo)
{
	ST_GET_ROOM_INFO_REQ* req = static_cast<ST_GET_ROOM_INFO_REQ*>(pStAsync);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_GetRoomInfo: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, false, SRoomListEntry());
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T(
		"SELECT r.room_id, r.name, r.owner_public_id, u.nickname, r.image_ref "
		"FROM rooms r JOIN users u ON r.owner_public_id = u.public_id "
		"WHERE r.room_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, false, SRoomListEntry());
		return EDBReturnType::INVALID;
	}

	guard->BindParamInput(1, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, false, SRoomListEntry());
		return EDBReturnType::INVALID;
	}

	bool found = false;
	SRoomListEntry entry;

	if( guard->Fetch() )
	{
		found = true;

		TCHAR roomIdBuf[16] = {};
		int32 roomIdBufLen = static_cast<int32>(sizeof(roomIdBuf));
		if( guard->GetData(1, roomIdBuf, roomIdBufLen) )
			entry.roomId = _ttoi(roomIdBuf);

		TCHAR nameBuf[64] = {}; // VARCHAR(50) + 여유
		int32 nameBufLen = static_cast<int32>(sizeof(nameBuf));
		if( guard->GetData(2, nameBuf, nameBufLen) )
			entry.name = TStringToUtf8(_tstring(nameBuf));

		TCHAR ownerIdBuf[64] = {};
		int32 ownerIdBufLen = static_cast<int32>(sizeof(ownerIdBuf));
		if( guard->GetData(3, ownerIdBuf, ownerIdBufLen) )
			entry.ownerPublicId = TStringToUtf8(_tstring(ownerIdBuf));

		TCHAR nicknameBuf[32] = {}; // VARCHAR(16) + 여유
		int32 nicknameBufLen = static_cast<int32>(sizeof(nicknameBuf));
		if( guard->GetData(4, nicknameBuf, nicknameBufLen) )
			entry.ownerNickname = TStringToUtf8(_tstring(nicknameBuf));

		// image_ref는 NULL일 수 있다(기본 이미지) — GetData() 실패를 "행
		// 자체가 없다"는 뜻으로 취급하지 않고, 빈 문자열로만 둔다.
		TCHAR imageRefBuf[512] = {};
		int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
		if( guard->GetData(5, imageRefBuf, imageRefBufLen) )
			entry.imageRef = TStringToUtf8(_tstring(imageRefBuf));
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, found, entry);
	return EDBReturnType::OK;
}