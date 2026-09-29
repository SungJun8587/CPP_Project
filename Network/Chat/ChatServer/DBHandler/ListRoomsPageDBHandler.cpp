
//***************************************************************************
// ListRoomsPageDBHandler.cpp : kDbCallIdent_ListRoomsPage 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBListRoomsPageRequest.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief LIKE 검색어에서 와일드카드로 해석될 문자('%', '_')와 이스케이프
	//        문자('!') 자체를 이스케이프한다.
	// @details 검색어에 '%'나 '_'가 들어있어도 "그 글자가 이름에 들어간 방"을
	//          찾도록 하기 위함이다. 이스케이프 문자로 백슬래시 대신 '!'를 쓴
	//          이유는, 백슬래시는 DB 설정(NO_BACKSLASH_ESCAPES 등)에 따라
	//          문자열 리터럴 안에서의 해석이 달라질 수 있기 때문이다. 이 세
	//          문자는 모두 ASCII라서, 멀티바이트 UTF-8 문자의 바이트와 절대
	//          겹치지 않으므로 바이트 단위로 처리해도 안전하다.
	//***************************************************************************
	std::string BuildLikePattern(const std::string& keywordUtf8)
	{
		std::string pattern;
		pattern.reserve(keywordUtf8.size() + 8);
		pattern += '%';
		for( const char c : keywordUtf8 )
		{
			if( c == '!' || c == '%' || c == '_' )
				pattern += '!';
			pattern += c;
		}
		pattern += '%';
		return pattern;
	}
}

