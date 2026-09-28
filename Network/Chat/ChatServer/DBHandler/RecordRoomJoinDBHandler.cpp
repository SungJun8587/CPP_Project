
//***************************************************************************
// RecordRoomJoinDBHandler.cpp : kDbCallIdent_RecordRoomJoin 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBRecordRoomJoinRequest.h"

#include <cstring>

//***************************************************************************
// @brief 사용자가 방에 입장했음을 room_members에 기록한다.
// @details 처음 입장이면 행을 추가하고, 이미 있으면 last_entered_at만
//          현재 시각으로 갱신한다(INSERT ... ON DUPLICATE KEY UPDATE).
//          입장 직후 그 방이 삭제된 극히 좁은 경합에서는 외래 키 위반으로
//          Execute()가 실패할 수 있는데, 삭제된 방에 대한 기록이 남지 않는
//          것이 오히려 올바른 결과이므로 로그만 남기고 넘어간다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_RecordRoomJoin)
{
	ST_RECORD_ROOM_JOIN_REQ* req = static_cast<ST_RECORD_ROOM_JOIN_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> requesterPublicId{};
	::memcpy(requesterPublicId.data(), req->requesterPublicId, requesterPublicId.size());
	const _tstring requesterPublicIdHexT = Utf8ToTString(Crypto::CCryptoUtil::ToHex(requesterPublicId.data(), requesterPublicId.size()));

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_RecordRoomJoin: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError);
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T(
		"INSERT INTO room_members (room_id, user_public_id) VALUES (?, ?) "
		"ON DUPLICATE KEY UPDATE last_entered_at = CURRENT_TIMESTAMP")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError);
		return EDBReturnType::INVALID;
	}

	SQLLEN ownerLenInd = SQL_NTS;
	guard->BindParamInput(1, req->roomId);
	guard->BindParamInput(2, requesterPublicIdHexT.c_str(), ownerLenInd);

	if( !guard->Execute() )
	{
		LOG_INFO(_T("kDbCallIdent_RecordRoomJoin: 입장 기록 실패(roomId=%d) — 그 사이 방이 삭제됐을 수 있음"), req->roomId);
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError);
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok);
	return EDBReturnType::OK;
}