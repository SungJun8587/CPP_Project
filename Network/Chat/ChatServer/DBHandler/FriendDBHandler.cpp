
//***************************************************************************
// FriendDBHandler.cpp : kDbCallIdent_Friend 핸들러 (친구 요청/수락/거절/삭제/목록/차단)
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBFriendRequest.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
	//***************************************************************************
	// @brief 한 연결(OdbcConnGuard) 위에서 파라미터 0~2개짜리 단순 구문을 실행하는 도우미.
	// @details 모든 파라미터는 문자열(public_id 16진 32자 또는 닉네임)이다. 각 함수는
	//          성공 여부를 반환하고, 어떤 경로에서도 ClearStmt()를 호출해 구문을 정리한다.
	//***************************************************************************
	class CFriendSql
	{
	public:
		explicit CFriendSql(OdbcConnGuard& guard) : _guard(guard) {}

		// INSERT/DELETE 등 결과 집합이 없는 구문
		bool Exec(const TCHAR* sql, const std::string& p1, const std::string* p2 = nullptr)
		{
			return Run(sql, p1, p2, nullptr, nullptr);
		}

		// 첫 행이 존재하는지 확인 (SELECT 1 ...)
		bool Exists(const TCHAR* sql, const std::string& p1, const std::string* p2, bool& exists)
		{
			return Run(sql, p1, p2, &exists, nullptr);
		}

		// SELECT COUNT(*) ... 의 첫 컬럼을 정수로 읽음
		bool Count(const TCHAR* sql, const std::string& p1, const std::string* p2, int32& count)
		{
			count = 0;
			return Run(sql, p1, p2, nullptr, &count);
		}

		// 컬럼 (public_id, nickname, image_ref) 순서의 행을 전부 읽는다.
		// nickname/image_ref가 없는 쿼리(차단 목록은 image_ref 제외)는 columnCount로 구분.
		bool Fetch(const TCHAR* sql, const std::string& p1, const std::string* p2, int columnCount,
			std::vector<SFriendEntry>& out)
		{
			_tstring t1 = Utf8ToTString(p1);
			_tstring t2 = p2 ? Utf8ToTString(*p2) : _tstring();
			if( !DbUtil::PrepareOrClear(_guard, sql) )
			{
				return false;
			}

			DbUtil::BindString(_guard, 1, t1);
			if( p2 )
				DbUtil::BindString(_guard, 2, t2);

			if( !DbUtil::ExecuteOrClear(_guard) )
			{
				return false;
			}

			while( _guard->Fetch() )
			{
				SFriendEntry entry;
				if( !ReadEntry(columnCount, entry) )
					continue;
				out.push_back(std::move(entry));
			}
			_guard->ClearStmt();
			return true;
		}

		// WHERE 절의 단일 값으로 계정 한 명을 읽는다. 없으면 found=false.
		bool LoadUser(const TCHAR* sql, const std::string& key, SFriendEntry& entry, bool& found)
		{
			std::vector<SFriendEntry> rows;
			if( !Fetch(sql, key, nullptr, 3, rows) )
				return false;
			found = !rows.empty();
			if( found )
				entry = std::move(rows.front());
			return true;
		}

	private:
		bool Run(const TCHAR* sql, const std::string& p1, const std::string* p2, bool* exists, int32* count)
		{
			_tstring t1 = Utf8ToTString(p1);
			_tstring t2 = p2 ? Utf8ToTString(*p2) : _tstring();
			if( !DbUtil::PrepareOrClear(_guard, sql) )
			{
				return false;
			}

			DbUtil::BindString(_guard, 1, t1);
			if( p2 )
				DbUtil::BindString(_guard, 2, t2);

			if( !DbUtil::ExecuteOrClear(_guard) )
			{
				return false;
			}

			if( exists != nullptr )
				*exists = _guard->Fetch();

			if( count != nullptr && _guard->Fetch() )
			{
				TCHAR buf[32] = {};
				int32 bufLen = static_cast<int32>(sizeof(buf));
				if( _guard->GetData(1, buf, bufLen) )
					*count = _ttoi(buf);
			}

			_guard->ClearStmt();
			return true;
		}

		bool ReadEntry(int columnCount, SFriendEntry& entry)
		{
			TCHAR idBuf[40] = {};
			int32 idBufLen = static_cast<int32>(sizeof(idBuf));
			if( !_guard->GetData(1, idBuf, idBufLen) )
				return false;
			if( !HexToPublicId(TStringToUtf8(_tstring(idBuf)), entry.publicId) )
				return false;

			TCHAR nickBuf[kNicknameBytes] = {};
			int32 nickBufLen = static_cast<int32>(sizeof(nickBuf));
			if( !_guard->GetData(2, nickBuf, nickBufLen) )
				return false;
			entry.nickname = TStringToUtf8(_tstring(nickBuf));

			if( columnCount >= 3 )
			{
				// 프로필 이미지는 없을 수 있다(COALESCE로 빈 문자열) — 읽기 실패해도 행은 유지
				TCHAR refBuf[kProfileImageUrlBytes] = {};
				int32 refBufLen = static_cast<int32>(sizeof(refBuf));
				if( _guard->GetData(3, refBuf, refBufLen) )
					entry.profileImageRef = TStringToUtf8(_tstring(refBuf));
			}
			return true;
		}

	private:
		OdbcConnGuard& _guard;
	};

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
}

