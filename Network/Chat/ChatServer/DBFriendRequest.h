
//***************************************************************************
// DBFriendRequest.h : 친구 기능 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef DBFRIENDREQUEST_H
#define DBFRIENDREQUEST_H

#include <DB/DBAsyncSrv.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @brief 친구 기능 DB 연산 종류.
// @details 친구 기능은 연산만 다른 동일 형태의 요청이라 kDbCallIdent_Friend
//          (ChatPacketTypes.h) 하나와 핸들러 하나(FriendDBHandler.cpp)로 처리한다.
//***************************************************************************
enum class EFriendOp : uint8
{
	SendRequest,	// 친구 요청 (대상: 닉네임)
	Accept,			// 요청 수락 (대상: publicId = 요청 보낸 쪽)
	Reject,			// 요청 거절
	Remove,			// 친구 삭제
	ListFriends,	// 친구 목록
	ListPending,	// 내가 받은 대기 요청 목록
	Block,			// 차단 (대상: 닉네임)
	Unblock,		// 차단 해제 (대상: publicId)
	ListBlocks,		// 차단 목록
};

//***************************************************************************
// @brief 목록/대상 계정 한 명의 DB 왕복용 정보 (프로토콜 패킷과는 별개)
//***************************************************************************
struct SFriendEntry
{
	std::array<BYTE, kPublicIdBytes>	publicId{};
	std::string							nickname;
	std::string							profileImageRef;	// DB에 저장된 image_ref (표시용 URL 복원은 서버가 수행)
};

//***************************************************************************
// @brief 연산 결과. 연산별로 쓰이는 필드가 다르다.
//***************************************************************************
struct SFriendOpResult
{
	EFriendResult				result = EFriendResult::DbError;
	SFriendEntry				target;					// 단건 연산의 상대 계정 (실패 시 비어 있을 수 있음)
	std::vector<SFriendEntry>	entries;				// List* 연산 결과
	bool						autoAccepted = false;	// SendRequest가 상대의 기존 요청과 맞물려 즉시 친구가 됨
	bool						silent = false;			// 상대가 나를 차단한 상태 — 성공으로 응답하되 상대에게 알림/저장 없음
	bool						friendshipRemoved = false;	// Block이 기존 친구 관계를 끊음
	int32						totalCount = 0;				// ListPending: 받은 요청 전체 개수
	int32						page = 0;					// ListPending: 서버가 보정해 실제 적용한 페이지 번호
	int32						pageSize = 0;				// ListPending: 서버가 보정해 실제 적용한 페이지 크기
};

//***************************************************************************
// @struct ST_FRIEND_REQ
//***************************************************************************
struct ST_FRIEND_REQ : public st_DBAsyncRq
{
	ST_FRIEND_REQ()
	{
		callIdent = kDbCallIdent_Friend;
		bReTry = false;
	}

	EFriendOp	op = EFriendOp::ListFriends;
	BYTE		publicId[kPublicIdBytes] = {};		// 요청자
	bool		hasTargetId = false;				// true면 targetPublicId, false면 targetNickname으로 대상 지정
	BYTE		targetPublicId[kPublicIdBytes] = {};
	char		targetNickname[kNicknameBytes] = {};
	int32		page = 0;							// ListPending 전용: 요청한 페이지 번호(0부터)
	int32		pageSize = 0;						// ListPending 전용: 요청한 페이지 크기

	std::function<void(const SFriendOpResult& result)>	onComplete;
};

#endif // ndef DBFRIENDREQUEST_H