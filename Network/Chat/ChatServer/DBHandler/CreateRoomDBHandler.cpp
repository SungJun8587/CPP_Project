
//***************************************************************************
// CreateRoomDBHandler.cpp : DBASYNC_CREATE_ROOM_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBCreateRoomRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방을 새로 만든다.
// @details [설계 — 개수 제한 검사와 INSERT 사이의 경쟁 상태] COUNT(*) 확인과
//          INSERT를 하나의 트랜잭션으로 묶지 않았다 — 같은 계정이 동시에
//          여러 CreateRoom 요청을 보내는 극단적인 경우에만 상한을 아주
//          살짝 넘길 수 있는데(예: 상한 3인데 동시에 두 요청이 둘 다
//          "지금 2개니까 통과" 판단), 이 서버 규모에서 그 정도 위험은
//          트랜잭션 도입의 복잡도를 감수할 만큼 크지 않다고 판단했다.
//          [설계 — 새 room_id 조회] COdbcConnGuard에 "방금 넣은 INSERT의
//          auto-increment 값"을 바로 주는 전용 API가 있는지 확신이 없어서,
//          이미 검증된 SELECT+Fetch+GetData 패턴으로 LAST_INSERT_ID()를
//          다시 조회하는 방식을 썼다(MySQL 세션 범위 함수라 같은 커넥션
//          안에서 방금 이 커넥션이 실행한 INSERT의 값을 정확히 돌려준다).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_CreateRoom)
{
	ST_CREATE_ROOM_REQ* req = static_cast<ST_CREATE_ROOM_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> ownerPublicId{};
	::memcpy(ownerPublicId.data(), req->ownerPublicId, ownerPublicId.size());
	const std::string ownerPublicIdHex = Crypto::CCryptoUtil::ToHex(ownerPublicId.data(), ownerPublicId.size());
	_tstring ownerPublicIdHexT = Utf8ToTString(ownerPublicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_CreateRoom: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}

	// 1. 개수 제한 확인(설정에서 제한을 뒀을 때만 — 0 이하면 건너뜀).
	if( req->maxRoomsPerOwner > 0 )
	{
		if( !guard->PrepareQuery(_T("SELECT COUNT(*) FROM rooms WHERE owner_public_id = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, 0);
			return EDBReturnType::INVALID;
		}

		SQLLEN ownerLenInd = SQL_NTS;
		guard->BindParamInput(1, ownerPublicIdHexT.c_str(), ownerLenInd);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError, 0);
			return EDBReturnType::INVALID;
		}

		int32 currentCount = 0;
		if( guard->Fetch() )
		{
			TCHAR countBuf[16] = {};
			int32 countBufLen = static_cast<int32>(sizeof(countBuf));
			if( guard->GetData(1, countBuf, countBufLen) )
				currentCount = _ttoi(countBuf);
		}
		guard->ClearStmt();

		if( currentCount >= req->maxRoomsPerOwner )
		{
			if( req->onComplete )
				req->onComplete(ERoomResult::RoomLimitExceeded, 0);
			return EDBReturnType::OK; // DB 자체는 정상 처리됨(제한 초과는 "결과"이지 "오류"가 아님)
		}
	}

	// 2. 실제 생성.
	_tstring roomNameT = Utf8ToTString(req->roomName);

	if( !guard->PrepareQuery(_T("INSERT INTO rooms (name, owner_public_id) VALUES (?, ?)")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}

	SQLLEN nameLenInd = SQL_NTS;
	guard->BindParamInput(1, roomNameT.c_str(), nameLenInd);
	SQLLEN ownerLenInd2 = SQL_NTS;
	guard->BindParamInput(2, ownerPublicIdHexT.c_str(), ownerLenInd2);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	// 3. 방금 생성된 room_id 조회.
	if( !guard->PrepareQuery(_T("SELECT LAST_INSERT_ID()")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}

	int32 newRoomId = 0;
	if( guard->Fetch() )
	{
		TCHAR idBuf[32] = {};
		int32 idBufLen = static_cast<int32>(sizeof(idBuf));
		if( guard->GetData(1, idBuf, idBufLen) )
			newRoomId = _ttoi(idBuf);
	}
	guard->ClearStmt();

	if( newRoomId <= 0 )
	{
		// INSERT는 성공했는데 ID 조회만 실패한 드문 상황 — 방 자체는 DB에
		// 남아있지만 호출부(서버)는 방 번호를 모르니 정상적으로 쓸 수
		// 없다. 데모 범위에서는 그대로 오류로 보고한다(고아 행 정리는
		// 별도 배치가 필요하다면 추가할 것).
		LOG_ERROR(_T("kDbCallIdent_CreateRoom: LAST_INSERT_ID() 조회 실패 — 방은 생성됐으나 ID를 못 받음"));
		if( req->onComplete )
			req->onComplete(ERoomResult::DbError, 0);
		return EDBReturnType::INVALID;
	}

	if( req->onComplete )
		req->onComplete(ERoomResult::Ok, newRoomId);
	return EDBReturnType::OK;
}