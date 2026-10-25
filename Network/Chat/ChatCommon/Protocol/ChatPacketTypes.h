
//***************************************************************************
// ChatPacketTypes.h : 채팅 패킷 관련 상수 및 열거형 정의
//
//***************************************************************************

#ifndef UC_CHATPACKETTYPES_H
#define UC_CHATPACKETTYPES_H

#include <cstddef>
#include <cstdint>

//***************************************************************************
// @brief 재접속 토큰 및 SHA-256 해시 바이트 크기 (256비트)
// @details 프로토콜 상수로 사용되며, 실제 해시 및 난수 생성 작업은
//          Crypto::CCryptoUtil 모듈을 통해 수행됩니다.
//***************************************************************************
constexpr size_t kTokenBytes = 32;

//***************************************************************************
// @brief 외부 노출용 계정 식별자(public_id) 바이트 크기 (128비트)
// @details DB 내부 전용 순번 PK(uid)와 분리된 무작위 식별자입니다.
//          가입자 수 추정이나 무작위 스캔 공격을 방지하며, 닉네임 변경 시에도
//          고정 값을 유지하여 Redis 및 로컬 토큰 식별 용도로 사용됩니다.
//***************************************************************************
constexpr size_t kPublicIdBytes = 16;

//***************************************************************************
// @brief 닉네임 필드 버퍼 바이트 크기
// @details UTF-8 기준 최대 16글자(한글 3바이트*16=48바이트)와 NUL 종단 문자를
//          수용할 수 있는 크기입니다. 실제 글자 수 검증은 비즈니스 로직에서
//          별도 수행합니다.
//***************************************************************************
constexpr size_t kNicknameBytes = 50;

//***************************************************************************
// @brief 프로필 이미지 URL 필드 버퍼 바이트 크기 (UTF-8, NUL 포함)
// @details 서버는 이미지 URL 문자열만 보관 및 전송하며, 클라이언트가 해당 URL을
//          통해 직접 이미지를 로드합니다.
//***************************************************************************
constexpr size_t kProfileImageUrlBytes = 256;

//***************************************************************************
// @brief [추가] 방 이름 필드 버퍼 바이트 크기 (UTF-8, NUL 포함).
// @details DB rooms.name이 VARCHAR(50)이므로, 한글 기준(3바이트/글자)
//          최대 길이를 넉넉히 커버하도록 여유 있게 잡았다.
//***************************************************************************
constexpr size_t kRoomNameBytes = 100;

//***************************************************************************
// @brief [추가 — 통합] DB 비동기 요청(ST_XXX_REQ::callIdent)을 식별하는
//        값들을 전부 여기 한곳에 모았다.
// @details 원래는 각 DB*Request.h 파일(DBFriendRequest.h 등)이 자기 값을
//          각자 선언했는데, 파일이 여러 개로 늘어나면서 같은 값을 실수로
//          두 번 쓰는 충돌이 실제로 발생했다(kDbCallIdent_CreateRoom을
//          처음엔 300으로 잡았다가 BYTE 범위 초과로 204로 낮췄는데,
//          그게 이미 kDbCallIdent_SelectProfileImage가 쓰고 있던 값과
//          겹쳤던 사고). 값을 전부 한곳에 모아두면 새 DB 요청을 추가할 때
//          "지금까지 쓴 값이 어디까지인지"를 이 블록 하나만 보고 바로
//          알 수 있어서 같은 실수를 구조적으로 막을 수 있다.
//
//          callIdent는 BYTE(0~255) 하나뿐이라 이 프로젝트 전체가 이
//          공간을 공유한다 — 새 값을 추가할 때는 반드시 이 블록의
//          마지막 값 다음 번호를 쓸 것.
//***************************************************************************
constexpr BYTE kDbCallIdent_Account = 200;				// DBAccountRequest.h — 회원가입/재접속 검증, 닉네임 변경(연산은 EAccountOp로 구분)
constexpr BYTE kDbCallIdent_ProfileImage = 202;	// DBProfileImageRequest.h — 프로필 이미지 등록/목록/대표 지정/삭제(연산은 EProfileImageOp로 구분)
constexpr BYTE kDbCallIdent_Room = 206;			// DBRoomRequest.h — 방 생성/삭제/이름 변경/이미지/방장 이양/입장 기록/방 정보·목록·멤버 목록 조회(연산은 ERoomOp로 구분)
constexpr BYTE kDbCallIdent_Friend = 216;				// DBFriendRequest.h — 친구 요청/수락/거절/삭제/목록/차단(연산은 EFriendOp로 구분)
constexpr BYTE kDbCallIdent_Conv = 217;				// DBConvRequest.h — 대화방(1:1/그룹) 생성/초대/나가기/목록/메시지 저장·조회/읽음(연산은 EConvOp로 구분)
// 다음 새 값은 217부터 시작할 것.

