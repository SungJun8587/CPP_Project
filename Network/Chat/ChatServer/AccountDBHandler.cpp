
//***************************************************************************
// AccountDBHandler.cpp : DBASYNC_SIGNUP_REQ(회원가입/재접속 검증) 핸들러
//
//***************************************************************************

#include "pch.h"
#include <Util/EncodingConvert.h>
#include <Crypto/CryptoUtil.h>
#include <DB/DBAsyncHandler.h>

#include "DbServiceManager.h"
#include "DBSignupRequest.h"
#include "ChatSession.h"
#include "NicknameValidation.h"

#include <cstring>
#include <cctype>
#include <cstdint>
#include <array>

namespace
{
	using NicknameValidation::IsValidNickname;

	// [수정] IsAllowedCodepoint()/IsValidNickname() 직접 구현을 제거했다 —
	// ChangeNicknameDBHandler.cpp도 정확히 같은 규칙이 필요해서
	// NicknameValidation.h 공용 헤더로 뺐다.

	// [수정] Utf8ToTString()/TStringToUtf8() 직접 구현을 제거했다 — 이름까지
	// 똑같은 정식 함수가 이미 <EncodingConvert.h>에 있다(내부적으로
	// CIconvUtil 또는 Windows API로 실제 UTF-8<->UTF-16 변환을 함).
	// IsValidNickname()의 UTF-8 구조 검증(오버롱/서로게이트 등)은 이
	// 헤더에 없는 기능이라 여기 그대로 남겨둔다.

	//***************************************************************************
	// @brief 재접속 토큰(원문 32바이트)의 SHA-256 해시를 16진 문자열로 계산합니다.
	// @details Crypto::CCryptoUtil::HashSHA256()이 이미 16진 문자열을 돌려주므로
	//          별도 ToHex 변환이 필요 없다 — std::string은 바이너리 세이프하므로
	//          원문 바이트를 그대로 문자열에 담아 넘긴다.
	//***************************************************************************
	std::string HashTokenHex(const BYTE* token, size_t len)
	{
		return Crypto::CCryptoUtil::HashSHA256(std::string(reinterpret_cast<const char*>(token), len));
	}
}

