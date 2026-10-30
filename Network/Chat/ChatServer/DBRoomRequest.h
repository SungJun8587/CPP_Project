
//***************************************************************************
// DBRoomRequest.h : 방 기능 DB 비동기 요청 구조체 모음
//
//***************************************************************************

#ifndef DBROOMREQUEST_H
#define DBROOMREQUEST_H

#include <DB/DBAsyncSrv.h>

#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @brief 방 기능 DB 연산 종류.
// @details 방 기능의 모든 요청은 kDbCallIdent_Room(ChatPacketTypes.h) 하나와 핸들러
//          하나(RoomDBHandler.cpp)로 처리하고, 연산은 이 값으로 구분한다. 연산마다
//          입력과 결과 콜백의 모양이 달라서 요청 구조체는 연산별로 두고
//          (이 파일 아래), 모두 ST_ROOM_REQ_BASE를 상속해 op를 실어 보낸다.
//***************************************************************************
enum class ERoomOp : uint8
{
	Create,			// 방 생성 — ST_CREATE_ROOM_REQ
	Delete,			// 방 삭제 — ST_DELETE_ROOM_REQ
	Rename,			// 방 이름 변경 — ST_RENAME_ROOM_REQ
	SetImage,		// 방 이미지 설정/교체/해제 — ST_SET_ROOM_IMAGE_REQ
	TransferOwner,	// 방장 자동 이양(서버 내부 전용) — ST_TRANSFER_ROOM_OWNER_REQ
	RecordJoin,		// 방 입장 기록 — ST_RECORD_ROOM_JOIN_REQ
	GetInfo,		// 방 하나의 정보 조회 — ST_GET_ROOM_INFO_REQ
	List,			// 전체 방 목록(시작 시 레지스트리 적재용) — ST_LIST_ROOMS_REQ
	ListPage,		// 방 목록 페이지 조회 — ST_LIST_ROOMS_PAGE_REQ
	ListMembersPage,// 방 입장 이력 유저 목록 페이지 조회 — ST_LIST_ROOM_MEMBERS_PAGE_REQ
};

//***************************************************************************
// @struct ST_ROOM_REQ_BASE
// @brief 방 DB 요청의 공통 부분. 핸들러가 op를 보고 실제 구조체로 캐스팅한다.
//***************************************************************************
struct ST_ROOM_REQ_BASE : public st_DBAsyncRq
{
	explicit ST_ROOM_REQ_BASE(ERoomOp operation) : op(operation)
	{
		callIdent = kDbCallIdent_Room;
		bReTry = false;
	}

	const ERoomOp	op;
};

//***************************************************************************
// @struct ST_CREATE_ROOM_REQ
// @brief 새 방을 만든다. 생성과 동시에 요청자가 방장이 된다.
//***************************************************************************
struct ST_CREATE_ROOM_REQ : public ST_ROOM_REQ_BASE
{
	ST_CREATE_ROOM_REQ() : ST_ROOM_REQ_BASE(ERoomOp::Create) {}

	BYTE		ownerPublicId[kPublicIdBytes] = {};	// 생성 요청자(=새 방장)의 안정 식별자
	std::string	roomName;								// 방 이름(형식 검증은 ChatRoomHandler.cpp가 먼저 수행)

	// [설계] 1인당 생성 가능한 방 개수 상한을 호출부(패킷 핸들러)가 서버
	// 설정에서 읽어 여기 실어 보낸다 — DB 핸들러 자신은 설정 파일에 접근할
	// 방법이 없으므로(DB 계층은 설정과 완전히 독립적으로 설계됨). 0
	// 이하면 개수 제한을 적용하지 않는다.
	int32		maxRoomsPerOwner = 0;

	std::function<void(ERoomResult result, int32 newRoomId)>	onComplete;
};

//***************************************************************************
// @struct ST_DELETE_ROOM_REQ
// @brief 방을 삭제한다. requesterPublicId가 그 방의 현재 방장이어야 한다.
//***************************************************************************
struct ST_DELETE_ROOM_REQ : public ST_ROOM_REQ_BASE
{
	ST_DELETE_ROOM_REQ() : ST_ROOM_REQ_BASE(ERoomOp::Delete) {}

