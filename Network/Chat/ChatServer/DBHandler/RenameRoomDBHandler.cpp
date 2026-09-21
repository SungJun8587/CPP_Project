
//***************************************************************************
// RenameRoomDBHandler.cpp : DBASYNC_RENAME_ROOM_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBRenameRoomRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방 이름을 바꾼다. DeleteRoomDBHandler.cpp와 동일한 이유로
//        소유권 확인(SELECT)과 실제 변경(UPDATE)을 2단계로 나눴다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_RenameRoom)
{
	ST_RENAME_ROOM_REQ* req = static_cast<ST_RENAME_ROOM_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> requesterPublicId{};
	::memcpy(requesterPublicId.data(), req->requesterPublicId, requesterPublicId.size());
	const std::string requesterPublicIdHex = Crypto::CCryptoUtil::ToHex(requesterPublicId.data(), requesterPublicId.size());
	_tstring requesterPublicIdHexT = Utf8ToTString(requesterPublicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_RenameRoom: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError);
		return EDBReturnType::INVALID;
	}

	// 1. 존재 여부 + 소유권 확인.
	if( !guard->PrepareQuery(_T("SELECT owner_public_id FROM rooms WHERE room_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError);
		return EDBReturnType::INVALID;
	}

	guard->BindParamInput(1, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError);
		return EDBReturnType::INVALID;
	}

	bool found = false;
	_tstring currentOwnerHexT;
	if( guard->Fetch() )
	{
		found = true;
		TCHAR ownerBuf[64] = {};
		int32 ownerBufLen = static_cast<int32>(sizeof(ownerBuf));
		if( guard->GetData(1, ownerBuf, ownerBufLen) )
			currentOwnerHexT = ownerBuf;
	}
	guard->ClearStmt();

	if( !found )
	{
		if( req->onComplete )
			req->onComplete(ERoomResult::RoomNotFound);
		return EDBReturnType::OK;
	}

	if( currentOwnerHexT != requesterPublicIdHexT )
	{
		if( req->onComplete )
			req->onComplete(ERoomResult::NotOwner);
		return EDBReturnType::OK;
	}

	// 2. 소유권 확인 통과 — 실제 이름 변경.
	_tstring newNameT = Utf8ToTString(req->newName);

	if( !guard->PrepareQuery(_T("UPDATE rooms SET name = ? WHERE room_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError);
		return EDBReturnType::INVALID;
	}

	SQLLEN nameLenInd = SQL_NTS;
	guard->BindParamInput(1, newNameT.c_str(), nameLenInd);
	guard->BindParamInput(2, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError);
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ERoomResult::Ok);
	return EDBReturnType::OK;
}