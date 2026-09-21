
//***************************************************************************
// DBListRoomsRequest.h : 방 목록 조회 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBLISTROOMSREQUEST_H
#define UC_DBLISTROOMSREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult

#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @brief 방 목록 항목 하나(DB 왕복용 평범한 구조체).
// @details ownerNickname은 users 테이블과의 JOIN으로 "지금 시점"의 닉네임을
//          가져온다 — rooms 테이블 자체엔 방장의 닉네임을 저장하지 않는다
//          (닉네임은 언제든 바뀔 수 있는 값이라, 저장해두면 방장이 닉네임을
//          바꿀 때마다 여기도 갱신해야 하는 동기화 부담이 생긴다).
//***************************************************************************
struct SRoomListEntry
{
	int32		roomId = 0;
	std::string	name;
	std::string	ownerPublicId;	// 16진 문자열
	std::string	ownerNickname;
};

//***************************************************************************
// @struct ST_LIST_ROOMS_REQ
// @brief 존재하는 모든 방을 최신순으로 조회한다 — 로그인만 하면 누구나 볼 수
//        있다(계정별로 필터링하지 않음).
//***************************************************************************
struct ST_LIST_ROOMS_REQ : public st_DBAsyncRq
{
	ST_LIST_ROOMS_REQ()
	{
		callIdent = kDbCallIdent_ListRooms;
		bReTry = false;
	}

	std::function<void(ELoginResult result, const std::vector<SRoomListEntry>& rooms)>	onComplete;
};

#endif // ndef UC_DBLISTROOMSREQUEST_H