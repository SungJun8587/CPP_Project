
//***************************************************************************
// ListRoomsDBHandler.cpp : DBASYNC_LIST_ROOMS_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Util/EncodingConvert.h>

#include "DBListRoomsRequest.h"

//***************************************************************************
// @brief 존재하는 모든 방을 최신순으로 조회합니다. 방장의 "지금" 닉네임을
//        같이 돌려주기 위해 users 테이블과 JOIN한다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ListRooms)
{
	ST_LIST_ROOMS_REQ* req = static_cast<ST_LIST_ROOMS_REQ*>(pStAsync);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ListRooms: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T(
		"SELECT r.room_id, r.name, r.owner_public_id, u.nickname "
		"FROM rooms r JOIN users u ON r.owner_public_id = u.public_id "
		"ORDER BY r.created_at DESC")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
		return EDBReturnType::INVALID;
	}

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
		return EDBReturnType::INVALID;
	}

	std::vector<SRoomListEntry> rooms;
	while( guard->Fetch() )
	{
		TCHAR roomIdBuf[16] = {};
		int32 roomIdBufLen = static_cast<int32>(sizeof(roomIdBuf));
		if( !guard->GetData(1, roomIdBuf, roomIdBufLen) )
			continue;

		TCHAR nameBuf[64] = {}; // VARCHAR(50) + 여유
		int32 nameBufLen = static_cast<int32>(sizeof(nameBuf));
		if( !guard->GetData(2, nameBuf, nameBufLen) )
			continue;

		TCHAR ownerIdBuf[64] = {};
		int32 ownerIdBufLen = static_cast<int32>(sizeof(ownerIdBuf));
		if( !guard->GetData(3, ownerIdBuf, ownerIdBufLen) )
			continue;

		TCHAR nicknameBuf[32] = {}; // VARCHAR(16) + 여유
		int32 nicknameBufLen = static_cast<int32>(sizeof(nicknameBuf));
		if( !guard->GetData(4, nicknameBuf, nicknameBufLen) )
			continue;

		SRoomListEntry entry;
		entry.roomId = _ttoi(roomIdBuf);
		entry.name = TStringToUtf8(_tstring(nameBuf));
		entry.ownerPublicId = TStringToUtf8(_tstring(ownerIdBuf));
		entry.ownerNickname = TStringToUtf8(_tstring(nicknameBuf));
		rooms.push_back(std::move(entry));
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, rooms);
	return EDBReturnType::OK;
}