
//***************************************************************************
// FriendDBHandler.cpp : kDbCallIdent_Friend 핸들러 (친구 요청/수락/거절/삭제/목록/차단)
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "../ChatCommon/Protocol/ChatPacketUtil.h"
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include "DBFriendRequest.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
	//***************************************************************************
	// @brief 컬럼 (public_id, nickname[, image_ref]) 순서의 행을 전부 SFriendEntry로 읽는다.
	// @details image_ref가 없는 쿼리(차단 목록)는 columnCount=2로 구분한다. publicId가 깨진 행은 건너뛴다.
	//***************************************************************************
	bool FetchEntries(DbUtil::CSql& sql, const TCHAR* query, const std::vector<std::string>& params,
		int columnCount, std::vector<SFriendEntry>& out)
	{
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(query, params, columnCount, rows) )
			return false;

		for( const DbUtil::Row& row : rows )
		{
			SFriendEntry entry;
			if( !HexToPublicId(row[0], entry.publicId) )
				continue;
			entry.nickname = row[1];
			if( columnCount >= 3 )
				entry.profileImageRef = row[2];	// 프로필 이미지는 없을 수 있다(COALESCE로 빈 문자열)
			out.push_back(std::move(entry));
		}
		return true;
	}

	// WHERE 절의 단일 값으로 계정 한 명을 읽는다. 없으면 found=false.
	bool LoadUser(DbUtil::CSql& sql, const TCHAR* query, const std::string& key, SFriendEntry& entry, bool& found)
	{
		std::vector<SFriendEntry> rows;
		if( !FetchEntries(sql, query, { key }, 3, rows) )
			return false;
		found = !rows.empty();
		if( found )
			entry = std::move(rows.front());
		return true;
	}

	// 정규화된 쌍 (작은 쪽, 큰 쪽) — friendships의 저장 규칙. 둘 다 소문자 16진 32자라 단순 문자열 비교로 충분.
	void MakePair(const std::string& x, const std::string& y, std::string& lo, std::string& hi)
	{
		if( x < y ) { lo = x; hi = y; }
		else { lo = y; hi = x; }
	}

	// ── SQL ───────────────────────────────────────────────────────────────
	const TCHAR* const kSqlUserByNickname =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, '') FROM users u ")
		_T("LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("WHERE u.nickname = ?");
	const TCHAR* const kSqlUserById =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, '') FROM users u ")
		_T("LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("WHERE u.public_id = ?");

	const TCHAR* const kSqlIsFriend = _T("SELECT 1 FROM friendships WHERE user_a = ? AND user_b = ?");
	const TCHAR* const kSqlRequestExists = _T("SELECT 1 FROM friend_requests WHERE from_id = ? AND to_id = ?");
	const TCHAR* const kSqlBlockExists = _T("SELECT 1 FROM user_blocks WHERE blocker_id = ? AND blocked_id = ?");
	const TCHAR* const kSqlFriendCount = _T("SELECT COUNT(*) FROM friendships WHERE user_a = ? OR user_b = ?");

	const TCHAR* const kSqlInsertRequest = _T("INSERT IGNORE INTO friend_requests (from_id, to_id) VALUES (?, ?)");
	const TCHAR* const kSqlDeleteRequest = _T("DELETE FROM friend_requests WHERE from_id = ? AND to_id = ?");
	const TCHAR* const kSqlInsertFriend = _T("INSERT IGNORE INTO friendships (user_a, user_b) VALUES (?, ?)");
	const TCHAR* const kSqlDeleteFriend = _T("DELETE FROM friendships WHERE user_a = ? AND user_b = ?");
	const TCHAR* const kSqlInsertBlock = _T("INSERT IGNORE INTO user_blocks (blocker_id, blocked_id) VALUES (?, ?)");
	const TCHAR* const kSqlDeleteBlock = _T("DELETE FROM user_blocks WHERE blocker_id = ? AND blocked_id = ?");

	const TCHAR* const kSqlCountPending = _T("SELECT COUNT(*) FROM friend_requests WHERE to_id = ?");

	const TCHAR* const kSqlListFriends =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, '') FROM ")
		_T("(SELECT user_b AS fid FROM friendships WHERE user_a = ? ")
		_T(" UNION ALL SELECT user_a FROM friendships WHERE user_b = ?) f ")
		_T("JOIN users u ON u.public_id = f.fid ")
		_T("LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("ORDER BY u.nickname");
	const TCHAR* const kSqlListPending =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, '') FROM friend_requests r ")
		_T("JOIN users u ON u.public_id = r.from_id ")
		_T("LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("WHERE r.to_id = ? ORDER BY r.created_at, r.from_id");	// LIMIT/OFFSET은 호출부에서 정수를 직접 덧붙인다
	const TCHAR* const kSqlListBlocks =
		_T("SELECT u.public_id, u.nickname FROM user_blocks b ")
		_T("JOIN users u ON u.public_id = b.blocked_id ")
		_T("WHERE b.blocker_id = ? ORDER BY b.created_at");

	//***************************************************************************
	// @brief 연산 하나를 처리하는 동안 연산 함수들이 공유하는 상태.
	// @details target/lo/hi는 단건 연산에서 ResolveTarget()이 채운다(목록 연산에서는 쓰지 않는다).
	//***************************************************************************
	struct SFriendCtx
	{
		ST_FRIEND_REQ* req;
		SFriendOpResult& out;
		DbUtil::CSql& sql;
		std::string			me;			// 요청자 public_id(16진 32자)
		std::string			target;		// 대상 public_id(16진 32자)
		std::string			lo, hi;		// (me, target)을 friendships 저장 규칙대로 정규화한 쌍
	};

	// ── 목록 연산 ─────────────────────────────────────────────────────────

	EFriendResult HandleListFriends(SFriendCtx& c)
	{
		return FetchEntries(c.sql, kSqlListFriends, { c.me, c.me }, 3, c.out.entries) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	EFriendResult HandleListPending(SFriendCtx& c)
	{
		// 페이지 크기를 1~kMaxPendingPageSize로, 페이지 번호를 0~마지막 페이지로 보정한다.
		// LIMIT/OFFSET은 문자열 파라미터로 바인딩하면 서버 측 prepare에서 거부될 수 있어
		// 서버가 보정한 정수를 SQL 텍스트에 직접 넣는다(외부 문자열이 아니므로 주입 위험 없음).
		int32 total = 0;
		if( !c.sql.Count(kSqlCountPending, { c.me }, total) )
			return EFriendResult::DbError;

		const int32 pageSize = (std::max)(1, (std::min)(c.req->pageSize, kMaxPendingPageSize));
		const int32 lastPage = (total > 0) ? ((total - 1) / pageSize) : 0;
		const int32 page = (std::max)(0, (std::min)(c.req->page, lastPage));

		c.out.totalCount = total;
		c.out.pageSize = pageSize;
		c.out.page = page;

		if( total > 0 )
		{
			_tstring query = kSqlListPending;
			query += Utf8ToTString(" LIMIT " + std::to_string(pageSize) + " OFFSET " + std::to_string(page * pageSize));

			if( !FetchEntries(c.sql, query.c_str(), { c.me }, 3, c.out.entries) )
				return EFriendResult::DbError;
		}
		return EFriendResult::Ok;
	}

	EFriendResult HandleListBlocks(SFriendCtx& c)
	{
		return FetchEntries(c.sql, kSqlListBlocks, { c.me }, 2, c.out.entries) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	// ── 단건 연산 ─────────────────────────────────────────────────────────

	// 대상 계정을 읽어 c.out.target/c.target/c.lo/c.hi를 채운다. 없으면 UserNotFound, 자기 자신이면 SelfTarget.
	EFriendResult ResolveTarget(SFriendCtx& c)
	{
		bool found = false;
		bool ok;
		if( c.req->hasTargetId )
		{
			const std::string targetHex = PublicIdToHex(c.req->targetPublicId);
			ok = LoadUser(c.sql, kSqlUserById, targetHex, c.out.target, found);
		}
		else
		{
			char safeBuf[sizeof(c.req->targetNickname) + 1] = {};
			::memcpy(safeBuf, c.req->targetNickname, sizeof(c.req->targetNickname));
			ok = LoadUser(c.sql, kSqlUserByNickname, std::string(safeBuf), c.out.target, found);
		}
		if( !ok )
			return EFriendResult::DbError;
		if( !found )
			return EFriendResult::UserNotFound;

		c.target = PublicIdToHex(c.out.target.publicId);
		if( c.target == c.me )
			return EFriendResult::SelfTarget;

		MakePair(c.me, c.target, c.lo, c.hi);
		return EFriendResult::Ok;
	}

	// 친구 관계 성립(요청 삭제 포함). requester/accepter는 정리할 대기 요청의 방향이다.
	// 양쪽 친구 수 상한을 검사한다.
	EFriendResult EstablishFriendship(SFriendCtx& c, const std::string& requester, const std::string& accepter)
	{
		int32 myCount = 0, otherCount = 0;
		if( !c.sql.Count(kSqlFriendCount, { c.me, c.me }, myCount) ||
			!c.sql.Count(kSqlFriendCount, { c.target, c.target }, otherCount) )
			return EFriendResult::DbError;
		if( myCount >= kMaxFriends || otherCount >= kMaxFriends )
			return EFriendResult::FriendLimit;

		if( !c.sql.Exec(kSqlInsertFriend, { c.lo, c.hi }) )
			return EFriendResult::DbError;
		// 어느 방향의 대기 요청이든 정리 (둘 다 없을 수도 있음)
		if( !c.sql.Exec(kSqlDeleteRequest, { requester, accepter }) ||
			!c.sql.Exec(kSqlDeleteRequest, { accepter, requester }) )
			return EFriendResult::DbError;
		return EFriendResult::Ok;
	}

	EFriendResult HandleSendRequest(SFriendCtx& c)
	{
		bool exists = false;

		// 내가 차단한 상대에게는 요청 불가 (차단 해제 후 가능)
		if( !c.sql.Exists(kSqlBlockExists, { c.me, c.target }, exists) )
			return EFriendResult::DbError;
		if( exists )
			return EFriendResult::BlockedByMe;

		// 상대가 나를 차단함 — 차단 사실을 드러내지 않도록 성공으로 응답하되 저장/알림은 하지 않는다.
		if( !c.sql.Exists(kSqlBlockExists, { c.target, c.me }, exists) )
			return EFriendResult::DbError;
		if( exists )
		{
			c.out.silent = true;
			return EFriendResult::Ok;
		}

		if( !c.sql.Exists(kSqlIsFriend, { c.lo, c.hi }, exists) )
			return EFriendResult::DbError;
		if( exists )
			return EFriendResult::AlreadyFriends;

		if( !c.sql.Exists(kSqlRequestExists, { c.me, c.target }, exists) )
			return EFriendResult::DbError;
		if( exists )
			return EFriendResult::AlreadyRequested;

		// 상대가 이미 나에게 요청을 보낸 상태면 서로 원하는 것이므로 즉시 친구로 만든다.
		if( !c.sql.Exists(kSqlRequestExists, { c.target, c.me }, exists) )
			return EFriendResult::DbError;
		if( exists )
		{
			const EFriendResult r = EstablishFriendship(c, c.target, c.me);
			c.out.autoAccepted = (r == EFriendResult::Ok);
			return r;
		}

		int32 myCount = 0;
		if( !c.sql.Count(kSqlFriendCount, { c.me, c.me }, myCount) )
			return EFriendResult::DbError;
		if( myCount >= kMaxFriends )
			return EFriendResult::FriendLimit;

		return c.sql.Exec(kSqlInsertRequest, { c.me, c.target }) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	EFriendResult HandleAccept(SFriendCtx& c)
	{
		bool exists = false;
		if( !c.sql.Exists(kSqlRequestExists, { c.target, c.me }, exists) )
			return EFriendResult::DbError;
		if( !exists )
			return EFriendResult::NoPendingRequest;
		return EstablishFriendship(c, c.target, c.me);
	}

	EFriendResult HandleReject(SFriendCtx& c)
	{
		bool exists = false;
		if( !c.sql.Exists(kSqlRequestExists, { c.target, c.me }, exists) )
			return EFriendResult::DbError;
		if( !exists )
			return EFriendResult::NoPendingRequest;
		return c.sql.Exec(kSqlDeleteRequest, { c.target, c.me }) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	EFriendResult HandleRemove(SFriendCtx& c)
	{
		bool exists = false;
		if( !c.sql.Exists(kSqlIsFriend, { c.lo, c.hi }, exists) )
			return EFriendResult::DbError;
		if( !exists )
			return EFriendResult::NotFriends;
		return c.sql.Exec(kSqlDeleteFriend, { c.lo, c.hi }) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	EFriendResult HandleBlock(SFriendCtx& c)
	{
		bool exists = false;
		if( !c.sql.Exists(kSqlBlockExists, { c.me, c.target }, exists) )
			return EFriendResult::DbError;
		if( exists )
			return EFriendResult::AlreadyBlocked;

		if( !c.sql.Exec(kSqlInsertBlock, { c.me, c.target }) )
			return EFriendResult::DbError;

		// 차단하면 친구 관계와 양방향 대기 요청을 모두 정리한다.
		if( !c.sql.Exists(kSqlIsFriend, { c.lo, c.hi }, exists) )
			return EFriendResult::DbError;
		if( exists )
		{
			if( !c.sql.Exec(kSqlDeleteFriend, { c.lo, c.hi }) )
				return EFriendResult::DbError;
			c.out.friendshipRemoved = true;
		}
		if( !c.sql.Exec(kSqlDeleteRequest, { c.me, c.target }) || !c.sql.Exec(kSqlDeleteRequest, { c.target, c.me }) )
			return EFriendResult::DbError;
		return EFriendResult::Ok;
	}

	EFriendResult HandleUnblock(SFriendCtx& c)
	{
		bool exists = false;
		if( !c.sql.Exists(kSqlBlockExists, { c.me, c.target }, exists) )
			return EFriendResult::DbError;
		if( !exists )
			return EFriendResult::NotBlocked;
		return c.sql.Exec(kSqlDeleteBlock, { c.me, c.target }) ? EFriendResult::Ok : EFriendResult::DbError;
	}

	// 대상 계정을 먼저 확인한 뒤 단건 연산 함수를 실행한다.
	EFriendResult RunWithTarget(SFriendCtx& c, EFriendResult(*handler)(SFriendCtx&))
	{
		const EFriendResult r = ResolveTarget(c);
		return (r == EFriendResult::Ok) ? handler(c) : r;
	}
}

//***************************************************************************
// @brief 친구 기능 연산 하나를 op에 따라 연산별 함수로 나눠 처리합니다. 어떤 경로에서도 onComplete가 정확히 한 번 호출됩니다.
// @details 여러 구문을 하나의 트랜잭션으로 묶지 않는다(프로필 이미지 대표 변경과 같은 실용적
//          타협). 모든 쓰기를 INSERT IGNORE / 조건부 DELETE로 멱등하게 구성했고, 순서를
//          "관계 생성 -> 요청 삭제"로 둬서 중간에 실패해도 재시도하면 수렴한다
//          (최악의 경우 이미 친구인데 대기 요청이 남는 정도이며 다음 수락/요청 시 정리됨).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Friend)
{
	ST_FRIEND_REQ* req = static_cast<ST_FRIEND_REQ*>(pStAsync);

	SFriendOpResult out;
	auto finish = [&](EFriendResult result) -> EDBReturnType
		{
			out.result = result;
			if( req->onComplete )
				req->onComplete(out);
			return (result == EFriendResult::DbError) ? EDBReturnType::INVALID : EDBReturnType::OK;
		};

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_Friend: No available ODBC connection in pool."));
		return finish(EFriendResult::DbError);
	}

	DbUtil::CSql sql(guard);
	SFriendCtx ctx{ req, out, sql, PublicIdToHex(req->publicId), std::string(), std::string(), std::string() };

	switch( req->op )
	{
	case EFriendOp::ListFriends:	return finish(HandleListFriends(ctx));
	case EFriendOp::ListPending:	return finish(HandleListPending(ctx));
	case EFriendOp::ListBlocks:		return finish(HandleListBlocks(ctx));
	case EFriendOp::SendRequest:	return finish(RunWithTarget(ctx, HandleSendRequest));
	case EFriendOp::Accept:			return finish(RunWithTarget(ctx, HandleAccept));
	case EFriendOp::Reject:			return finish(RunWithTarget(ctx, HandleReject));
	case EFriendOp::Remove:			return finish(RunWithTarget(ctx, HandleRemove));
	case EFriendOp::Block:			return finish(RunWithTarget(ctx, HandleBlock));
	case EFriendOp::Unblock:		return finish(RunWithTarget(ctx, HandleUnblock));
	}

	LOG_ERROR(_T("kDbCallIdent_Friend: unknown op %d"), static_cast<int>(req->op));
	return finish(EFriendResult::DbError);
}