	int32	roomId = 0;
	BYTE	requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상 — 방장이 아니면 NotOwner

	std::function<void(ERoomResult result)>	onComplete;
};

//***************************************************************************
// @struct ST_RENAME_ROOM_REQ
// @brief 방 이름을 바꾼다. requesterPublicId가 그 방의 현재 방장이어야 한다.
//***************************************************************************
struct ST_RENAME_ROOM_REQ : public ST_ROOM_REQ_BASE
{
	ST_RENAME_ROOM_REQ() : ST_ROOM_REQ_BASE(ERoomOp::Rename) {}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상
	std::string	newName;										// 형식 검증은 ChatRoomHandler.cpp가 먼저 수행

	std::function<void(ERoomResult result)>	onComplete;
};

//***************************************************************************
// @struct ST_SET_ROOM_IMAGE_REQ
// @brief 방 프로필 이미지를 설정/교체/해제한다. requesterPublicId가 그 방의
//        현재 방장이어야 한다. 갤러리가 아니라 단일 값이라 — 새 값을
//        저장하면 예전 값은 그냥 덮어써진다(user_profile_images처럼 여러
//        행을 관리하지 않음).
//***************************************************************************
struct ST_SET_ROOM_IMAGE_REQ : public ST_ROOM_REQ_BASE
{
	ST_SET_ROOM_IMAGE_REQ() : ST_ROOM_REQ_BASE(ERoomOp::SetImage) {}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};	// 방장인지 확인할 대상
	std::string	imageRef;									// 새 이미지 참조(상대경로 또는 외부 URL). 빈 문자열이면 해제

	// onComplete: oldImageRef는 교체되기 전 값(빈 문자열일 수 있음) — 호출부가
	// 그 값이 우리 파일 서버 소유면 실제 파일 삭제를 예약하는 데 쓴다.
	std::function<void(ERoomResult result, const std::string& oldImageRef)>	onComplete;
};

//***************************************************************************
// @struct ST_TRANSFER_ROOM_OWNER_REQ
// @brief 방장을 강제로 바꾼다. [설계] 이 요청엔 소유권 검증이 없다 — 클라이언트
//        요청이 아니라 서버 자신이 "방장이 방을 나갔고, 남은 사람 중
//        가장 오래 있었던 사람에게 이양한다"는 내부 판단을 이미 마친
//        뒤에만 호출하는 것을 전제로 한다(CChatServerMain의 방장 이양
//        로직 참고). 사용자 요청을 직접 이 구조체로 연결하면 안 된다.
//***************************************************************************
struct ST_TRANSFER_ROOM_OWNER_REQ : public ST_ROOM_REQ_BASE
{
	ST_TRANSFER_ROOM_OWNER_REQ() : ST_ROOM_REQ_BASE(ERoomOp::TransferOwner) {}

	int32	roomId = 0;
	BYTE	newOwnerPublicId[kPublicIdBytes] = {};

	std::function<void(bool success)>	onComplete;
};

//***************************************************************************
// @struct ST_RECORD_ROOM_JOIN_REQ
// @brief 사용자가 방에 입장했음을 room_members 테이블에 기록한다.
// @details 처음 입장이면 행을 추가하고, 이미 있으면 last_entered_at만
//          갱신한다. "내가 참여한 방" 목록(ERoomListScope::Joined)의
//          근거가 되는 데이터다. 결과를 기다릴 필요가 없는 기록성 요청이라
//          onComplete는 선택 사항이다.
//***************************************************************************
struct ST_RECORD_ROOM_JOIN_REQ : public ST_ROOM_REQ_BASE
{
	ST_RECORD_ROOM_JOIN_REQ() : ST_ROOM_REQ_BASE(ERoomOp::RecordJoin) {}

	int32		roomId = 0;
	BYTE		requesterPublicId[kPublicIdBytes] = {};

	std::function<void(ELoginResult result)>	onComplete;
};