//***************************************************************************
// @brief 방 목록의 한 페이지를 조회한다.
// @details 1) 조건(scope/keyword)에 맞는 전체 개수를 COUNT(*)로 먼저 구한다.
//          2) 요청 페이지가 마지막 페이지를 넘으면(그 사이 방이 삭제된 경우
//             등) 마지막 페이지로 보정한다.
//          3) 보정된 페이지의 항목을 LIMIT/OFFSET으로 조회한다.
//          정렬은 페이지 경계에서 항목이 겹치거나 빠지지 않도록 항상
//          room_id를 두 번째 키로 둬서 순서를 확정한다. LIMIT/OFFSET 값은
//          호출부가 검증한 정수라 파라미터가 아니라 문자열로 직접
//          넣는다(SQL 인젝션 여지 없음). 검색어와 사용자 ID는 파라미터로
//          바인딩한다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ListRoomsPage)
{
	ST_LIST_ROOMS_PAGE_REQ* req = static_cast<ST_LIST_ROOMS_PAGE_REQ*>(pStAsync);

	auto fail = [req]()
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, 0, 0, std::vector<SRoomListEntry>());
		};

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ListRoomsPage: No available ODBC connection in pool."));
		fail();
		return EDBReturnType::INVALID;
	}

	const bool joined = (req->scope == ERoomListScope::Joined);
	const bool hasKeyword = !req->keyword.empty();

	int32 pageSize = req->pageSize;
	if( pageSize < 1 )
		pageSize = 1;
	if( pageSize > kMaxRoomPageSize )
		pageSize = kMaxRoomPageSize;

	// 조건 절 — COUNT 쿼리와 페이지 쿼리가 완전히 같은 FROM/WHERE를 쓴다.
	_tstring fromWhere;
	if( joined )
	{
		fromWhere = _T("FROM room_members m ")
			_T("JOIN rooms r ON r.room_id = m.room_id ")
			_T("JOIN users u ON r.owner_public_id = u.public_id ")
			_T("WHERE m.user_public_id = ? ");
	}
	else
	{
		fromWhere = _T("FROM rooms r ")
			_T("JOIN users u ON r.owner_public_id = u.public_id ");
	}

	if( hasKeyword )
	{
		fromWhere += joined ? _T("AND ") : _T("WHERE ");
		fromWhere += _T("r.name LIKE ? ESCAPE '!' ");
	}

	// 바인딩할 값들 — Execute() 전까지 살아있어야 하므로 여기서 선언한다.
	_tstring ownerHexT;
	if( joined )
	{
		std::array<BYTE, kPublicIdBytes> requesterPublicId{};
		::memcpy(requesterPublicId.data(), req->requesterPublicId, requesterPublicId.size());
		ownerHexT = Utf8ToTString(Crypto::CCryptoUtil::ToHex(requesterPublicId.data(), requesterPublicId.size()));
	}
	_tstring likePatternT;
	if( hasKeyword )
		likePatternT = Utf8ToTString(BuildLikePattern(req->keyword));

	SQLLEN ownerLenInd = SQL_NTS;
	SQLLEN keywordLenInd = SQL_NTS;

	auto bindConditions = [&]()
		{
			int32 index = 1;
			if( joined )
				guard->BindParamInput(index++, ownerHexT.c_str(), ownerLenInd);
			if( hasKeyword )
				guard->BindParamInput(index++, likePatternT.c_str(), keywordLenInd);
		};

	// 1. 조건에 맞는 전체 개수.
	const _tstring countQuery = _tstring(_T("SELECT COUNT(*) ")) + fromWhere;
	if( !guard->PrepareQuery(countQuery.c_str()) )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	bindConditions();

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
			req->onComplete(ELoginResult::Ok, 0, 0, std::vector<SRoomListEntry>());
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

	// 3. 해당 페이지 조회.
	const _tstring orderBy = joined
		? _tstring(_T("ORDER BY m.last_entered_at DESC, r.room_id DESC "))
		: _tstring(_T("ORDER BY r.created_at DESC, r.room_id DESC "));

	// [수정 — Redis로 이관] totalMemberCount(입장 이력이 있는 전체 유저 수)는
	// 더 이상 여기서 COUNT 서브쿼리로 채우지 않는다 — CChatServerMain::
	// RequestListRooms()가 이 함수의 결과를 받은 뒤, 방마다 Redis Set
	// (RoomMembers:{roomId})의 SCARD로 채운다. 페이지마다 매번 room_members에
	// COUNT 서브쿼리를 날리는 대신, 입장 시점에 이미 갱신해둔 Redis 카운터를
	// 읽기만 하는 구조로 바꾼 것 — SRoomListEntry::totalMemberCount는 여기서
	// 기본값(0)인 채로 나가고, 위 함수가 그 자리를 채운다.
	const _tstring pageQuery = _tstring(_T(
		"SELECT r.room_id, r.name, r.owner_public_id, u.nickname, r.image_ref "))
		+ fromWhere
		+ orderBy
		+ _T("LIMIT ") + Utf8ToTString(std::to_string(pageSize))
		+ _T(" OFFSET ") + Utf8ToTString(std::to_string(offset));

	if( !guard->PrepareQuery(pageQuery.c_str()) )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	bindConditions();

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		fail();
		return EDBReturnType::INVALID;
	}

	std::vector<SRoomListEntry> rooms;
	rooms.reserve(static_cast<size_t>(pageSize));
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

		// image_ref는 NULL일 수 있다(기본 이미지) — GetData() 실패를
		// "행 자체를 건너뛸 이유"로 취급하지 않고, 빈 문자열로만 둔다.
		TCHAR imageRefBuf[512] = {};
		int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
		const bool hasImageRef = guard->GetData(5, imageRefBuf, imageRefBufLen);

		// totalMemberCount는 여기서 채우지 않는다 — 기본값(0)인 채로 두면
		// 호출부(CChatServerMain::RequestListRooms())가 Redis SCARD 결과로
		// 채운다(위 SELECT 절 수정 설명 참고).
		SRoomListEntry entry;
		entry.roomId = _ttoi(roomIdBuf);
		entry.name = TStringToUtf8(_tstring(nameBuf));
		entry.ownerPublicId = TStringToUtf8(_tstring(ownerIdBuf));
		entry.ownerNickname = TStringToUtf8(_tstring(nicknameBuf));
		entry.imageRef = hasImageRef ? TStringToUtf8(_tstring(imageRefBuf)) : std::string();
		rooms.push_back(std::move(entry));
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, totalCount, page, rooms);
	return EDBReturnType::OK;
}