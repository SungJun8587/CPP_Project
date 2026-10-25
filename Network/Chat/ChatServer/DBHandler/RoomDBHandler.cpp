
//***************************************************************************
// RoomDBHandler.cpp : kDbCallIdent_Room 핸들러 — 방 기능 DB 연산 전체
//
// 방 생성/삭제/이름 변경/이미지/방장 이양/입장 기록/정보·목록·멤버 목록 조회를
// ERoomOp로 구분해 처리한다. 연산마다 입력과 콜백이 달라 요청 구조체는 연산별
// (DB*Request.h)로 두고, 이 파일은 op를 보고 알맞은 함수로 나눠 준다.
// 모든 연산은 어떤 경로에서도 onComplete를 정확히 한 번 호출한다.
//***************************************************************************

#include "pch.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>
#include <DB/DBAsyncHandler.h>

#include "../ChatCommon/Protocol/ChatPacketUtil.h"
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include "RoomDbUtil.h"
#include "DBRoomRequest.h"

#include <cstring>

namespace
{
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
	EDBReturnType HandleCreate(ST_CREATE_ROOM_REQ* req, OdbcConnGuard& guard)
	{
		const std::string ownerPublicIdHex = PublicIdToHex(ToPublicId(req->ownerPublicId));

		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ERoomResult::DbError, 0);
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 방장 한 명당 만들 수 있는 방 수 제한.
		if( req->maxRoomsPerOwner > 0 )
		{
			int32 currentCount = 0;
			if( !RoomDb::CountRows(sql, _T("SELECT COUNT(*) FROM rooms WHERE owner_public_id = ?"),
				{ ownerPublicIdHex }, currentCount) )
			{
				fail();
				return EDBReturnType::INVALID;
			}

			if( currentCount >= req->maxRoomsPerOwner )
			{
				if( req->onComplete )
					req->onComplete(ERoomResult::RoomLimitExceeded, 0);
				return EDBReturnType::OK; // DB 자체는 정상 처리됨(제한 초과는 "결과"이지 "오류"가 아님)
			}
		}

