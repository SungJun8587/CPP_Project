
//***************************************************************************
// DBListRoomsPageRequest.h : 방 목록 페이지 조회 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBLISTROOMSPAGEREQUEST_H
#define UC_DBLISTROOMSPAGEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"			
#include "DBListRoomsRequest.h"

#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @struct ST_LIST_ROOMS_PAGE_REQ
// @brief 방 목록의 한 페이지를 조회한다.
// @details scope가 All이면 존재하는 모든 방을 최신 생성순으로, Joined면
//          requesterPublicId가 입장한 적이 있는 방을 최근 입장순으로
//          조회한다. keyword가 비어있지 않으면 방 이름에 그 문자열이
//          포함된 방만 대상으로 한다(대소문자 구분은 DB 콜레이션을 따름).
//
//          호출부(CChatServerMain::RequestListRooms())가 pageSize를
//          1~kMaxRoomPageSize로 보정해서 넘긴다. page가 마지막 페이지를
//          넘으면 DB 핸들러가 마지막 페이지로 보정하고, 실제로 조회한
//          페이지 번호를 onComplete의 page 인자로 돌려준다.
//***************************************************************************
struct ST_LIST_ROOMS_PAGE_REQ : public st_DBAsyncRq
{
	ST_LIST_ROOMS_PAGE_REQ()
	{
		callIdent = kDbCallIdent_ListRoomsPage;
		bReTry = false;
	}

	ERoomListScope	scope = ERoomListScope::All;
	BYTE			requesterPublicId[kPublicIdBytes] = {};	// Joined일 때만 사용
	std::string		keyword;									// UTF-8. 빈 문자열이면 검색 조건 없음
	int32			page = 0;									// 0부터 시작
	int32			pageSize = 10;

	// totalCount: 조건에 맞는 전체 방 수, page: 실제로 조회한(보정된) 페이지 번호
	std::function<void(ELoginResult result, int32 totalCount, int32 page, const std::vector<SRoomListEntry>& rooms)>	onComplete;
};

#endif // ndef UC_DBLISTROOMSPAGEREQUEST_H