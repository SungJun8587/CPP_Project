
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

// [수정 — 통합] kDbCallIdent_ListRooms 값은 이제 ChatPacketTypes.h에
// 모여있다 — ChatPacket.h가 그 헤더를 include하므로 그대로 쓸 수 있다.

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
	std::string	imageRef;		// [추가] 방 프로필 이미지. 비어있으면(NULL) 기본 이미지

	//***************************************************************************
	// @brief [추가] 이 방에 입장한 적이 있는 전체 유저 수. 이 DB 조회
	//        핸들러(ListRoomsPageDBHandler.cpp)는 이 값을 채우지 않고
	//        기본값(0)인 채로 둔다 — 호출부인 CChatServerMain::
	//        RequestListRooms()가 DB 결과를 받은 뒤 Redis Set
	//        (RoomMembers:{roomId})의 SCARD로 채운다(BuildRoomMembersKey()
	//        참고). 지금 접속해 있는 인원수(서버 메모리 기준)는 이
	//        구조체가 아니라 CChatServerMain::GetRoomUserCount()가 별도로
	//        채운다 — 방 목록 화면에서 "현재 접속자수/전체 참여 인원수"를
	//        나란히 보여주기 위해 둘을 구분해서 관리한다.
	//***************************************************************************
	int32		totalMemberCount = 0;
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