		if( !sql.Exec(_T("INSERT INTO rooms (name, owner_public_id) VALUES (?, ?)"),
			{ req->roomName, ownerPublicIdHex }) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		// 같은 연결에서 바로 조회해야 방금 INSERT의 AUTO_INCREMENT 값이 나온다.
		int64 newRoomId = 0;
		if( !sql.LastInsertId(newRoomId) || newRoomId <= 0 )
		{
			LOG_ERROR(_T("kDbCallIdent_Room(Create): LAST_INSERT_ID() 조회 실패 — 방은 생성됐으나 ID를 못 받음"));
			fail();
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ERoomResult::Ok, static_cast<int32>(newRoomId));
		return EDBReturnType::OK;
	}

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
	EDBReturnType HandleDelete(ST_DELETE_ROOM_REQ* req, OdbcConnGuard& guard)
	{
		const std::string requesterPublicIdHex = PublicIdToHex(ToPublicId(req->requesterPublicId));

		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError);
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		ERoomResult ownerCheck = ERoomResult::DbError;
		if( !RoomDb::CheckOwner(sql, req->roomId, requesterPublicIdHex, ownerCheck) ||
			(ownerCheck == ERoomResult::Ok && !sql.Exec(_T("DELETE FROM rooms WHERE room_id = ?"),
				{ std::to_string(req->roomId) })) )
		{
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError);
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ownerCheck);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 방 이름을 바꾼다. DeleteRoomDBHandler.cpp와 동일한 이유로
	//        소유권 확인(SELECT)과 실제 변경(UPDATE)을 2단계로 나눴다.
	//***************************************************************************
	EDBReturnType HandleRename(ST_RENAME_ROOM_REQ* req, OdbcConnGuard& guard)
	{
		const std::string requesterPublicIdHex = PublicIdToHex(ToPublicId(req->requesterPublicId));

		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError);
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		ERoomResult ownerCheck = ERoomResult::DbError;
		if( !RoomDb::CheckOwner(sql, req->roomId, requesterPublicIdHex, ownerCheck) ||
			(ownerCheck == ERoomResult::Ok && !sql.Exec(_T("UPDATE rooms SET name = ? WHERE room_id = ?"),
				{ req->newName, std::to_string(req->roomId) })) )
		{
			if( req->onComplete )
				req->onComplete(ERoomResult::DbError);
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ownerCheck);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 방 프로필 이미지를 설정/교체/해제한다.
	// @details DeleteRoomDBHandler.cpp/RenameRoomDBHandler.cpp와 동일한 이유로
	//          소유권 확인(SELECT)과 실제 변경(UPDATE)을 2단계로 나눴다 — 이
	//          SELECT가 동시에 "교체되기 전 이미지 값"도 가져와서 onComplete로
	//          돌려준다(호출부가 예전 파일 삭제 예약에 쓸 수 있도록).
	//***************************************************************************
	EDBReturnType HandleSetImage(ST_SET_ROOM_IMAGE_REQ* req, OdbcConnGuard& guard)
	{
		const std::string requesterPublicIdHex = PublicIdToHex(ToPublicId(req->requesterPublicId));

		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ERoomResult::DbError, std::string());
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 1. 존재 여부 + 소유권 확인 + 교체되기 전 이미지 값 조회(NULL이면 빈 문자열).
		ERoomResult ownerCheck = ERoomResult::DbError;
		std::string oldImageRef;
		if( !RoomDb::CheckOwner(sql, req->roomId, requesterPublicIdHex, ownerCheck, &oldImageRef) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( ownerCheck != ERoomResult::Ok )
		{
			if( req->onComplete )
				req->onComplete(ownerCheck, std::string());
			return EDBReturnType::OK;
		}

		// 2. 소유권 확인 통과 — 실제 이미지 값 갱신(빈 문자열이면 NULL로 해제).
		const std::string roomIdText = std::to_string(req->roomId);
		const bool updated = req->imageRef.empty()
			? sql.Exec(_T("UPDATE rooms SET image_ref = NULL WHERE room_id = ?"), { roomIdText })
			: sql.Exec(_T("UPDATE rooms SET image_ref = ? WHERE room_id = ?"), { req->imageRef, roomIdText });
		if( !updated )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ERoomResult::Ok, oldImageRef);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 방장을 newOwnerPublicId로 강제 변경한다. 소유권 검증 없음 —
	//        ST_TRANSFER_ROOM_OWNER_REQ 선언부의 설계 노트 참고(서버 내부
	//        판단으로만 호출되는 것을 전제로 함).
	//***************************************************************************
	EDBReturnType HandleTransferOwner(ST_TRANSFER_ROOM_OWNER_REQ* req, OdbcConnGuard& guard)
	{
		const std::string newOwnerPublicIdHex = PublicIdToHex(ToPublicId(req->newOwnerPublicId));

		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(false);
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		const bool ok = sql.Exec(_T("UPDATE rooms SET owner_public_id = ? WHERE room_id = ?"),
			{ newOwnerPublicIdHex, std::to_string(req->roomId) });

		if( req->onComplete )
			req->onComplete(ok);
		return ok ? EDBReturnType::OK : EDBReturnType::INVALID;
	}

	//***************************************************************************
	// @brief 사용자가 방에 입장했음을 room_members에 기록한다.
	// @details 처음 입장이면 행을 추가하고, 이미 있으면 last_entered_at만
	//          현재 시각으로 갱신한다(INSERT ... ON DUPLICATE KEY UPDATE).
	//          입장 직후 그 방이 삭제된 극히 좁은 경합에서는 외래 키 위반으로
	//          Execute()가 실패할 수 있는데, 삭제된 방에 대한 기록이 남지 않는
	//          것이 오히려 올바른 결과이므로 로그만 남기고 넘어간다.
	//***************************************************************************
	EDBReturnType HandleRecordJoin(ST_RECORD_ROOM_JOIN_REQ* req, OdbcConnGuard& guard)
	{
		const std::string requesterPublicIdHex = PublicIdToHex(ToPublicId(req->requesterPublicId));

		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError);
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		if( !sql.Exec(_T("INSERT INTO room_members (room_id, user_public_id) VALUES (?, ?) ")
			_T("ON DUPLICATE KEY UPDATE last_entered_at = CURRENT_TIMESTAMP"),
			{ std::to_string(req->roomId), requesterPublicIdHex }) )
		{
			LOG_INFO(_T("kDbCallIdent_Room(RecordJoin): 입장 기록 실패(roomId=%d) — 그 사이 방이 삭제됐을 수 있음"), req->roomId);
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError);
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 방 하나(roomId)의 이름/방장 닉네임/이미지를 조회한다.
	// @details ListRoomsDBHandler.cpp와 정확히 같은 SELECT(JOIN으로 방장의
	//          "지금" 닉네임을 같이 가져옴)에 WHERE room_id=?만 붙인 버전이다.
	//***************************************************************************
	EDBReturnType HandleGetInfo(ST_GET_ROOM_INFO_REQ* req, OdbcConnGuard& guard)
	{
		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, false, SRoomListEntry());
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(ROOM_ENTRY_SELECT_SQL _T("WHERE r.room_id = ?"), { std::to_string(req->roomId) }, 5, rows) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, false, SRoomListEntry());
			return EDBReturnType::INVALID;
		}

		SRoomListEntry entry;
		const bool found = !rows.empty() && RoomDb::ParseRoomRow(rows.front(), entry);

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, found, entry);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 존재하는 모든 방을 최신순으로 조회합니다. 방장의 "지금" 닉네임을
	//        같이 돌려주기 위해 users 테이블과 JOIN한다.
	//***************************************************************************
	EDBReturnType HandleList(ST_LIST_ROOMS_REQ* req, OdbcConnGuard& guard)
	{
		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(ROOM_ENTRY_SELECT_SQL _T("ORDER BY r.created_at DESC"), {}, 5, rows) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
			return EDBReturnType::INVALID;
		}

		std::vector<SRoomListEntry> rooms;
		rooms.reserve(rows.size());
		for( const DbUtil::Row& row : rows )
		{
			SRoomListEntry entry;
			if( RoomDb::ParseRoomRow(row, entry) )
				rooms.push_back(std::move(entry));
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, rooms);
		return EDBReturnType::OK;
	}

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
	EDBReturnType HandleListPage(ST_LIST_ROOMS_PAGE_REQ* req, OdbcConnGuard& guard)
	{
		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::DbError, 0, 0, std::vector<SRoomListEntry>());
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		const bool joined = (req->scope == ERoomListScope::Joined);
		const bool hasKeyword = !req->keyword.empty();
		const int32 pageSize = RoomDb::ClampPageSize(req->pageSize);

		// 조건 절 — COUNT 쿼리와 페이지 쿼리가 완전히 같은 FROM/WHERE와 파라미터를 쓴다.
		_tstring fromWhere;
		std::vector<std::string> params;
		if( joined )
		{
			fromWhere = _T("FROM room_members m ")
				_T("JOIN rooms r ON r.room_id = m.room_id ")
				_T("JOIN users u ON r.owner_public_id = u.public_id ")
				_T("WHERE m.user_public_id = ? ");
			params.push_back(PublicIdToHex(ToPublicId(req->requesterPublicId)));
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
			params.push_back(BuildLikePattern(req->keyword));
		}

		// 1. 조건에 맞는 전체 개수.
		int32 totalCount = 0;
		const _tstring countQuery = _tstring(_T("SELECT COUNT(*) ")) + fromWhere;
		if( !RoomDb::CountRows(sql, countQuery.c_str(), params, totalCount) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( totalCount <= 0 )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, 0, 0, std::vector<SRoomListEntry>());
			return EDBReturnType::OK;
		}

		// 2. 요청 페이지를 유효 범위로 보정.
		const int32 page = RoomDb::ClampPage(req->page, totalCount, pageSize);
		const int64 offset = static_cast<int64>(page) * pageSize;

		// 3. 해당 페이지 조회. totalMemberCount는 여기서 채우지 않는다 — 기본값(0)인 채로
		// 두면 호출부(CChatServerMain::RequestListRooms())가 Redis SCARD 결과로 채운다.
		const _tstring orderBy = joined
			? _tstring(_T("ORDER BY m.last_entered_at DESC, r.room_id DESC "))
			: _tstring(_T("ORDER BY r.created_at DESC, r.room_id DESC "));

		const _tstring pageQuery = _tstring(_T(
			"SELECT r.room_id, r.name, r.owner_public_id, u.nickname, r.image_ref "))
			+ fromWhere
			+ orderBy
			+ _T("LIMIT ") + Utf8ToTString(std::to_string(pageSize))
			+ _T(" OFFSET ") + Utf8ToTString(std::to_string(offset));

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(pageQuery.c_str(), params, 5, rows) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		std::vector<SRoomListEntry> rooms;
		rooms.reserve(rows.size());
		for( const DbUtil::Row& row : rows )
		{
			SRoomListEntry entry;
			if( RoomDb::ParseRoomRow(row, entry) )
				rooms.push_back(std::move(entry));
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, totalCount, page, rooms);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 방에 입장한 적이 있는 유저 목록의 한 페이지를 조회한다.
	// @details ListRoomsPageDBHandler.cpp와 동일한 2단계(전체 개수 -> 페이지
	//          보정 -> 해당 페이지 조회) 패턴을 쓴다. 대표 프로필 이미지는
	//          user_profile_images(status=1인 행)에서 상관 서브쿼리로 가져온다
	//          — AccountDBHandler.cpp의 로그인 조회와 동일한 방식이며, 그
	//          유저가 한 번도 대표 이미지를 설정한 적이 없으면 NULL이라 빈
	//          문자열로 처리된다(에러 아님).
	//***************************************************************************
	EDBReturnType HandleListMembersPage(ST_LIST_ROOM_MEMBERS_PAGE_REQ* req, OdbcConnGuard& guard)
	{
		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::DbError, 0, 0, std::vector<SRoomMemberHistoryEntry>());
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		const int32 pageSize = RoomDb::ClampPageSize(req->pageSize);
		const std::vector<std::string> params = { std::to_string(req->roomId) };

		// 1. 이 방에 입장 이력이 있는 전체 유저 수.
		int32 totalCount = 0;
		if( !RoomDb::CountRows(sql, _T("SELECT COUNT(*) FROM room_members WHERE room_id = ?"), params, totalCount) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( totalCount <= 0 )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, 0, 0, std::vector<SRoomMemberHistoryEntry>());
			return EDBReturnType::OK;
		}

		// 2. 요청 페이지를 유효 범위로 보정.
		const int32 page = RoomDb::ClampPage(req->page, totalCount, pageSize);
		const int64 offset = static_cast<int64>(page) * pageSize;

		// 3. 해당 페이지 조회 — 최근 입장순. LIMIT/OFFSET은 서버가 보정한 정수라
		// 파라미터 바인딩 대신 쿼리 문자열에 직접 넣는다(SQL 인젝션 위험 없음).
		const _tstring pageQuery = _tstring(_T(
			"SELECT u.public_id, u.nickname, "
			"(SELECT p.image_ref FROM user_profile_images p WHERE p.user_public_id = u.public_id AND p.status = 1 LIMIT 1) AS image_ref "
			"FROM room_members m "
			"JOIN users u ON u.public_id = m.user_public_id "
			"WHERE m.room_id = ? "
			"ORDER BY m.last_entered_at DESC, u.public_id "
			"LIMIT ")) + Utf8ToTString(std::to_string(pageSize))
			+ _T(" OFFSET ") + Utf8ToTString(std::to_string(offset));

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(pageQuery.c_str(), params, 3, rows) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		std::vector<SRoomMemberHistoryEntry> members;
		members.reserve(rows.size());
		for( const DbUtil::Row& row : rows )
		{
			SRoomMemberHistoryEntry entry;
			if( !HexToPublicId(row[0], entry.publicId) )
				continue; // 형식이 깨진 값 — 방어적으로 건너뜀

			entry.nickname = row[1];
			entry.profileImageUrl = row[2];	// NULL(대표 이미지 미설정)이면 빈 문자열
			members.push_back(std::move(entry));
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, totalCount, page, members);
		return EDBReturnType::OK;
	}

}