//***************************************************************************
// @brief 로비 및 룸 식별 상수
//***************************************************************************
constexpr int32_t kLobbyRoomId = 0;

//***************************************************************************
// @brief 방 목록 조회 범위.
// @details ListRoomsReqPacket::scope에 실려온다.
//          All    : 존재하는 모든 방(최신 생성순)
//          Joined : 요청자가 입장한 적이 있는 방(최근 입장순) — room_members 테이블 기준
//***************************************************************************
enum class ERoomListScope : uint8_t
{
	All = 0,
	Joined = 1,
};

//***************************************************************************
// @brief 방 멤버 목록 조회 범위.
// @details RoomMemberListReqPacket::scope에 실려온다.
//          Present : 지금 실제로 그 방에 접속해 있는 사람(인메모리 스냅샷,
//                     CChatServerMain::GetRoomMembers() — DB를 거치지 않고
//                     즉시 응답). 전원이 online=1로 나간다(정의상 지금
//                     연결돼 있어야만 이 목록에 들어오므로).
//          History : 그 방에 입장한 적이 있는 전체 유저(DB의 room_members
//                     기준, CChatServerMain::RequestRoomMemberHistoryPage()).
//                     online은 "지금 이 방에 있는지"가 아니라 "지금 서버
//                     어딘가에 로그인해 있는지"를 뜻한다.
//***************************************************************************
enum class ERoomMemberListScope : uint8_t
{
	Present = 0,
	History = 1,
};

//***************************************************************************
// @brief 방 목록 한 페이지의 최대 크기. 클라이언트가 더 큰 값을 요청해도
//        서버가 이 값으로 줄여서 처리한다(응답 패킷 폭주 방지).
//***************************************************************************
constexpr int32_t kMaxRoomPageSize = 50;

//***************************************************************************
// @brief 로그인 처리 결과 열거형
// @details LoginResPacket::reason 필드에 설정되어 처리 상태를 전달합니다.
//***************************************************************************
enum class ELoginResult : uint8_t
{
	Ok = 0,					// 성공
	NicknameTaken = 1,		// 닉네임 중복 (신규 가입 시)
	DbError = 2,			// DB 처리 오류 (재시도 가능)
	InvalidNickname = 3,	// 닉네임 형식 위반 (문자셋 및 길이 제한)
	AccountNotFound = 4,	// 계정 정보 없음 (재접속 시)
	TokenMismatch = 5,		// 재접속 토큰 불일치
};

