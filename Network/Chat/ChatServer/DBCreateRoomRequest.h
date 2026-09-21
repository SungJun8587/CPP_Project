
//***************************************************************************
// DBCreateRoomRequest.h : 방 생성 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBCREATEROOMREQUEST_H
#define UC_DBCREATEROOMREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ERoomResult, kPublicIdBytes

#include <functional>
#include <string>

//***************************************************************************
// @struct ST_CREATE_ROOM_REQ
// @brief 새 방을 만든다. 생성과 동시에 요청자가 방장이 된다.
//***************************************************************************
struct ST_CREATE_ROOM_REQ : public st_DBAsyncRq
{
	ST_CREATE_ROOM_REQ()
	{
		callIdent = kDbCallIdent_CreateRoom;
		bReTry = false;
	}

	BYTE		ownerPublicId[kPublicIdBytes] = {};	// 생성 요청자(=새 방장)의 안정 식별자
	std::string	roomName;								// 방 이름(형식 검증은 CreateRoomHandler.cpp가 먼저 수행)

	// [설계] 1인당 생성 가능한 방 개수 상한을 호출부(패킷 핸들러)가 서버
	// 설정에서 읽어 여기 실어 보낸다 — DB 핸들러 자신은 설정 파일에 접근할
	// 방법이 없으므로(DB 계층은 설정과 완전히 독립적으로 설계됨). 0
	// 이하면 개수 제한을 적용하지 않는다.
	int32		maxRoomsPerOwner = 0;

	std::function<void(ERoomResult result, int32 newRoomId)>	onComplete;
};

#endif // ndef UC_DBCREATEROOMREQUEST_H