// [수정 — 통합] 방 DB 요청의 식별자(kDbCallIdent_Room)는 ChatPacketTypes.h에
// 모여있다 — 연산 종류는 DBRoomRequest.h의 ERoomOp로 구분한다.

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
	//        핸들러(RoomDBHandler.cpp의 HandleListPage)는 이 값을 채우지 않고
	//        기본값(0)인 채로 둔다 — 호출부인 CChatServerMain::
	//        RequestListRooms()가 DB 결과를 받은 뒤 Redis Set
	//        (RoomMembers:{roomId})의 SCARD로 채운다(ChatServerKeys::BuildRoomMembersKey()
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
struct ST_LIST_ROOMS_REQ : public ST_ROOM_REQ_BASE
{
	ST_LIST_ROOMS_REQ() : ST_ROOM_REQ_BASE(ERoomOp::List) {}

	std::function<void(ELoginResult result, const std::vector<SRoomListEntry>& rooms)>	onComplete;
};

//***************************************************************************
// @struct ST_GET_ROOM_INFO_REQ
// @brief 방 하나(roomId)의 이름/방장 닉네임/이미지를 조회한다.
// @details [설계] ChatRoomHandler.cpp가 방 입장 성공 직후 이 요청으로 방
//          정보를 가져와 RoomEnterResPacket에 그대로 실어 보낸다 — 클라이언트가
//          별도로 방 목록 캐시를 참조하거나 그 캐시가 준비될 때까지 기다릴
//          필요 없이, 입장 응답 하나로 이름/방장/이미지까지 한 번에 받는다.
//          (예전엔 클라이언트가 RoomListPanel의 목록 캐시에 의존했는데,
//          로그인 직후 재접속하면서 곧바로 마지막 방에 자동 재입장하는
//          것처럼 목록이 아직 로딩되기 전에 입장하는 타이밍이면 캐시가
//          비어있어 방 이름/방장/이미지가 안 채워지는 문제가 있었다.)
//***************************************************************************
struct ST_GET_ROOM_INFO_REQ : public ST_ROOM_REQ_BASE
{
	ST_GET_ROOM_INFO_REQ() : ST_ROOM_REQ_BASE(ERoomOp::GetInfo) {}

	int32	roomId = 0;

	// found: 그 roomId가 실제로 존재하면 true(방 삭제 직후의 아주 좁은
	// 경합 등으로 없을 수도 있음 — 그 경우 info는 비어있는 채로 온다).
	std::function<void(ELoginResult result, bool found, const SRoomListEntry& info)>	onComplete;
};

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
struct ST_LIST_ROOMS_PAGE_REQ : public ST_ROOM_REQ_BASE
{
	ST_LIST_ROOMS_PAGE_REQ() : ST_ROOM_REQ_BASE(ERoomOp::ListPage) {}

	ERoomListScope	scope = ERoomListScope::All;
	BYTE			requesterPublicId[kPublicIdBytes] = {};	// Joined일 때만 사용
	std::string		keyword;									// UTF-8. 빈 문자열이면 검색 조건 없음
	int32			page = 0;									// 0부터 시작
	int32			pageSize = 10;

	// totalCount: 조건에 맞는 전체 방 수, page: 실제로 조회한(보정된) 페이지 번호
	std::function<void(ELoginResult result, int32 totalCount, int32 page, const std::vector<SRoomListEntry>& rooms)>	onComplete;
};

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
struct ST_LIST_ROOM_MEMBERS_PAGE_REQ : public ST_ROOM_REQ_BASE
{
	ST_LIST_ROOM_MEMBERS_PAGE_REQ() : ST_ROOM_REQ_BASE(ERoomOp::ListMembersPage) {}

	int32			roomId = 0;
	int32			page = 0;		// 0부터 시작
	int32			pageSize = 20;

	// totalCount: 이 방에 입장 이력이 있는 전체 유저 수, page: 실제로 조회한(보정된) 페이지 번호
	std::function<void(ELoginResult result, int32 totalCount, int32 page, const std::vector<SRoomMemberHistoryEntry>& members)>	onComplete;
};

#endif // ndef DBROOMREQUEST_H