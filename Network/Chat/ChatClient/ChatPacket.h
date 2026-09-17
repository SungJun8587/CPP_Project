
//***************************************************************************
// ChatPacket.h : 채팅 서버 기본 패킷 프로토콜 정의
//
//***************************************************************************

#ifndef UC_CHATPACKET_H
#define UC_CHATPACKET_H

#include <Network/Packet.h>   
#include "ChatPacketTypes.h"

//***************************************************************************
// 프레이밍 규칙: 모든 패킷은 PacketHeader(Packet.h)로 시작하며, header.size는
// "헤더를 포함한" 패킷 전체 크기입니다. CChatSession::OnRecv()가 이 규칙으로
// 수신 버퍼를 순회하며 완전한 패킷 단위로 잘라 처리합니다.
//***************************************************************************
#pragma pack(push, 1)

//***************************************************************************
// @brief 로그인 요청 구조체 (Client -> Server).
// @details hasToken 상태에 따라 신규 가입 시도 또는 재접속 시도로 처리됩니다.
//***************************************************************************
struct LoginReqPacket : PacketHeader
{
	char	userId[kNicknameBytes];	    // hasToken==0일 때 신규 가입용 닉네임(UTF-8)
	BYTE	publicId[kPublicIdBytes];	// hasToken==1일 때 재접속 대상 계정의 식별자
	uint8	hasToken;		            // 1: publicId/token 유효(재접속), 0: 신규 가입
	BYTE	token[kTokenBytes];		    // 재접속 토큰 원문(256비트)
};

//***************************************************************************
// @brief 로그인 응답 구조체 (Server -> Client).
// @details 로그인 처리 결과 및 계정 정보를 반환합니다.
//***************************************************************************
struct LoginResPacket : PacketHeader
{
	uint8	success;	                            // 1: 성공, 0: 실패
	uint8	reason;		                            // ELoginResult (success==0일 때 의미)
	char	nickname[kNicknameBytes];	            // 이 계정의 닉네임(UTF-8)
	BYTE	publicId[kPublicIdBytes];	            // success==1일 때 유효한 계정 식별자
	BYTE	token[kTokenBytes];	                    // success==1일 때 발급되는 새 토큰
	char	profileImageUrl[kProfileImageUrlBytes];	// success==1일 때 유효한 프로필 이미지 URL(UTF-8)
};

//***************************************************************************
// @brief 채팅 메시지 구조체 (양방향).
// @details Client -> Server 발신 및 Server -> Client 브로드캐스트 공용 포맷입니다.
//***************************************************************************
struct ChatPacket : PacketHeader
{
	// [추가] 메시지 삭제 기능을 위한 고유 ID. Client -> Server 방향에서는
	// 클라이언트가 뭘 채워 보내든 서버가 무시하고(0으로 채워 보내는 게
	// 관례) 새로 발급한다(CChatServerMain::RegisterNewMessage() 참고).
	// Server -> Client 브로드캐스트에만 유효한 값이 채워진다 — 나중에
	// DeleteChatMessageReqPacket으로 이 값을 그대로 실어 보내면 삭제를
	// 요청할 수 있다.
	int64	messageId;
	char	nickname[kNicknameBytes];	            // 발신자 닉네임(서버 브로드캐스트 시 스냅샷)
	char	profileImageUrl[kProfileImageUrlBytes];	// 발신자의 프로필 이미지 URL(UTF-8)
	char	message[256];                           // 채팅 메시지 본문
};

//***************************************************************************
// @brief 랜덤 닉네임 생성 요청 구조체 (Client -> Server).
// @details 바디 없음 — 헤더만으로 완결되는 단순 요청입니다.
//***************************************************************************
struct NicknameGenerateReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 랜덤 닉네임 생성 응답 구조체 (Server -> Client).
// @details 생성된 임시 닉네임을 반환합니다.
//***************************************************************************
struct NicknameGenerateResPacket : PacketHeader
{
	char	nickname[32];   // 생성된 닉네임 문자열
};

//***************************************************************************
// @brief 닉네임 변경 요청 구조체 (Client -> Server).
// @details 로그인된 세션에서만 처리가 유효합니다.
//***************************************************************************
struct ChangeNicknameReqPacket : PacketHeader
{
	char	newNickname[kNicknameBytes];	// 변경할 새 닉네임(UTF-8)
};

