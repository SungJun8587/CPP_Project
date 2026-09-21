
//***************************************************************************
// DBDeleteRoomRequest.h : 방 삭제 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBDELETEROOMREQUEST_H
#define UC_DBDELETEROOMREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ERoomResult, kPublicIdBytes

#include <functional>

//***************************************************************************
// @struct ST_DELETE_ROOM_REQ
// @brief 방을 삭제한다. requesterPublicId가 그 방의 현재 방장이어야 한다.
//***************************************************************************
struct ST_DELETE_ROOM_REQ : public st_DBAsyncRq
{
	ST_DELETE_ROOM_REQ()
	{
		callIdent = kDbCallIdent_DeleteRoom;
		bReTry = false;
	}

	int32	roomId = 0;
	BYTE	requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상 — 방장이 아니면 NotOwner

	std::function<void(ERoomResult result)>	onComplete;
};

#endif // ndef UC_DBDELETEROOMREQUEST_H