//***************************************************************************
// @brief 방 DB 요청 하나를 연산별 함수로 나눠 처리한다.
// @details 커넥션을 못 얻으면 로그는 여기서 한 번 남기고, 각 연산 함수가 자기 콜백으로
//          DbError를 돌려준다(콜백 모양이 연산마다 다르기 때문).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Room)
{
	ST_ROOM_REQ_BASE* base = static_cast<ST_ROOM_REQ_BASE*>(pStAsync);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
		LOG_ERROR(_T("kDbCallIdent_Room: No available ODBC connection in pool."));

	switch( base->op )
	{
	case ERoomOp::Create:			return HandleCreate(static_cast<ST_CREATE_ROOM_REQ*>(base), guard);
	case ERoomOp::Delete:			return HandleDelete(static_cast<ST_DELETE_ROOM_REQ*>(base), guard);
	case ERoomOp::Rename:			return HandleRename(static_cast<ST_RENAME_ROOM_REQ*>(base), guard);
	case ERoomOp::SetImage:			return HandleSetImage(static_cast<ST_SET_ROOM_IMAGE_REQ*>(base), guard);
	case ERoomOp::TransferOwner:	return HandleTransferOwner(static_cast<ST_TRANSFER_ROOM_OWNER_REQ*>(base), guard);
	case ERoomOp::RecordJoin:		return HandleRecordJoin(static_cast<ST_RECORD_ROOM_JOIN_REQ*>(base), guard);
	case ERoomOp::GetInfo:			return HandleGetInfo(static_cast<ST_GET_ROOM_INFO_REQ*>(base), guard);
	case ERoomOp::List:				return HandleList(static_cast<ST_LIST_ROOMS_REQ*>(base), guard);
	case ERoomOp::ListPage:			return HandleListPage(static_cast<ST_LIST_ROOMS_PAGE_REQ*>(base), guard);
	case ERoomOp::ListMembersPage:	return HandleListMembersPage(static_cast<ST_LIST_ROOM_MEMBERS_PAGE_REQ*>(base), guard);
	}

	LOG_ERROR(_T("kDbCallIdent_Room: unknown op %d"), static_cast<int>(base->op));
	return EDBReturnType::INVALID;
}