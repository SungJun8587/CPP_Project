
//***************************************************************************
// DBTransferRoomOwnerRequest.h : 방장 자동 이양 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBTRANSFERROOMOWNERREQUEST_H
#define UC_DBTRANSFERROOMOWNERREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// kPublicIdBytes

#include <functional>

//***************************************************************************
// @struct ST_TRANSFER_ROOM_OWNER_REQ
// @brief 방장을 강제로 바꾼다. [설계] 이 요청엔 소유권 검증이 없다 — 클라이언트
//        요청이 아니라 서버 자신이 "방장이 방을 나갔고, 남은 사람 중
//        가장 오래 있었던 사람에게 이양한다"는 내부 판단을 이미 마친
//        뒤에만 호출하는 것을 전제로 한다(CChatServerMain의 방장 이양
//        로직 참고). 사용자 요청을 직접 이 구조체로 연결하면 안 된다.
//***************************************************************************
struct ST_TRANSFER_ROOM_OWNER_REQ : public st_DBAsyncRq
{
	ST_TRANSFER_ROOM_OWNER_REQ()
	{
		callIdent = kDbCallIdent_TransferRoomOwner;
		bReTry = false;
	}

	int32	roomId = 0;
	BYTE	newOwnerPublicId[kPublicIdBytes] = {};

	std::function<void(bool success)>	onComplete;
};

#endif // ndef UC_DBTRANSFERROOMOWNERREQUEST_H