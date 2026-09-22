
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
// @details 원래는 각 DB*Request.h 파일(DBSignupRequest.h 등)이 자기 값을
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
constexpr BYTE kDbCallIdent_Signup = 200;				// DBSignupRequest.h — 회원가입/재접속 검증
constexpr BYTE kDbCallIdent_ChangeNickname = 201;		// DBChangeNicknameRequest.h — 닉네임 변경
constexpr BYTE kDbCallIdent_SetProfileImageUrl = 202;	// DBSetProfileImageUrlRequest.h — 프로필 이미지 URL 등록
constexpr BYTE kDbCallIdent_ListProfileImages = 203;	// DBListProfileImagesRequest.h — 프로필 이미지 갤러리 목록 조회
constexpr BYTE kDbCallIdent_SelectProfileImage = 204;	// DBSelectProfileImageRequest.h — 갤러리 이미지 대표 지정
constexpr BYTE kDbCallIdent_DeleteProfileImage = 205;	// DBDeleteProfileImageRequest.h — 갤러리 이미지 삭제
constexpr BYTE kDbCallIdent_CreateRoom = 206;			// DBCreateRoomRequest.h — 방 생성
constexpr BYTE kDbCallIdent_DeleteRoom = 207;			// DBDeleteRoomRequest.h — 방 삭제
constexpr BYTE kDbCallIdent_RenameRoom = 208;			// DBRenameRoomRequest.h — 방 이름 변경
constexpr BYTE kDbCallIdent_ListRooms = 209;			// DBListRoomsRequest.h — 방 목록 조회
constexpr BYTE kDbCallIdent_TransferRoomOwner = 210;	// DBTransferRoomOwnerRequest.h — 방장 자동 이양(서버 내부 전용)
constexpr BYTE kDbCallIdent_SetRoomImage = 211;		// DBSetRoomImageRequest.h — 방 프로필 이미지 설정/교체/해제
// 다음 새 값은 212부터 시작할 것.

//***************************************************************************
// @brief 로비 및 룸 식별 상수
// @details [수정] 방이 동적으로 생성/삭제되면서 kMaxRoomId(고정 상한)는
//          더 이상 "유효한 방 번호 범위"를 뜻하지 않는다 — 이제 방
//          존재 여부는 CChatServerMain::RoomExists()(DB rooms 테이블 기반
//          인메모리 레지스트리)로 판단한다. 이 상수 자체는 하위 호환을
//          위해 남겨뒀을 뿐 더 이상 RoomEnterHandler.cpp 등에서 참조하지
//          않는다.
//***************************************************************************
constexpr int32_t kLobbyRoomId = 0;
constexpr int32_t kMaxRoomId = 10;

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
	ListRoomsReq = 37,						// Client -> Server, 존재하는 모든 방 목록 조회 요청
	ListRoomsItemRes = 38,					// Server -> Client, 방 목록 항목 단건 응답(가변 개수 스트리밍)
	ListRoomsEndRes = 39,					// Server -> Client, 방 목록 전송 완료 및 총 개수 통지
	RoomOwnerChangedNotify = 40,			// Server -> Client(Broadcast), 방장이 나가서 다른 멤버에게 자동 이양됐을 때 그 방 멤버 전원에게

	// [추가] 방 프로필 이미지 설정/교체/해제(방장 전용) + 변경 알림.
	SetRoomImageReq = 41,					// Client -> Server, 방 프로필 이미지 설정/교체/해제 요청(방장만 가능, 빈 URL이면 해제)
	SetRoomImageRes = 42,					// Server -> Client, 설정 결과 응답(요청자에게만)
	RoomImageChangedNotify = 43,			// Server -> Client(Broadcast), 이미지가 바뀌었을 때 그 방 멤버 전원에게

	// [추가] 방 입장 시 서버가 자동으로 스트리밍해주는 과거 대화 기록.
	ChatHistoryItemRes = 44,				// Server -> Client, 기록 항목 단건(가변 개수 스트리밍) — 요청자에게만
	ChatHistoryEndRes = 45,					// Server -> Client, 기록 전송 완료 — 요청자에게만
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