//***************************************************************************
// @brief 친구 기능 연산 하나를 처리합니다. 어떤 경로에서도 onComplete가 정확히 한 번 호출됩니다.
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

	CFriendSql sql(guard);
	const std::string me = PublicIdToHex(req->publicId);

	// ── 목록 조회 ──────────────────────────────────────────────────────────
	switch( req->op )
	{
	case EFriendOp::ListFriends:
		return finish(sql.Fetch(kSqlListFriends, me, &me, 3, out.entries) ? EFriendResult::Ok : EFriendResult::DbError);
	case EFriendOp::ListPending:
	{
		// 페이지 크기를 1~kMaxPendingPageSize로, 페이지 번호를 0~마지막 페이지로 보정한다.
		// LIMIT/OFFSET은 문자열 파라미터로 바인딩하면 서버 측 prepare에서 거부될 수 있어
		// 서버가 보정한 정수를 SQL 텍스트에 직접 넣는다(외부 문자열이 아니므로 주입 위험 없음).
		int32 total = 0;
		if( !sql.Count(kSqlCountPending, me, nullptr, total) )
			return finish(EFriendResult::DbError);

		const int32 pageSize = (std::max)(1, (std::min)(req->pageSize, kMaxPendingPageSize));
		const int32 lastPage = (total > 0) ? ((total - 1) / pageSize) : 0;
		const int32 page = (std::max)(0, (std::min)(req->page, lastPage));

		out.totalCount = total;
		out.pageSize = pageSize;
		out.page = page;

		if( total > 0 )
		{
			_tstring query = kSqlListPending;
			query += Utf8ToTString(" LIMIT " + std::to_string(pageSize) + " OFFSET " + std::to_string(page * pageSize));

			if( !sql.Fetch(query.c_str(), me, nullptr, 3, out.entries) )
				return finish(EFriendResult::DbError);
		}
		return finish(EFriendResult::Ok);
	}
	case EFriendOp::ListBlocks:
		return finish(sql.Fetch(kSqlListBlocks, me, nullptr, 2, out.entries) ? EFriendResult::Ok : EFriendResult::DbError);
	default:
		break;
	}

	// ── 단건 연산: 대상 계정 확인 ──────────────────────────────────────────
	bool found = false;
	bool ok;
	if( req->hasTargetId )
	{
		const std::string targetHex = PublicIdToHex(req->targetPublicId);
		ok = sql.LoadUser(kSqlUserById, targetHex, out.target, found);
	}
	else
	{
		char safeBuf[sizeof(req->targetNickname) + 1] = {};
		::memcpy(safeBuf, req->targetNickname, sizeof(req->targetNickname));
		ok = sql.LoadUser(kSqlUserByNickname, std::string(safeBuf), out.target, found);
	}
	if( !ok )
		return finish(EFriendResult::DbError);
	if( !found )
		return finish(EFriendResult::UserNotFound);

	const std::string target = PublicIdToHex(out.target.publicId);
	if( target == me )
		return finish(EFriendResult::SelfTarget);

	std::string lo, hi;
	MakePair(me, target, lo, hi);

	// 친구 관계 성립(요청 삭제 포함). accepter=me, requester=상대 또는 그 반대일 수 있어 방향은 호출부가 정한다.
	// 양쪽 친구 수 상한을 검사한다.
	auto establishFriendship = [&](const std::string& requester, const std::string& accepter) -> EFriendResult
		{
			int32 myCount = 0, otherCount = 0;
			if( !sql.Count(kSqlFriendCount, me, &me, myCount) ||
				!sql.Count(kSqlFriendCount, target, &target, otherCount) )
				return EFriendResult::DbError;
			if( myCount >= kMaxFriends || otherCount >= kMaxFriends )
				return EFriendResult::FriendLimit;

			if( !sql.Exec(kSqlInsertFriend, lo, &hi) )
				return EFriendResult::DbError;
			// 어느 방향의 대기 요청이든 정리 (둘 다 없을 수도 있음)
			if( !sql.Exec(kSqlDeleteRequest, requester, &accepter) ||
				!sql.Exec(kSqlDeleteRequest, accepter, &requester) )
				return EFriendResult::DbError;
			return EFriendResult::Ok;
		};

	bool exists = false;

	switch( req->op )
	{
	case EFriendOp::SendRequest:
	{
		// 내가 차단한 상대에게는 요청 불가 (차단 해제 후 가능)
		if( !sql.Exists(kSqlBlockExists, me, &target, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
			return finish(EFriendResult::BlockedByMe);

		// 상대가 나를 차단함 — 차단 사실을 드러내지 않도록 성공으로 응답하되 저장/알림은 하지 않는다.
		if( !sql.Exists(kSqlBlockExists, target, &me, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
		{
			out.silent = true;
			return finish(EFriendResult::Ok);
		}

		if( !sql.Exists(kSqlIsFriend, lo, &hi, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
			return finish(EFriendResult::AlreadyFriends);

		if( !sql.Exists(kSqlRequestExists, me, &target, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
			return finish(EFriendResult::AlreadyRequested);

		// 상대가 이미 나에게 요청을 보낸 상태면 서로 원하는 것이므로 즉시 친구로 만든다.
		if( !sql.Exists(kSqlRequestExists, target, &me, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
		{
			const EFriendResult r = establishFriendship(target, me);
			out.autoAccepted = (r == EFriendResult::Ok);
			return finish(r);
		}

		int32 myCount = 0;
		if( !sql.Count(kSqlFriendCount, me, &me, myCount) )
			return finish(EFriendResult::DbError);
		if( myCount >= kMaxFriends )
			return finish(EFriendResult::FriendLimit);

		return finish(sql.Exec(kSqlInsertRequest, me, &target) ? EFriendResult::Ok : EFriendResult::DbError);
	}

	case EFriendOp::Accept:
	{
		if( !sql.Exists(kSqlRequestExists, target, &me, exists) )
			return finish(EFriendResult::DbError);
		if( !exists )
			return finish(EFriendResult::NoPendingRequest);
		return finish(establishFriendship(target, me));
	}

	case EFriendOp::Reject:
	{
		if( !sql.Exists(kSqlRequestExists, target, &me, exists) )
			return finish(EFriendResult::DbError);
		if( !exists )
			return finish(EFriendResult::NoPendingRequest);
		return finish(sql.Exec(kSqlDeleteRequest, target, &me) ? EFriendResult::Ok : EFriendResult::DbError);
	}

	case EFriendOp::Remove:
	{
		if( !sql.Exists(kSqlIsFriend, lo, &hi, exists) )
			return finish(EFriendResult::DbError);
		if( !exists )
			return finish(EFriendResult::NotFriends);
		return finish(sql.Exec(kSqlDeleteFriend, lo, &hi) ? EFriendResult::Ok : EFriendResult::DbError);
	}

	case EFriendOp::Block:
	{
		if( !sql.Exists(kSqlBlockExists, me, &target, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
			return finish(EFriendResult::AlreadyBlocked);

		if( !sql.Exec(kSqlInsertBlock, me, &target) )
			return finish(EFriendResult::DbError);

		// 차단하면 친구 관계와 양방향 대기 요청을 모두 정리한다.
		if( !sql.Exists(kSqlIsFriend, lo, &hi, exists) )
			return finish(EFriendResult::DbError);
		if( exists )
		{
			if( !sql.Exec(kSqlDeleteFriend, lo, &hi) )
				return finish(EFriendResult::DbError);
			out.friendshipRemoved = true;
		}
		if( !sql.Exec(kSqlDeleteRequest, me, &target) || !sql.Exec(kSqlDeleteRequest, target, &me) )
			return finish(EFriendResult::DbError);
		return finish(EFriendResult::Ok);
	}

	case EFriendOp::Unblock:
	{
		if( !sql.Exists(kSqlBlockExists, me, &target, exists) )
			return finish(EFriendResult::DbError);
		if( !exists )
			return finish(EFriendResult::NotBlocked);
		return finish(sql.Exec(kSqlDeleteBlock, me, &target) ? EFriendResult::Ok : EFriendResult::DbError);
	}

	default:
		break;
	}

	return finish(EFriendResult::DbError);
}