//***************************************************************************
// @brief 방 입장/퇴장/생성/삭제/이름변경 처리 결과 열거형
// @details RoomEnterResPacket::reason 등의 필드에 설정됩니다.
// @details [추가] 방 생성/삭제/이름변경 기능 도입으로 사유가 늘었다.
//***************************************************************************
enum class ERoomResult : uint8_t
{
	Ok = 0,					// 성공
	InvalidRoomId = 1,		// 유효하지 않은 Room ID (존재하지 않는 방)
	RoomNotFound = 2,		// [추가] 존재하지 않는 방(삭제/이름변경 대상)
	NotOwner = 3,			// [추가] 요청자가 그 방의 방장이 아님(삭제/이름변경 시도)
	RoomLimitExceeded = 4,	// [추가] 1인당 생성 가능한 방 개수 상한 초과
	InvalidName = 5,		// [추가] 방 이름 형식 위반(빈 문자열/길이 초과 등)
	DbError = 6,			// [추가] DB 처리 오류
	TooFast = 7,			// 방 입장 요청이 너무 잦음
};

//***************************************************************************
// @brief [추가 — 이동] 채팅 메시지 삭제 결과 사유.
// @details [수정] DeleteChatMessageResPacket::reason 필드에 설정되는 값이라
//          원래는 CChatServerMain의 중첩 타입이었는데(서버 내부 클래스에
//          와이어 프로토콜 값이 갇혀있던 셈), ERoomResult/ELoginResult처럼
//          클라이언트·서버가 같이 합의해야 하는 "응답 사유" enum들과 성격이
//          같아서 이 파일로 옮겼다.
//***************************************************************************
enum class EDeleteMessageResult : uint8_t
{
	Ok = 0,
	NotFound = 1,	// messageId가 추적 창(최근 N개)을 벗어났거나 애초에 존재한 적 없음
	NotOwner = 2,	// 요청자가 이 메시지의 작성자가 아님
};

//***************************************************************************
// @brief 계정당 최대 친구 수. 친구 요청/수락 시 서버가 양쪽 모두 검사한다.
//***************************************************************************
constexpr int32_t kMaxFriends = 3000;

//***************************************************************************
// @brief 친구 목록 응답(FriendListBatchRes) 한 패킷에 담는 최대 항목 수.
//        패킷 한 개가 클라이언트가 읽는 한도(8192바이트) 안에 들어가도록 정한다.
//***************************************************************************
constexpr int32_t kMaxFriendsPerBatch = 20;

//***************************************************************************
// @brief 받은 친구 요청 목록 한 페이지의 최대 크기. 클라이언트가 더 큰 값을
//        요청해도 서버가 이 값으로 줄여서 처리한다.
//***************************************************************************
constexpr int32_t kMaxPendingPageSize = 50;

//***************************************************************************
// @brief 친구 기능(요청/수락/거절/삭제/목록/차단) 처리 결과 열거형
// @details 친구 기능 Res 패킷의 reason 필드에 설정됩니다.
//***************************************************************************
enum class EFriendResult : uint8_t
{
	Ok = 0,
	DbError = 1,			// DB 처리 오류 (재시도 가능)
	UserNotFound = 2,		// 대상 계정 없음
	SelfTarget = 3,			// 자기 자신을 대상으로 지정
	AlreadyFriends = 4,		// 이미 친구
	AlreadyRequested = 5,	// 이미 요청을 보낸 상태
	NoPendingRequest = 6,	// 수락/거절할 대기 요청이 없음
	NotFriends = 7,			// 친구가 아님 (삭제 대상 아님)
	BlockedByMe = 8,		// 내가 차단한 상대 (먼저 차단 해제 필요)
	FriendLimit = 9,		// 친구 수 상한 초과 (본인 또는 상대)
	AlreadyBlocked = 10,	// 이미 차단됨
	NotBlocked = 11,		// 차단 목록에 없음
};

