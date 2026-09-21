
//***************************************************************************
// TransferRoomOwnerDBHandler.cpp : DBASYNC_TRANSFER_ROOM_OWNER_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBTransferRoomOwnerRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방장을 newOwnerPublicId로 강제 변경한다. 소유권 검증 없음 —
//        ST_TRANSFER_ROOM_OWNER_REQ 선언부의 설계 노트 참고(서버 내부
//        판단으로만 호출되는 것을 전제로 함).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_TransferRoomOwner)
{
	ST_TRANSFER_ROOM_OWNER_REQ* req = static_cast<ST_TRANSFER_ROOM_OWNER_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> newOwnerPublicId{};
	::memcpy(newOwnerPublicId.data(), req->newOwnerPublicId, newOwnerPublicId.size());
	const std::string newOwnerPublicIdHex = Crypto::CCryptoUtil::ToHex(newOwnerPublicId.data(), newOwnerPublicId.size());
	_tstring newOwnerPublicIdHexT = Utf8ToTString(newOwnerPublicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_TransferRoomOwner: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(false);
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T("UPDATE rooms SET owner_public_id = ? WHERE room_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(false);
		return EDBReturnType::INVALID;
	}

	SQLLEN ownerLenInd = SQL_NTS;
	guard->BindParamInput(1, newOwnerPublicIdHexT.c_str(), ownerLenInd);
	guard->BindParamInput(2, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(false);
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(true);
	return EDBReturnType::OK;
}