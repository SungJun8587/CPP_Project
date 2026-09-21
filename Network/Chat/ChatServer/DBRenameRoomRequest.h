
//***************************************************************************
// DBRenameRoomRequest.h : 방 이름 변경 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBRENAMEROOMREQUEST_H
#define UC_DBRENAMEROOMREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ERoomResult, kPublicIdBytes

#include <functional>
#include <string>

//***************************************************************************
// @struct ST_RENAME_ROOM_REQ
// @brief 방 이름을 바꾼다. requesterPublicId가 그 방의 현재 방장이어야 한다.
//***************************************************************************
struct ST_RENAME_ROOM_REQ : public st_DBAsyncRq
{
	ST_RENAME_ROOM_REQ()
	{
		callIdent = kDbCallIdent_RenameRoom;
		bReTry = false;
	}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상
	std::string	newName;										// 형식 검증은 RenameRoomHandler.cpp가 먼저 수행

	std::function<void(ERoomResult result)>	onComplete;
};

#endif // ndef UC_DBRENAMEROOMREQUEST_H