//***************************************************************************
// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true)을 처리합니다.
// @details [설계 변경] COdbcAsyncSrv 자신의 Instance()가 없어져서(도메인별
//          다중 인스턴스를 지원하도록 CDbServiceManager로 소유권이 옮겨감),
//          DECLARE_DBASYNC_HANDLER_VIA(command, instanceExpr) 매크로로
//          "이 요청은 CDbServiceManager::Instance().Member() 인스턴스에
//          등록된다"는 걸 명시한다. 매크로가 만드는 핸들러 클래스는
//          기본 생성자만 가지므로(생성자로 풀을 주입받을 수 없음), 매
//          호출마다 CDbServiceManager::Instance().Member().GetOdbcConnPool()로
//          풀을 새로 얻는다. 이 등록 자체는 정적 초기화 시점(main() 진입
//          전)에 일어나는데, CDbServiceManager::Instance()는 함수 지역
//          static(최초 사용 시점 생성)이라 다른 정적 초기화식과의 순서
//          경쟁에서 자유롭다 — 이 시점엔 아직 StartService()로 실제 DB
//          풀이 만들어지기 전이지만, "생성자에서 풀을 미리 캡처"하는 게
//          아니라 매 호출마다 다시 조회하므로 문제되지 않는다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Signup)
{
	ST_SIGNUP_REQ* req = static_cast<ST_SIGNUP_REQ*>(pStAsync);

	const std::array<BYTE, kTokenBytes> emptyToken{};

	const size_t nicknameLen = ::strnlen(req->nickname, sizeof(req->nickname));
	if( !IsValidNickname(req->nickname, nicknameLen) )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::InvalidNickname, std::string(req->nickname, nicknameLen), emptyToken);
		return EDBReturnType::OK;
	}

	const std::string nickname(req->nickname, nicknameLen);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_Signup: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyToken);
		return EDBReturnType::INVALID;
	}

	// 스키마: create_chat_db.sql 참고
	//   CREATE TABLE users (
	//     nickname   VARCHAR(31)  NOT NULL PRIMARY KEY,
	//     token_hash CHAR(64)     NOT NULL,   -- SHA-256 hex
	//     created_at DATETIME     NOT NULL,
	//     updated_at DATETIME     NOT NULL
	//   );

	if( !req->hasToken )
	{
		// ── 신규 가입 ──────────────────────────────────────────────
		std::array<BYTE, kTokenBytes> newToken{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(newToken.data(), newToken.size()) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		const std::string hashHex = HashTokenHex(newToken.data(), newToken.size());

		if( !guard->PrepareQuery(_T("INSERT INTO users (nickname, token_hash, created_at, updated_at) VALUES (?, ?, NOW(), NOW())")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		_tstring nicknameT = Utf8ToTString(nickname);
		_tstring hashHexT = Utf8ToTString(hashHex);
		SQLLEN nicknameLenInd = SQL_NTS;
		SQLLEN hashHexLenInd = SQL_NTS;

		guard->BindParamInput(1, nicknameT.c_str(), nicknameLenInd);
		guard->BindParamInput(2, hashHexT.c_str(), hashHexLenInd);

		if( guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, nickname, newToken);
			return EDBReturnType::OK;
		}

		// INSERT 실패 — 원인이 "중복 키"인지 그 외 오류인지 구분해야 한다.
		// CDBError는 SQLHENV/statement 핸들이 필요한데 CBaseODBC(BaseODBC.h)가
		// 그 핸들이나 최종 에러 정보를 외부에 노출하는 public 메서드가 없어
		// 여기서 직접 호출할 수 없다. 대신 핸들 접근이 필요 없는 방법으로
		// 판별한다: 실패가 이미 확정된 뒤 "이 닉네임이 지금 존재하는가"만
		// 다시 조회한다. 이 조회는 진단 목적일 뿐 정확성에 관여하지 않는다.
		guard->ClearStmt();
		if( guard->PrepareQuery(_T("SELECT 1 FROM users WHERE nickname = ?")) )
		{
			SQLLEN checkLenInd = SQL_NTS;
			guard->BindParamInput(1, nicknameT.c_str(), checkLenInd);

			if( guard->Execute() && guard->Fetch() )
			{
				guard->ClearStmt();
				if( req->onComplete )
					req->onComplete(ELoginResult::NicknameTaken, nickname, emptyToken);
				return EDBReturnType::OK;
			}
			guard->ClearStmt();
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyToken);
		return EDBReturnType::INVALID;
	}
	else
	{
		// ── 재접속 검증 ────────────────────────────────────────────
		if( !guard->PrepareQuery(_T("SELECT token_hash FROM users WHERE nickname = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		_tstring nicknameT = Utf8ToTString(nickname);
		SQLLEN nicknameLenInd = SQL_NTS;
		guard->BindParamInput(1, nicknameT.c_str(), nicknameLenInd);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		if( !guard->Fetch() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::AccountNotFound, nickname, emptyToken);
			return EDBReturnType::FETCH_NOT_FIND;
		}

		TCHAR hashBuf[65] = {};
		int32 hashBufLen = static_cast<int32>(std::size(hashBuf));
		if( !guard->GetData(1, hashBuf, hashBufLen) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();

		const std::string storedHashHex = TStringToUtf8(_tstring(hashBuf));
		const std::string providedHashHex = HashTokenHex(req->token, sizeof(req->token));

		const bool tokenMatches =
			storedHashHex.size() == providedHashHex.size() &&
			Crypto::CCryptoUtil::ConstantTimeEquals(
				reinterpret_cast<const unsigned char*>(storedHashHex.data()),
				reinterpret_cast<const unsigned char*>(providedHashHex.data()),
				storedHashHex.size());

		if( !tokenMatches )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::TokenMismatch, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		// 토큰 일치 — 회전(새 토큰 발급) 후 갱신.
		std::array<BYTE, kTokenBytes> rotatedToken{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(rotatedToken.data(), rotatedToken.size()) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		const std::string rotatedHashHex = HashTokenHex(rotatedToken.data(), rotatedToken.size());

		if( !guard->PrepareQuery(_T("UPDATE users SET token_hash = ?, updated_at = NOW() WHERE nickname = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}

		_tstring rotatedHashHexT = Utf8ToTString(rotatedHashHex);
		SQLLEN rotatedHashLenInd = SQL_NTS;
		guard->BindParamInput(1, rotatedHashHexT.c_str(), rotatedHashLenInd);
		guard->BindParamInput(2, nicknameT.c_str(), nicknameLenInd);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, nickname, rotatedToken);
		return EDBReturnType::OK;
	}
}