// ---------------------------------------------------------------------------
// 대화방(Conversation) — 친구 기반 1:1/그룹 대화. 공개 방(rooms, 오픈채팅 성격)과는 별개이며
// 한 사용자가 여러 대화방에 동시에 속한다(서버는 접속 중인 모든 멤버에게 메시지를 푸시한다).
// ---------------------------------------------------------------------------
constexpr size_t  kConvNameBytes = 50;				// 그룹 대화방 이름 버퍼(NUL 포함). 비어 있으면 클라이언트가 멤버 이름으로 표시
constexpr size_t  kConvMessageBytes = 512;			// 대화 메시지 본문 버퍼(UTF-8, NUL 포함)
constexpr int32_t kMaxConvInvite = 20;				// 대화방 생성/초대 한 번에 지정할 수 있는 최대 인원
constexpr int32_t kMaxConvMembers = 100;			// 그룹 대화방 최대 인원
constexpr int32_t kMaxConvHistoryPage = 50;			// 대화 기록 한 번에 가져올 수 있는 최대 메시지 수
constexpr int32_t kMaxConvList = 300;				// 내 대화방 목록 최대 개수(초과분은 오래된 순으로 잘림)
constexpr size_t  kConvReplyPreviewBytes = 100;		// 답장 대상 원문 미리보기 버퍼(NUL 포함, 첫 줄만)

enum class EConvType : uint8_t
{
	Direct = 1,		// 1:1 (두 친구당 하나, 다시 열면 같은 방)
	Group = 2,		// 그룹 (이름 선택, 초대로 확장)
};

enum class EConvMessageKind : uint8_t
{
	Text = 0,		// 사용자가 보낸 메시지
	System = 1,		// 시스템 안내(초대/나가기). 발신자 없음
	Image = 2,		// 사진. 본문(message)은 우리 파일 서버의 이미지 URL
	Deleted = 3,	// 발신자가 삭제한 메시지(본문 비움). 마지막 메시지/답장 대상 표시에도 같은 값을 쓴다
};

enum class EConvResult : uint8_t
{
	Ok = 0,
	DbError = 1,			// DB 처리 오류 (재시도 가능)
	NotFriends = 2,			// 대상이 친구가 아님
	Blocked = 3,			// 차단 관계(내가 차단한 상대 포함)
	NotMember = 4,			// 이 대화방의 멤버가 아님(또는 존재하지 않음)
	NotGroup = 5,			// 그룹 전용 연산을 1:1에 시도
	InvalidTarget = 6,		// 대상 없음/자기 자신/중복/인원 수 위반
	MemberLimit = 7,		// 그룹 최대 인원 초과
	InvalidName = 8,		// 이름 길이 위반
	InvalidMessage = 9,		// 빈 메시지, 또는 우리 파일 서버 이미지가 아닌 사진 URL
	InvalidReply = 10,		// 답장 대상이 이 대화방의 보이는 일반 메시지가 아님(시스템/삭제됨/다른 방)
	NotFound = 11,			// 삭제할 메시지가 없음(이미 삭제됐거나 다른 방)
	NotSender = 12,			// 내가 보낸 메시지가 아니라 삭제할 수 없음
	TooFast = 13,			// 전송 속도 제한 초과
};

