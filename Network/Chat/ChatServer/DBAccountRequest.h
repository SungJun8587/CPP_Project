
//***************************************************************************
// DBAccountRequest.h : 계정(회원가입/재접속, 닉네임 변경) DB 비동기 요청 구조체 모음
//
//***************************************************************************

#ifndef DBACCOUNTREQUEST_H
#define DBACCOUNTREQUEST_H

#include <DB/DBAsyncSrv.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

class CChatSession;

//***************************************************************************
// @brief 계정 DB 연산 종류.
// @details 계정 기능의 모든 요청은 kDbCallIdent_Account(ChatPacketTypes.h) 하나와
//          핸들러 하나(AccountDBHandler.cpp)로 처리하고, 연산은 이 값으로 구분한다.
//          연산마다 입력과 결과 콜백의 모양이 달라서 요청 구조체는 연산별로 두고
//          (이 파일 아래), 모두 ST_ACCOUNT_REQ_BASE를 상속해 op를 실어 보낸다.
//***************************************************************************
enum class EAccountOp : uint8
{
	Signup,				// 회원가입 또는 재접속 검증 — ST_SIGNUP_REQ
	ChangeNickname,		// 닉네임 변경 — ST_CHANGE_NICKNAME_REQ
};

//***************************************************************************
// @struct ST_ACCOUNT_REQ_BASE
// @brief 계정 DB 요청의 공통 부분. 핸들러가 op를 보고 실제 구조체로 캐스팅한다.
//***************************************************************************
struct ST_ACCOUNT_REQ_BASE : public st_DBAsyncRq
{
	explicit ST_ACCOUNT_REQ_BASE(EAccountOp operation) : op(operation)
	{
		callIdent = kDbCallIdent_Account;
		bReTry = false;
	}

	const EAccountOp	op;
};

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
struct ST_SIGNUP_REQ : public ST_ACCOUNT_REQ_BASE
{
	ST_SIGNUP_REQ() : ST_ACCOUNT_REQ_BASE(EAccountOp::Signup) {}

	char	nickname[kNicknameBytes] = {};			// hasToken==false일 때만 의미: 신규 가입 시 원하는 닉네임(UTF-8, NUL 종단 보장은 호출부 책임)
	bool	hasToken = false;
	BYTE	publicId[kPublicIdBytes] = {};			// hasToken==true일 때만 의미: 재접속 대상 계정의 안정 식별자
	BYTE	token[kTokenBytes] = {};				// hasToken==true일 때만 의미 있음 (원문 — DB엔 이 값의 해시만 비교/저장)

	std::function<void(
		ELoginResult result,
		const std::string& nickname,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& newToken,
		const std::string& profileImageUrl)>	onComplete;
};

//***************************************************************************
// @struct ST_CHANGE_NICKNAME_REQ
// @brief 로그인된 계정(publicId로 식별)의 닉네임을 newNickname으로 바꾸는 요청.
// @details [설계 변경] nickname은 더 이상 users 테이블의 PRIMARY KEY가
// 아니다(create_chat_db.sql 참고 — uid가 내부 PK, public_id가 외부 식별자).
// 그래서 이 요청은 대상 행을 publicId(안 바뀌는 값)로 찾아 nickname
// 컬럼만 갱신한다 — 계정의 "식별자 자체"는 전혀 바뀌지 않으므로, 예전
// 설계에서 필요했던 세션 아이덴티티 재발급이나 Redis 키 RENAME이 더 이상
// 필요 없다. 호출부(ChatAccountHandler.cpp)는 표시용 닉네임만
// 갱신하면 된다(CChatSession::UpdateNickname()).
//
// onComplete는 ST_SIGNUP_REQ와 동일한 관례 — DB 워커 스레드에서 정확히
// 1회 호출된다.
//***************************************************************************
struct ST_CHANGE_NICKNAME_REQ : public ST_ACCOUNT_REQ_BASE
{
	ST_CHANGE_NICKNAME_REQ() : ST_ACCOUNT_REQ_BASE(EAccountOp::ChangeNickname) {}

	BYTE	publicId[kPublicIdBytes] = {};		// 대상 계정의 안정 식별자 — UPDATE의 WHERE 절에 사용
	char	newNickname[kNicknameBytes] = {};	// 새로 바꿀 닉네임(UTF-8)

	std::function<void(
		ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newNickname)>	onComplete;
};

#endif // ndef DBACCOUNTREQUEST_H