//***************************************************************************
// @brief 닉네임 변경 응답 구조체 (Server -> Client).
// @details 닉네임 변경 요청에 대한 처리 결과입니다.
//***************************************************************************
struct ChangeNicknameResPacket : PacketHeader
{
	uint8	success;	// 1: 성공, 0: 실패
	uint8	reason;		// ELoginResult (Ok, NicknameTaken, InvalidNickname, DbError 등)
};

//***************************************************************************
// @brief 방 입장 요청 구조체 (Client -> Server).
// @details 지정된 ID의 채팅방 입장을 요청합니다.
//***************************************************************************
struct RoomEnterReqPacket : PacketHeader
{
	int32	roomId;	// 입장하려는 방 번호 (1~kMaxRoomId)
};

//***************************************************************************
// @brief 방 입장 응답 구조체 (Server -> Client).
// @details 방 입장 처리 결과 및 해당 방의 인원수를 반환합니다.
//***************************************************************************
struct RoomEnterResPacket : PacketHeader
{
	uint8	success;		// 1: 성공, 0: 실패
	uint8	reason;			// ERoomResult
	int32	roomId;			// 요청했던 roomId 그대로 반환
	int32	roomUserCount;	// 입장 후 해당 방의 인원수
};

//***************************************************************************
// @brief 방 퇴장 요청 구조체 (Client -> Server).
// @details 현재 입장한 방에서 나와 로비로 복귀를 요청합니다.
//***************************************************************************
struct RoomLeaveReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 방 퇴장 응답 구조체 (Server -> Client).
// @details 방 퇴장 처리 결과 및 이동 후 남아있는 인원수를 반환합니다.
//***************************************************************************
struct RoomLeaveResPacket : PacketHeader
{
	uint8	success;		// 1: 성공(로비 이동), 0: 실패
	int32	roomId;			// 퇴장한 방 번호
	int32	roomUserCount;	// 퇴장 후 해당 방에 남은 인원수
};

//***************************************************************************
// @brief 방 인원수 변경 알림 구조체 (Server -> Client).
// @details 서버가 방 내부 유저들에게 자발적으로 브로드캐스트합니다.
//***************************************************************************
struct RoomUserCountNotifyPacket : PacketHeader
{
	int32	roomId;     // 대상 방 번호
	int32	userCount;  // 변경된 방 인원수
};

//***************************************************************************
// @brief 서버 전체 접속자 수 조회 요청 구조체 (Client -> Server).
// @details 바디 없음 — 폴링 방식으로 동접자수를 확인할 때 사용합니다.
//***************************************************************************
struct ServerUserCountReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 서버 전체 접속자 수 조회 응답 구조체 (Server -> Client).
// @details 전체 접속자 수 및 로비에 위치한 인원수를 반환합니다.
//***************************************************************************
struct ServerUserCountResPacket : PacketHeader
{
	int32	userCount;	    // 서버 전체 접속자 수
	int32	lobbyUserCount;	// 로비 상주 인원수
};

//***************************************************************************
// @brief 외부 URL 프로필 이미지 등록 및 대표 지정 요청 구조체 (Client -> Server).
// @details 새로운 이미지 URL을 갤러리에 추가하고 즉시 대표로 지정합니다.
//***************************************************************************
struct SetProfileImageUrlReqPacket : PacketHeader
{
	char	url[kProfileImageUrlBytes]; // 등록할 이미지 URL
};

//***************************************************************************
// @brief 프로필 이미지 URL 설정 응답 구조체 (Server -> Client).
// @details 등록 처리 결과 및 생성된 imageId를 반환합니다.
//***************************************************************************
struct SetProfileImageUrlResPacket : PacketHeader
{
	uint8	success;    // 1: 성공, 0: 실패
	uint8	reason;	    // ELoginResult
	int64	imageId;    // 생성된 갤러리 이미지 ID
};

//***************************************************************************
// @brief 파일 서버 업로드용 임시 토큰 발급 요청 구조체 (Client -> Server).
// @details 이미지 바이트 전송 전 파일 서버 인증용 임시 토큰을 요청합니다.
//***************************************************************************
struct RequestUploadTokenReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 업로드 토큰 발급 응답 구조체 (Server -> Client).
// @details 발급된 임시 토큰과 업로드 대상 파일 서버 URL을 반환합니다.
//***************************************************************************
struct RequestUploadTokenResPacket : PacketHeader
{
	uint8	success;                                // 1: 성공, 0: 실패
	uint8	reason;                                 // ELoginResult
	char	uploadToken[64];                        // 발급된 16진수 임시 토큰
	char	fileServerUrl[kProfileImageUrlBytes];	// 파일 서버 접속 URL
};