//***************************************************************************
// @brief 채팅 서버 패킷 타입 식별자 열거형.
// @details 각 패킷 헤더(PacketHeader::type)에 매핑되는 식별자 목록입니다.
//***************************************************************************
enum class EChatPacketType : uint16_t
{
	LoginReq = 1,							// Client -> Server, 로그인/재접속 요청
	LoginRes = 2,							// Server -> Client, 로그인/재접속 응답
	Chat = 3,								// Client <-> Server, 채팅 메시지 전송 및 브로드캐스트
	NicknameGenerateReq = 4,				// Client -> Server, 임시 무작위 닉네임 생성 요청
	NicknameGenerateRes = 5,				// Server -> Client, 생성된 임시 닉네임 응답
	ChangeNicknameReq = 6,					// Client -> Server, 닉네임 변경 요청
	ChangeNicknameRes = 7,					// Server -> Client, 닉네임 변경 결과 응답
	RoomEnterReq = 8,						// Client -> Server, 특정 방 입장 요청
	RoomEnterRes = 9,						// Server -> Client, 방 입장 결과 응답
	RoomLeaveReq = 10,						// Client -> Server, 현재 방 퇴장(로비 복귀) 요청
	RoomLeaveRes = 11,						// Server -> Client, 방 퇴장 결과 응답
	RoomUserCountNotify = 12,				// Server -> Client, 방 인원수 변동 시 서버가 자발적으로 보내는 알림
	ServerUserCountReq = 13,				// Client -> Server, 서버 전체 접속자 수(동접자수) 조회 요청 (주기적 폴링용)
	ServerUserCountRes = 14,				// Server -> Client, 서버 전체 접속자 수 조회 응답
	SetProfileImageUrlReq = 15,				// Client -> Server, 내 계정의 프로필 이미지 URL 설정/변경 요청
	SetProfileImageUrlRes = 16,				// Server -> Client, 프로필 이미지 URL 설정 응답
	RequestUploadTokenReq = 17,				// Client -> Server, 파일 서버 업로드용 임시 토큰 요청
	RequestUploadTokenRes = 18,				// Server -> Client, 업로드 임시 토큰 및 파일 서버 주소 응답
	ListProfileImagesReq = 19,				// Client -> Server, 내 프로필 이미지 갤러리 목록 조회 요청
	ListProfileImagesItemRes = 20,			// Server -> Client, 갤러리 목록 항목 단건 응답 (가변 개수 스트리밍)
	ListProfileImagesEndRes = 21,			// Server -> Client, 갤러리 목록 전송 완료 및 총 개수 통지
	SelectProfileImageReq = 22,				// Client -> Server, 갤러리 내 특정 이미지를 대표 프로필로 선택 요청
	SelectProfileImageRes = 23,				// Server -> Client, 대표 이미지 선택 결과 응답
	DeleteProfileImageReq = 24,				// Client -> Server, 갤러리 내 특정 이미지 삭제 요청
	DeleteProfileImageRes = 25,				// Server -> Client, 이미지 삭제 결과 응답
	DeleteChatMessageReq = 26,				// Client -> Server, 특정 채팅 메시지 삭제 요청
	DeleteChatMessageRes = 27,				// Server -> Client, 메시지 삭제 결과 응답
	DeleteChatMessageNotify = 28,			// Server -> Client(Broadcast), 메시지 삭제 실시간 통보

	// [추가] 방 생성/삭제/이름변경/목록조회 + 방장 자동 이양 알림.
	CreateRoomReq = 29,						// Client -> Server, 방 생성 요청(생성자가 방장이 됨)
	CreateRoomRes = 30,						// Server -> Client, 방 생성 결과 응답
	DeleteRoomReq = 31,						// Client -> Server, 방 삭제 요청(방장만 가능)
	DeleteRoomRes = 32,						// Server -> Client, 방 삭제 결과 응답(요청자에게만)
	DeleteRoomNotify = 33,					// Server -> Client(Broadcast), 방 삭제 시 그 방 멤버 전원에게
	RenameRoomReq = 34,						// Client -> Server, 방 이름 변경 요청(방장만 가능)
	RenameRoomRes = 35,						// Server -> Client, 방 이름 변경 결과 응답(요청자에게만)
	RenameRoomNotify = 36,					// Server -> Client(Broadcast), 방 이름 변경 시 그 방 멤버 전원에게
	ListRoomsReq = 37,						// Client -> Server, 방 목록 한 페이지 조회 요청(범위/페이지/검색어)
	ListRoomsItemRes = 38,					// Server -> Client, 방 목록 항목 단건 응답(가변 개수 스트리밍)
	ListRoomsEndRes = 39,					// Server -> Client, 방 목록 한 페이지 전송 완료(조건에 맞는 전체 개수/페이지 번호 통지)
	RoomOwnerChangedNotify = 40,			// Server -> Client(Broadcast), 방장이 나가서 다른 멤버에게 자동 이양됐을 때 그 방 멤버 전원에게

