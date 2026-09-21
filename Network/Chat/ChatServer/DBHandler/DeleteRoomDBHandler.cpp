
//***************************************************************************
// DeleteRoomDBHandler.cpp : DBASYNC_DELETE_ROOM_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBDeleteRoomRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방을 삭제한다.
// @details [설계 — 소유권 확인 방식] "DELETE ... WHERE room_id=? AND
//          owner_public_id=?"를 한 번에 날리고 영향받은 행 수로 성공
//          여부를 판단하는 게 더 간단하지만, COdbcConnGuard가 "영향받은
//          행 수"를 돌려주는 API를 제공하는지 확신이 없어서(이미 검증된
//          Fetch/GetData 패턴이 아님), 먼저 SELECT로 존재 여부와 소유권을
//          확인한 뒤 DELETE하는 2단계로 나눴다. 두 쿼리 사이에 그 방이
//          다른 경로로 삭제되는 극히 좁은 경쟁 상태가 이론상 있지만,
//          그래도 DELETE 자체는 안전하게 실패(0행 삭제)할 뿐이라 문제되지 않는다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_DeleteRoom)
{
	ST_DELETE_ROOM_REQ* req = static_cast<ST_DELETE_ROOM_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> requesterPublicId{};
	::memcpy(requesterPublicId.data(), req->requesterPublicId, requesterPublicId.size());
	const std::string requesterPublicIdHex = Crypto::CCryptoUtil::ToHex(requesterPublicId.data(), requesterPublicId.size());
	_tstring requesterPublicIdHexT = Utf8ToTString(requesterPublicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_DeleteRoom: No available ODBC connection in pool."));
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

	// 2. 소유권 확인 통과 — 실제 삭제.
	if( !guard->PrepareQuery(_T("DELETE FROM rooms WHERE room_id = ?")) )
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
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ERoomResult::Ok);
	return EDBReturnType::OK;
}