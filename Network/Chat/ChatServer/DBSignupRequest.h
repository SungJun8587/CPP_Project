
//***************************************************************************
// DBSignupRequest.h : 회원가입/재접속(닉네임+토큰) DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBSIGNUPREQUEST_H
#define UC_DBSIGNUPREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult, kPublicIdBytes

#include <functional>
#include <memory>
#include <string>
#include <array>

class CChatSession;

//***************************************************************************
// @brief 이 DB 비동기 시스템 안에서 회원가입/재접속 요청을 식별할 callIdent.
// @details st_DBAsyncRq::callIdent가 실제로는 BYTE(0~255)라, 프로젝트 전체
//          DB 요청 타입이 이 256개 슬롯을 공유합니다. 다른 시스템과 번호가
//          겹치지 않는지 실제 등록 전에 반드시 확인해주세요 — 여기서는
//          채팅 서버 전용으로 임의 배정했습니다.
//***************************************************************************
constexpr BYTE kDbCallIdent_Signup = 200;

//***************************************************************************
// @struct ST_SIGNUP_REQ
// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true) 요청.
// @details [설계 변경] nickname은 더 이상 계정 식별자가 아니다(users.uid가
// 내부 PK, users.public_id가 외부 식별자 — create_chat_db.sql 참고).
// - hasToken==false: nickname으로 신규 INSERT를 시도(새 public_id/token도
//   이 요청 처리 중 서버가 생성). 성공 시 새로 발급한 public_id/token을
//   onComplete로 돌려준다.
// - hasToken==true: publicId로 저장된 토큰 해시를 조회해 token과 비교.
//   일치하면 토큰을 회전(재발급)하고, 그 시점의 실제 닉네임(DB 기준 —
//   요청자가 기억하는 값과 다를 수 있음)과 새 토큰을 onComplete로 돌려준다.
//   publicId 자체는 안 바뀌므로 그대로 echo.
//
// onComplete는 이 프로젝트의 "공식" 응답 전달 경로(st_DBAsyncRp)를 못 찾아
// 요청 구조체에 직접 콜백을 담아 DB 워커 스레드에서 정확히 1회 호출하는
// 방식을 씁니다(ChatServer.cpp 주석 참고). sessionWeak는 콜백 시점에 세션이
// 이미 끊겼을 수 있으므로 weak_ptr로 보관 — 콜백을 호출하는 쪽(ChatServer)이
// 아니라 그 콜백을 소비하는 쪽(ChatLoginHandler)에서 lock()으로 재확인한다.
//***************************************************************************
struct ST_SIGNUP_REQ : public st_DBAsyncRq
{
	ST_SIGNUP_REQ()
	{
		callIdent = kDbCallIdent_Signup;
		bReTry = false;
	}

	char	nickname[kNicknameBytes] = {};			// hasToken==false일 때만 의미: 신규 가입 시 원하는 닉네임(UTF-8, NUL 종단 보장은 호출부 책임)
	bool	hasToken = false;
	BYTE	publicId[kPublicIdBytes] = {};			// hasToken==true일 때만 의미: 재접속 대상 계정의 안정 식별자
	BYTE	token[kTokenBytes] = {};				// hasToken==true일 때만 의미 있음 (원문 — DB엔 이 값의 해시만 비교/저장)

	std::function<void(
		ELoginResult result,
		const std::string& nickname,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& newToken)>	onComplete;
};

#endif // ndef UC_DBSIGNUPREQUEST_H