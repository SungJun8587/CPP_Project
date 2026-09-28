
//***************************************************************************
// DBRecordRoomJoinRequest.h : 방 입장 기록 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBRECORDROOMJOINREQUEST_H
#define UC_DBRECORDROOMJOINREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>

//***************************************************************************
// @struct ST_RECORD_ROOM_JOIN_REQ
// @brief 사용자가 방에 입장했음을 room_members 테이블에 기록한다.
// @details 처음 입장이면 행을 추가하고, 이미 있으면 last_entered_at만
//          갱신한다. "내가 참여한 방" 목록(ERoomListScope::Joined)의
//          근거가 되는 데이터다. 결과를 기다릴 필요가 없는 기록성 요청이라
//          onComplete는 선택 사항이다.
//***************************************************************************
struct ST_RECORD_ROOM_JOIN_REQ : public st_DBAsyncRq
{
	ST_RECORD_ROOM_JOIN_REQ()
	{
		callIdent = kDbCallIdent_RecordRoomJoin;
		bReTry = false;
	}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};

	std::function<void(ELoginResult result)>	onComplete;
};

#endif // ndef UC_DBRECORDROOMJOINREQUEST_H