	// [추가] 방 프로필 이미지 설정/교체/해제(방장 전용) + 변경 알림.
	SetRoomImageReq = 41,					// Client -> Server, 방 프로필 이미지 설정/교체/해제 요청(방장만 가능, 빈 URL이면 해제)
	SetRoomImageRes = 42,					// Server -> Client, 설정 결과 응답(요청자에게만)
	RoomImageChangedNotify = 43,			// Server -> Client(Broadcast), 이미지가 바뀌었을 때 그 방 멤버 전원에게

	RoomMemberListReq = 47,					// Client -> Server, 지금 자신이 있는 방(로비 포함)의 멤버 목록 조회 요청
	RoomMemberListItemRes = 48,				// Server -> Client, 멤버 목록 항목 단건 응답(가변 개수 스트리밍)
	RoomMemberListEndRes = 49,				// Server -> Client, 멤버 목록 전송 완료 및 총 인원수 통지

	// [추가] 방 입장 시 서버가 자동으로 스트리밍해주는 과거 대화 기록.
	ChatHistoryItemRes = 44,				// Server -> Client, 기록 항목 단건(가변 개수 스트리밍) — 요청자에게만
	ChatHistoryEndRes = 45,					// Server -> Client, 기록 전송 완료 — 요청자에게만

	// [추가] 친구 요청/수락/거절/삭제/목록 조회/차단. (46은 비어 있음)
	// 대상 지정: 친구 요청·차단은 닉네임, 그 외는 목록/알림으로 받은 publicId.
	FriendRequestReq = 50,					// Client -> Server, 닉네임으로 친구 요청
	FriendRequestRes = 51,					// Server -> Client, 친구 요청 결과 응답
	FriendRespondReq = 52,					// Client -> Server, 받은 친구 요청 수락/거절
	FriendRespondRes = 53,					// Server -> Client, 수락/거절 결과 응답
	FriendRemoveReq = 54,					// Client -> Server, 친구 삭제 요청
	FriendRemoveRes = 55,					// Server -> Client, 친구 삭제 결과 응답
	FriendListReq = 56,						// Client -> Server, 친구 목록 조회 요청
	FriendListBatchRes = 57,				// Server -> Client, 친구 목록 항목 묶음 응답(한 패킷에 최대 kMaxFriendsPerBatch개, 가변 개수 스트리밍)
	FriendListEndRes = 58,					// Server -> Client, 친구 목록 전송 완료
	FriendPendingListReq = 59,				// Client -> Server, 받은 친구 요청 목록 한 페이지 조회 요청
	FriendPendingItemRes = 60,				// Server -> Client, 받은 요청 항목 단건 응답(가변 개수 스트리밍)
	FriendPendingEndRes = 61,				// Server -> Client, 받은 요청 한 페이지 전송 완료(다음 페이지 유무 통지)
	FriendRequestNotify = 62,				// Server -> Client, 친구 요청 도착 알림
	FriendAcceptedNotify = 63,				// Server -> Client, 내 요청이 수락됨(또는 요청이 맞물려 즉시 친구)
	FriendRemovedNotify = 64,				// Server -> Client, 친구 관계 해제 알림(상대의 삭제 또는 차단)
	FriendStatusNotify = 65,				// Server -> Client, 접속 중인 친구의 접속/종료 알림
	BlockReq = 66,							// Client -> Server, 닉네임으로 차단 요청
	BlockRes = 67,							// Server -> Client, 차단 결과 응답
	UnblockReq = 68,						// Client -> Server, 차단 해제 요청
	UnblockRes = 69,						// Server -> Client, 차단 해제 결과 응답
	BlockListReq = 70,						// Client -> Server, 차단 목록 조회 요청
	BlockListItemRes = 71,					// Server -> Client, 차단 목록 항목 단건 응답(가변 개수 스트리밍)
	BlockListEndRes = 72,					// Server -> Client, 차단 목록 전송 완료
	FriendProfileChangedNotify = 73,		// Server -> Client, 접속 중인 친구의 닉네임/프로필 이미지 변경 알림

