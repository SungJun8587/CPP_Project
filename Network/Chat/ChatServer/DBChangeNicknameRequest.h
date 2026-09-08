
//***************************************************************************
// DBChangeNicknameRequest.h : 닉네임 변경 DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBCHANGENICKNAMEREQUEST_H
#define UC_DBCHANGENICKNAMEREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kNicknameBytes

#include <functional>
#include <string>

//***************************************************************************
// @brief 이 DB 비동기 시스템 안에서 닉네임 변경 요청을 식별할 callIdent.
// @details kDbCallIdent_Signup(200)과 겹치지 않게 배정 — DBSignupRequest.h
//          참고(callIdent는 BYTE라 프로젝트 전체가 0~255 슬롯을 공유함).
//***************************************************************************
constexpr BYTE kDbCallIdent_ChangeNickname = 201;

//***************************************************************************
// @struct ST_CHANGE_NICKNAME_REQ
// @brief 로그인된 계정의 닉네임을 newNickname으로 바꾸는 요청.
// @details nickname이 users 테이블의 PRIMARY KEY이므로, 이 요청이 성공하면
//          그 계정의 "식별자 자체"가 바뀐다 — DB UPDATE 하나로 끝나지
//          않고, 호출부(ChatChangeNicknameHandler.cpp)가 세션의 아이덴티티
//          (CChatSession::UpdateNickname())와 Redis 온라인 상태 키
//          (CChatServerMain::OnUserNicknameChanged())까지 같이 갱신해야
//          완결된다.
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

	char	oldNickname[kNicknameBytes] = {};	// 현재(변경 전) 닉네임 — UPDATE의 WHERE 절에 사용
	char	newNickname[kNicknameBytes] = {};	// 새로 바꿀 닉네임(UTF-8)

	std::function<void(
		ELoginResult result,
		const std::string& oldNickname,
		const std::string& newNickname)>	onComplete;
};

#endif // ndef UC_DBCHANGENICKNAMEREQUEST_H