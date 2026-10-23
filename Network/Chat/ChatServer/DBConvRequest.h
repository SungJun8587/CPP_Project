
//***************************************************************************
// DBConvRequest.h : 대화방(1:1/그룹) DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBCONVREQUEST_H
#define UC_DBCONVREQUEST_H

#include <DB/DBAsyncSrv.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

//***************************************************************************
// @brief 대화방 DB 연산 종류.
// @details 연산만 다른 동일 형태의 요청이라 kDbCallIdent_Conv(ChatPacketTypes.h) 하나와
//          핸들러 하나(ConvDBHandler.cpp)로 처리한다(친구 기능과 같은 구조).
//***************************************************************************
enum class EConvOp : uint8
{
	OpenDirect,		// 친구와의 1:1 대화방 열기(없으면 생성). 대상: targets[0]
	CreateGroup,	// 그룹 생성. 대상: targets[0..targetCount-1], 이름: name
	Invite,			// 그룹 초대. 대상: targets[...], 대화방: convId
	Leave,			// 나가기
	List,			// 내 대화방 목록
	Send,			// 메시지 저장
	History,		// 지난 메시지 조회
	MarkRead,		// 읽음 위치 갱신
	Members,		// 멤버 목록
	Delete,			// 내가 보낸 메시지 삭제(소프트 삭제)
	Rename,			// 그룹 대화방 이름 변경
};

//***************************************************************************
// @brief 대화방 멤버 한 명. DB 왕복용(프로토콜 패킷과는 별개).
//***************************************************************************
struct SConvMember
{
	std::array<BYTE, kPublicIdBytes>	publicId{};
	std::string							nickname;
	std::string							profileImageRef;	// DB의 image_ref (표시용 URL 복원은 서버가 수행)
	int64								lastReadMsgId = 0;
};

//***************************************************************************
// @brief 대화방 요약(요청자 기준). 안 읽은 수와 읽음 위치는 요청자의 값이다.
//***************************************************************************
struct SConvSummary
{
	int64			convId = 0;
	uint8			type = 0;				// EConvType
	std::string		name;
	int32			memberCount = 0;
	int32			unreadCount = 0;
	int64			lastMsgId = 0;
	int64			lastMsgTime = 0;
	uint8			lastKind = 0;			// EConvMessageKind
	std::string		lastMessage;			// 잘리지 않은 원문(패킷에 담을 때 서버가 자른다)
	int64			myLastReadMsgId = 0;
	SConvMember		peer;					// 1:1일 때만 채워진다(상대가 나간 뒤에도 유지)
};

//***************************************************************************
// @brief 메시지 한 건.
//***************************************************************************
struct SConvMessage
{
	int64			convId = 0;
	int64			msgId = 0;
	uint8			kind = 0;				// EConvMessageKind
	bool			hasSender = false;
	std::array<BYTE, kPublicIdBytes>	sender{};
	std::string		senderNickname;
	std::string		senderImageRef;
	int64			timestamp = 0;			// Unix 초
	std::string		body;
	int64			replyToMsgId = 0;		// 0이면 답장 아님
	uint8			replyKind = 0;			// 답장 대상의 EConvMessageKind (보이지 않거나 삭제됐으면 Deleted)
	std::string		replySenderNickname;
	std::string		replyBody;				// 대상이 Text일 때만 의미 있음
};

//***************************************************************************
// @brief 연산 결과. 연산별로 쓰이는 필드가 다르다.
//***************************************************************************
struct SConvOpResult
{
	EConvResult								result = EConvResult::DbError;
	int64									convId = 0;
	bool									isNew = false;			// OpenDirect/CreateGroup: 방금 만들어짐
	bool									silent = false;			// Send(1:1): 상대가 나를 차단함 — 저장만 하고 상대에게 푸시하지 않음
	bool									hasMore = false;		// History: 더 오래된 메시지가 있음
	int64									readUpTo = 0;			// MarkRead: 실제 반영된 읽음 위치
	int64									deletedMsgId = 0;		// Delete: 삭제된 메시지(recipients에게 알린다)
	std::string								renamedTo;				// Rename: 저장된 새 이름(빈 문자열이면 해제)

	SConvSummary							summary;				// OpenDirect/CreateGroup/Invite: 요청자 기준 요약
	std::vector<SConvSummary>				convs;					// List
	std::vector<SConvMember>				members;				// Members
	std::vector<SConvMessage>				messages;				// History (오래된 것부터)

	bool									hasNewMsg = false;		// Send/CreateGroup/Invite/Leave가 저장한 메시지가 있음
	SConvMessage							newMsg;
	std::vector<std::array<BYTE, kPublicIdBytes>>	recipients;		// 푸시 대상(활성 멤버 전원, 요청자 포함). MarkRead는 요청자 제외
	std::vector<std::array<BYTE, kPublicIdBytes>>	addedMembers;	// CreateGroup/Invite: 새로 들어온 멤버
};

//***************************************************************************
// @struct ST_CONV_REQ
//***************************************************************************
struct ST_CONV_REQ : public st_DBAsyncRq
{
	ST_CONV_REQ()
	{
		callIdent = kDbCallIdent_Conv;
		bReTry = false;
	}

	EConvOp		op = EConvOp::List;
	BYTE		publicId[kPublicIdBytes] = {};			// 요청자
	int64		convId = 0;
	int32		targetCount = 0;
	BYTE		targets[kMaxConvInvite][kPublicIdBytes] = {};
	char		name[kConvNameBytes] = {};
	char		message[kConvMessageBytes] = {};
	int64		beforeMsgId = 0;						// History
	int32		count = 0;								// History: 요청한 개수(핸들러가 보정)
	int64		upToMsgId = 0;							// MarkRead
	int64		msgId = 0;								// Delete: 대상 메시지
	int64		replyToMsgId = 0;						// Send: 답장 대상(0이면 없음)
	uint8		msgKind = 0;							// Send: EConvMessageKind (Text/Image). Image면 message는 저장용 이미지 참조

	std::function<void(const SConvOpResult& result)>	onComplete;
};

#endif // ndef UC_DBCONVREQUEST_H