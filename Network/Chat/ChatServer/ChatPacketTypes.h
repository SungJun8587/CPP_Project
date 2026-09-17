
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
// @brief 로비 및 룸 식별 상수
// @details 로비(kLobbyRoomId=0)는 기본 공용 채팅 공간이며, 룸은 1~kMaxRoomId
//          범위의 고정 식별자를 사용합니다.
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
// @brief 방 입장 및 퇴장 처리 결과 열거형
// @details RoomEnterResPacket::reason 등의 필드에 설정됩니다.
//***************************************************************************
enum class ERoomResult : uint8_t
{
	Ok = 0,	// 성공
	InvalidRoomId = 1,	// 유효하지 않은 Room ID (범위 초과)
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