
//***************************************************************************
// DBGetRoomInfoRequest.h : 방 하나의 이름/방장/이미지 조회 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBGETROOMINFOREQUEST_H
#define UC_DBGETROOMINFOREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		
#include "DBListRoomsRequest.h"

#include <functional>

//***************************************************************************
// @struct ST_GET_ROOM_INFO_REQ
// @brief 방 하나(roomId)의 이름/방장 닉네임/이미지를 조회한다.
// @details [설계] RoomEnterHandler.cpp가 방 입장 성공 직후 이 요청으로 방
//          정보를 가져와 RoomEnterResPacket에 그대로 실어 보낸다 — 클라이언트가
//          별도로 방 목록 캐시를 참조하거나 그 캐시가 준비될 때까지 기다릴
//          필요 없이, 입장 응답 하나로 이름/방장/이미지까지 한 번에 받는다.
//          (예전엔 클라이언트가 RoomListPanel의 목록 캐시에 의존했는데,
//          로그인 직후 재접속하면서 곧바로 마지막 방에 자동 재입장하는
//          것처럼 목록이 아직 로딩되기 전에 입장하는 타이밍이면 캐시가
//          비어있어 방 이름/방장/이미지가 안 채워지는 문제가 있었다.)
//***************************************************************************
struct ST_GET_ROOM_INFO_REQ : public st_DBAsyncRq
{
	ST_GET_ROOM_INFO_REQ()
	{
		callIdent = kDbCallIdent_GetRoomInfo;
		bReTry = false;
	}

	int32	roomId = 0;

	// found: 그 roomId가 실제로 존재하면 true(방 삭제 직후의 아주 좁은
	// 경합 등으로 없을 수도 있음 — 그 경우 info는 비어있는 채로 온다).
	std::function<void(ELoginResult result, bool found, const SRoomListEntry& info)>	onComplete;
};

#endif // ndef UC_DBGETROOMINFOREQUEST_H