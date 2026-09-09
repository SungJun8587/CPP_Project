
//***************************************************************************
// DBChangeNicknameRequest.h : 닉네임 변경 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBCHANGENICKNAMEREQUEST_H
#define UC_DBCHANGENICKNAMEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kNicknameBytes, kPublicIdBytes

#include <functional>
#include <string>
#include <array>

//***************************************************************************
// @brief 이 DB 비동기 시스템 안에서 닉네임 변경 요청을 식별할 callIdent.
// @details kDbCallIdent_Signup(200)과 겹치지 않게 배정 — DBSignupRequest.h
//          참고(callIdent는 BYTE라 프로젝트 전체가 0~255 슬롯을 공유함).
//***************************************************************************
constexpr BYTE kDbCallIdent_ChangeNickname = 201;

//***************************************************************************
// @struct ST_CHANGE_NICKNAME_REQ
// @brief 로그인된 계정(publicId로 식별)의 닉네임을 newNickname으로 바꾸는 요청.
// @details [설계 변경] nickname은 더 이상 users 테이블의 PRIMARY KEY가
// 아니다(create_chat_db.sql 참고 — uid가 내부 PK, public_id가 외부 식별자).
// 그래서 이 요청은 대상 행을 publicId(안 바뀌는 값)로 찾아 nickname
// 컬럼만 갱신한다 — 계정의 "식별자 자체"는 전혀 바뀌지 않으므로, 예전
// 설계에서 필요했던 세션 아이덴티티 재발급이나 Redis 키 RENAME이 더 이상
// 필요 없다. 호출부(ChatChangeNicknameHandler.cpp)는 표시용 닉네임만
// 갱신하면 된다(CChatSession::UpdateNickname()).
//
// onComplete는 ST_SIGNUP_REQ와 동일한 관례 — DB 워커 스레드에서 정확히
// 1회 호출된다.
//***************************************************************************
struct ST_CHANGE_NICKNAME_REQ : public st_DBAsyncRq
{
	ST_CHANGE_NICKNAME_REQ()
	{
		callIdent = kDbCallIdent_ChangeNickname;
		bReTry = false;
	}

	BYTE	publicId[kPublicIdBytes] = {};		// 대상 계정의 안정 식별자 — UPDATE의 WHERE 절에 사용
	char	newNickname[kNicknameBytes] = {};	// 새로 바꿀 닉네임(UTF-8)

	std::function<void(
		ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newNickname)>	onComplete;
};

#endif // ndef UC_DBCHANGENICKNAMEREQUEST_H