
//***************************************************************************
// DBListRoomMembersPageRequest.h : 방 입장 이력 유저 목록 페이지 조회 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBLISTROOMMEMBERSPAGEREQUEST_H
#define UC_DBLISTROOMMEMBERSPAGEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @struct SRoomMemberHistoryEntry
// @brief 방에 입장한 적이 있는 유저 한 명. 접속 상태(online)는 이 DB 조회
//        결과에는 없다 — CChatServerMain::RequestRoomMemberHistoryPage()가
//        DB 응답을 받은 뒤 서버 메모리(IsUserOnline())에서 그 자리에 붙인다.
//***************************************************************************
struct SRoomMemberHistoryEntry
{
	std::array<BYTE, kPublicIdBytes> publicId{};
	std::string nickname;
	std::string profileImageUrl;	// 저장된 그대로(상대경로일 수 있음) — 호출부가 ToDisplayImageUrl()로 변환
};

//***************************************************************************
// @struct ST_LIST_ROOM_MEMBERS_PAGE_REQ
// @brief 지정한 방에 입장한 적이 있는 유저 목록의 한 페이지를 조회한다.
// @details room_members(입장 이력)를 users/user_profile_images(대표 이미지)와
//          조인해서 가져온다. 최근 입장순으로 정렬한다. 호출부(CChatServerMain::
//          RequestRoomMemberHistoryPage())가 pageSize를 1~kMaxRoomMemberPageSize로
//          보정해서 넘긴다. page가 마지막 페이지를 넘으면 마지막 페이지로
//          보정하고, 실제로 조회한 페이지 번호를 onComplete의 page 인자로
//          돌려준다(ListRoomsPageDBHandler.cpp와 동일한 보정 방식).
//***************************************************************************
struct ST_LIST_ROOM_MEMBERS_PAGE_REQ : public st_DBAsyncRq
{
	ST_LIST_ROOM_MEMBERS_PAGE_REQ()
	{
		callIdent = kDbCallIdent_ListRoomMembersPage;
		bReTry = false;
	}

	int32			roomId = 0;
	int32			page = 0;		// 0부터 시작
	int32			pageSize = 20;

	// totalCount: 이 방에 입장 이력이 있는 전체 유저 수, page: 실제로 조회한(보정된) 페이지 번호
	std::function<void(ELoginResult result, int32 totalCount, int32 page, const std::vector<SRoomMemberHistoryEntry>& members)>	onComplete;
};

#endif // ndef UC_DBLISTROOMMEMBERSPAGEREQUEST_H