	// [추가] 대화방(1:1/그룹) — 서버는 접속 중인 모든 멤버에게 푸시하고, 모든 메시지는 DB에 저장된다.
	ConvOpenDirectReq = 74,					// Client -> Server, 친구와의 1:1 대화방 열기(없으면 생성)
	ConvCreateGroupReq = 75,				// Client -> Server, 친구들로 그룹 대화방 만들기
	ConvOpenRes = 76,						// Server -> Client, 1:1 열기/그룹 생성 결과(대화방 요약 포함)
	ConvInviteReq = 77,						// Client -> Server, 그룹에 친구 초대
	ConvInviteRes = 78,						// Server -> Client, 초대 결과
	ConvLeaveReq = 79,						// Client -> Server, 대화방 나가기
	ConvLeaveRes = 80,						// Server -> Client, 나가기 결과
	ConvListReq = 81,						// Client -> Server, 내 대화방 목록 조회
	ConvListItemRes = 82,					// Server -> Client, 대화방 요약 단건(가변 개수 스트리밍)
	ConvListEndRes = 83,					// Server -> Client, 목록 전송 완료
	ConvSendReq = 84,						// Client -> Server, 대화방에 메시지 전송
	ConvSendRes = 85,						// Server -> Client, 전송 결과(발신자에게만)
	ConvMessageNotify = 86,					// Server -> Client, 새 메시지 푸시(발신자 본인 포함)
	ConvHistoryReq = 87,					// Client -> Server, 지난 메시지 조회(beforeMsgId 이전, 최신순 페이지)
	ConvHistoryItemRes = 88,				// Server -> Client, 기록 항목 단건(오래된 것부터)
	ConvHistoryEndRes = 89,					// Server -> Client, 기록 전송 완료(더 있는지 통지)
	ConvReadReq = 90,						// Client -> Server, 여기까지 읽음 표시
	ConvReadNotify = 91,					// Server -> Client, 다른 멤버가 읽음(안 읽은 사람 수 계산용)
	ConvMembersReq = 92,					// Client -> Server, 대화방 멤버 목록 조회
	ConvMemberItemRes = 93,					// Server -> Client, 멤버 단건
	ConvMembersEndRes = 94,					// Server -> Client, 멤버 목록 전송 완료
	ConvUpdatedNotify = 95,					// Server -> Client, 대화방 요약 갱신/신규 알림(초대됨, 인원 변경 등)
	ConvDeleteReq = 96,						// Client -> Server, 내가 보낸 메시지 삭제(모두에게서 삭제)
	ConvDeleteRes = 97,						// Server -> Client, 삭제 결과(요청자에게만)
	ConvDeletedNotify = 98,					// Server -> Client, 메시지가 삭제됨(요청자 포함 접속 중인 멤버 전원)
	ConvRenameReq = 99,						// Client -> Server, 그룹 대화방 이름 변경(멤버 누구나, 빈 이름이면 이름 해제)
	ConvRenameRes = 100,					// Server -> Client, 이름 변경 결과(요청자에게만)
	ConvRenamedNotify = 101,				// Server -> Client, 이름이 바뀜(요청자 포함 접속 중인 멤버 전원)
};

//***************************************************************************
// @brief 패킷 디스패처 처리 결과 열거형
// @details CPacketDispatcher 패킷 수신 처리의 상태 결과 값입니다.
//***************************************************************************
enum class EChatDispatchResult
{
	Handled,		// 정상 처리 완료
	UnknownType,	// 미등록 패킷 타입
	SizeViolation,	// 패킷 크기 검증 실패 (프로토콜 위반)
};

#endif // ndef UC_CHATPACKETTYPES_H