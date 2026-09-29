
//***************************************************************************
// ListRoomMembersPageDBHandler.cpp : kDbCallIdent_ListRoomMembersPage 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBListRoomMembersPageRequest.h"

#include <cstring>

//***************************************************************************
// @brief 방에 입장한 적이 있는 유저 목록의 한 페이지를 조회한다.
// @details ListRoomsPageDBHandler.cpp와 동일한 2단계(전체 개수 -> 페이지
//          보정 -> 해당 페이지 조회) 패턴을 쓴다. 대표 프로필 이미지는
//          user_profile_images(status=1인 행)에서 상관 서브쿼리로 가져온다
//          — AccountDBHandler.cpp의 로그인 조회와 동일한 방식이며, 그
//          유저가 한 번도 대표 이미지를 설정한 적이 없으면 NULL이라 빈
//          문자열로 처리된다(에러 아님).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ListRoomMembersPage)
{
	ST_LIST_ROOM_MEMBERS_PAGE_REQ* req = static_cast<ST_LIST_ROOM_MEMBERS_PAGE_REQ*>(pStAsync);

	auto fail = [req]()
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, 0, 0, std::vector<SRoomMemberHistoryEntry>());
		};

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ListRoomMembersPage: No available ODBC connection in pool."));
		fail();
		return EDBReturnType::INVALID;
	}

	int32 pageSize = req->pageSize;
	if( pageSize < 1 )
		pageSize = 1;
	if( pageSize > kMaxRoomPageSize )
		pageSize = kMaxRoomPageSize;

	// 1. 이 방에 입장 이력이 있는 전체 유저 수.
	if( !guard->PrepareQuery(_T("SELECT COUNT(*) FROM room_members WHERE room_id = ?")) )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	guard->BindParamInput(1, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	int32 totalCount = 0;
	if( guard->Fetch() )
	{
		TCHAR countBuf[24] = {};
		int32 countBufLen = static_cast<int32>(sizeof(countBuf));
		if( guard->GetData(1, countBuf, countBufLen) )
			totalCount = _ttoi(countBuf);
	}
	guard->ClearStmt();

	if( totalCount <= 0 )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, 0, 0, std::vector<SRoomMemberHistoryEntry>());
		return EDBReturnType::OK;
	}

	// 2. 요청 페이지를 유효 범위로 보정.
	const int32 lastPage = (totalCount - 1) / pageSize;
	int32 page = req->page;
	if( page < 0 )
		page = 0;
	if( page > lastPage )
		page = lastPage;

	const int64 offset = static_cast<int64>(page) * pageSize;

	// 3. 해당 페이지 조회 — 최근 입장순. LIMIT/OFFSET은 파라미터 바인딩
	// 대신 쿼리 문자열에 직접 넣는다 — 둘 다 서버가 이미 검증/보정한
	// 정수라 SQL 인젝션 위험이 없고(ListRoomsPageDBHandler.cpp와 동일한
	// 이유), BindParamInput()이 인자를 참조로 받는 시그니처라 캐스트
	// 표현식 같은 임시값을 바로 바인딩할 수 없었다(pageSize/offset은
	// PrepareQuery() 이후 값이 바뀌지 않으므로 값 자체를 문자열로 미리
	// 굳혀도 안전하다).
	const _tstring pageQuery = _tstring(_T(
		"SELECT u.public_id, u.nickname, "
		"(SELECT p.image_ref FROM user_profile_images p WHERE p.user_public_id = u.public_id AND p.status = 1 LIMIT 1) AS image_ref "
		"FROM room_members m "
		"JOIN users u ON u.public_id = m.user_public_id "
		"WHERE m.room_id = ? "
		"ORDER BY m.last_entered_at DESC, u.public_id "
		"LIMIT ")) + Utf8ToTString(std::to_string(pageSize))
		+ _T(" OFFSET ") + Utf8ToTString(std::to_string(offset));

	if( !guard->PrepareQuery(pageQuery.c_str()) )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	guard->BindParamInput(1, req->roomId);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	std::vector<SRoomMemberHistoryEntry> members;
	members.reserve(static_cast<size_t>(pageSize));
	while( guard->Fetch() )
	{
		TCHAR publicIdBuf[64] = {};
		int32 publicIdBufLen = static_cast<int32>(sizeof(publicIdBuf));
		if( !guard->GetData(1, publicIdBuf, publicIdBufLen) )
			continue;

		TCHAR nicknameBuf[32] = {}; // VARCHAR(16) + 여유
		int32 nicknameBufLen = static_cast<int32>(sizeof(nicknameBuf));
		if( !guard->GetData(2, nicknameBuf, nicknameBufLen) )
			continue;

		// image_ref는 NULL일 수 있다(대표 이미지 미설정) — GetData() 실패를
		// 행 자체를 건너뛸 이유로 취급하지 않는다.
		TCHAR imageRefBuf[512] = {};
		int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
		const bool hasImageRef = guard->GetData(3, imageRefBuf, imageRefBufLen);

		const std::string publicIdHex = TStringToUtf8(_tstring(publicIdBuf));

		SRoomMemberHistoryEntry entry;
		if( !Crypto::CCryptoUtil::FromHex(publicIdHex, entry.publicId.data(), entry.publicId.size()) )
			continue; // 형식이 깨진 값 — 방어적으로 건너뜀

		entry.nickname = TStringToUtf8(_tstring(nicknameBuf));
		entry.profileImageUrl = hasImageRef ? TStringToUtf8(_tstring(imageRefBuf)) : std::string();
		members.push_back(std::move(entry));
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, totalCount, page, members);
	return EDBReturnType::OK;
}