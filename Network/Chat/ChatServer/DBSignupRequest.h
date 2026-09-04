
//***************************************************************************
// DBSignupRequest.h : 회원가입/재접속(닉네임+토큰) DB 비동기 요청 구조체
//
//***************************************************************************

#ifndef UC_DBSIGNUPREQUEST_H
#define UC_DBSIGNUPREQUEST_H

#include <DB/DBAsyncSrv.h>
#include "ChatPacket.h"		// ELoginResult

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
// @details
// - hasToken==false: nickname으로 신규 INSERT를 시도. 성공 시 새로 발급한
//   토큰을 onComplete로 돌려준다.
// - hasToken==true: nickname으로 저장된 토큰 해시를 조회해 token과 비교.
//   일치하면 토큰을 회전(재발급)하고 새 토큰을 onComplete로 돌려준다.
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

	char	nickname[32] = {};						// NUL 종단 보장은 호출부(ChatLoginHandler) 책임
	bool	hasToken = false;
	BYTE	token[kTokenBytes] = {};	// hasToken==true일 때만 의미 있음 (원문 — DB엔 이 값의 해시만 비교/저장)

	std::function<void(
		ELoginResult result,
		const std::string& nickname,
		const std::array<BYTE, kTokenBytes>& newToken)>	onComplete;
};

#endif // ndef UC_DBSIGNUPREQUEST_H