//***************************************************************************
// @brief 프로필 이미지 갤러리 목록 조회 요청 구조체 (Client -> Server).
// @details 계정에 등록된 전체 이미지 갤러리 조회를 요청합니다.
//***************************************************************************
struct ListProfileImagesReqPacket : PacketHeader
{
};

//***************************************************************************
// @brief 갤러리 항목 단건 응답 구조체 (Server -> Client).
// @details 갤러리 목록 조회의 각 항목 정보 전송에 사용됩니다.
//***************************************************************************
struct ListProfileImagesItemResPacket : PacketHeader
{
	int64	imageId;                            // 이미지 ID
	char	imageRef[kProfileImageUrlBytes];    // 이미지 URL 참조
	uint8	isActive;                           // 1: 현재 대표(활성) 이미지, 0: 비활성
};

//***************************************************************************
// @brief 갤러리 목록 전송 완료 응답 구조체 (Server -> Client).
// @details 갤러리 목록 단건 응답 전송이 모두 완료되었음을 알립니다.
//***************************************************************************
struct ListProfileImagesEndResPacket : PacketHeader
{
	int32	totalCount; // 전송된 총 이미지 항목 수
};

//***************************************************************************
// @brief 갤러리 이미지 선택 요청 구조체 (Client -> Server).
// @details 기존 갤러리 항목 중 하나를 대표 프로필 이미지로 선택합니다.
//***************************************************************************
struct SelectProfileImageReqPacket : PacketHeader
{
	int64	imageId;    // 선택할 이미지 ID
};

//***************************************************************************
// @brief 갤러리 이미지 선택 응답 구조체 (Server -> Client).
// @details 선택 요청 처리 결과를 반환합니다.
//***************************************************************************
struct SelectProfileImageResPacket : PacketHeader
{
	uint8	success;    // 1: 성공, 0: 실패
	uint8	reason;     // ELoginResult
};

//***************************************************************************
// @brief 갤러리 이미지 삭제 요청 구조체 (Client -> Server).
// @details 갤러리 내 특정 이미지를 삭제 요청합니다.
//***************************************************************************
struct DeleteProfileImageReqPacket : PacketHeader
{
	int64	imageId;    // 삭제할 이미지 ID
};

//***************************************************************************
// @brief 갤러리 이미지 삭제 응답 구조체 (Server -> Client).
// @details 삭제 요청 처리 결과를 반환합니다.
//***************************************************************************
struct DeleteProfileImageResPacket : PacketHeader
{
	uint8	success;    // 1: 성공, 0: 실패
	uint8	reason;     // ELoginResult
};

//***************************************************************************
// @brief [추가] 채팅 메시지 삭제 요청 구조체 (Client -> Server).
// @details messageId로 대상 메시지를 지정한다. 서버가 소유권(요청자가
//          실제 작성자인지)을 확인한 뒤 처리한다 —
//          CChatServerMain::TryDeleteMessage() 참고.
//***************************************************************************
struct DeleteChatMessageReqPacket : PacketHeader
{
	int64	messageId;
};

//***************************************************************************
// @brief [추가] 채팅 메시지 삭제 응답 구조체 (Server -> Client, 요청자에게만).
// @details reason은 CChatServerMain::EDeleteMessageResult 값이다.
//***************************************************************************
struct DeleteChatMessageResPacket : PacketHeader
{
	uint8	success;	// 1: 성공, 0: 실패
	uint8	reason;		// CChatServerMain::EDeleteMessageResult
};

//***************************************************************************
// @brief [추가] 채팅 메시지 삭제 알림 (Server -> Client, 서버가 자발적으로 브로드캐스트).
// @details 삭제가 성공했을 때만 보내지며, 그 메시지가 원래 브로드캐스트됐던
//          방(로비 포함)의 멤버 전원(삭제를 요청한 사람 포함)에게 간다 —
//          받는 쪽은 화면에서 messageId가 일치하는 항목을 찾아 제거하면 된다.
//***************************************************************************
struct DeleteChatMessageNotifyPacket : PacketHeader
{
	int64	messageId;
};

#pragma pack(pop)

#endif // ndef UC_CHATPACKET_H