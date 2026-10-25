
//***************************************************************************
// RoomDbUtil.h : 방 관련 DB 핸들러(DBHandler/*Room*.cpp)가 공유하는 조회 헬퍼
//
// 소유권 확인, 방 목록 한 행 파싱, 페이지 보정처럼 핸들러마다 똑같이 쓰던
// 부분을 DbUtil::CSql 위로 모은다. DbHandlerUtil.h 뒤에 include 한다.
//***************************************************************************

#ifndef UC_ROOMDBUTIL_H
#define UC_ROOMDBUTIL_H

#include "DBRoomRequest.h"

namespace RoomDb
{
	//***************************************************************************
	// @brief 방 목록/상세 조회가 공통으로 쓰는 5개 컬럼 SELECT 절.
	//        (room_id, name, owner_public_id, nickname, image_ref)
	//***************************************************************************
#define ROOM_ENTRY_SELECT_SQL \
		_T("SELECT r.room_id, r.name, r.owner_public_id, u.nickname, r.image_ref ") \
		_T("FROM rooms r JOIN users u ON r.owner_public_id = u.public_id ")

	//***************************************************************************
	// @brief ROOM_ENTRY_SELECT_SQL의 한 행을 SRoomListEntry로 옮긴다.
	// @details image_ref가 NULL이면 빈 문자열(기본 이미지)이 된다. room_id가
	//          비어 있으면(NULL 등 깨진 행) false를 돌려줘 호출부가 건너뛴다.
	//***************************************************************************
	inline bool ParseRoomRow(const DbUtil::Row& row, SRoomListEntry& entry)
	{
		if( row.size() < 5 || row[0].empty() )
			return false;

		entry.roomId = static_cast<int32>(DbUtil::ToI64(row[0]));
		entry.name = row[1];
		entry.ownerPublicId = row[2];
		entry.ownerNickname = row[3];
		entry.imageRef = row[4];
		return true;
	}

	//***************************************************************************
	// @brief 방의 존재 여부와 방장 일치 여부를 확인한다.
	// @param result   Ok(요청자가 방장) / RoomNotFound / NotOwner
	// @param imageRef 필요하면 현재 image_ref(NULL이면 빈 문자열)를 돌려받는다.
	// @return DB 호출 자체가 성공했는지. false면 result는 의미 없다.
	//***************************************************************************
	inline bool CheckOwner(DbUtil::CSql& sql, int32 roomId, const std::string& requesterHex,
		ERoomResult& result, std::string* imageRef = nullptr)
	{
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT owner_public_id, image_ref FROM rooms WHERE room_id = ?"),
			{ std::to_string(roomId) }, 2, rows) )
			return false;

		if( rows.empty() )
			result = ERoomResult::RoomNotFound;
		else if( rows.front()[0] != requesterHex )
			result = ERoomResult::NotOwner;
		else
			result = ERoomResult::Ok;

		if( imageRef != nullptr && !rows.empty() )
			*imageRef = rows.front()[1];
		return true;
	}

	//***************************************************************************
	// @brief 페이지 크기를 1~kMaxRoomPageSize로 보정한다.
	//***************************************************************************
	inline int32 ClampPageSize(int32 pageSize)
	{
		return (std::min)((std::max)(pageSize, 1), static_cast<int32>(kMaxRoomPageSize));
	}

	//***************************************************************************
	// @brief 요청 페이지를 [0, 마지막 페이지]로 보정한다. totalCount는 1 이상.
	//***************************************************************************
	inline int32 ClampPage(int32 page, int32 totalCount, int32 pageSize)
	{
		const int32 lastPage = (totalCount - 1) / pageSize;
		return (std::min)((std::max)(page, 0), lastPage);
	}

	//***************************************************************************
	// @brief COUNT(*) 한 줄짜리 쿼리를 실행해 개수를 돌려준다.
	//***************************************************************************
	inline bool CountRows(DbUtil::CSql& sql, const TCHAR* countSql,
		const std::vector<std::string>& params, int32& totalCount)
	{
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(countSql, params, 1, rows) )
			return false;
		totalCount = rows.empty() ? 0 : static_cast<int32>(DbUtil::ToI64(rows.front()[0]));
		return true;
	}
}

#endif // ndef UC_ROOMDBUTIL_H