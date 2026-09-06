
//***************************************************************************
// AccountDBHandler.cpp : DBASYNC_SIGNUP_REQ(회원가입/재접속 검증) 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <DB/OdbcAsyncSrv.h>
#include <Crypto/CryptoUtil.h>

#include "DBSignupRequest.h"
#include "ChatSession.h"

#include <cstring>
#include <cctype>
#include <array>

namespace
{
	//***************************************************************************
	// @brief 닉네임 형식을 검증합니다: 영문 대소문자/숫자/밑줄, 1~31자.
	// @details SQL 인젝션 방어의 1차 방어선이기도 하다. 실제 방어는 아래
	//          PrepareQuery+BindParamInput(파라미터 바인딩)이 담당한다 —
	//          이 화이트리스트는 형식이 이상한 닉네임을 조기에 걸러 DB
	//          워커 부하를 아끼기 위한 것.
	//***************************************************************************
	bool IsValidNickname(const char* nickname, size_t len)
	{
		if( len == 0 || len >= 32 )
			return false;

		for( size_t i = 0; i < len; ++i )
		{
			const unsigned char c = static_cast<unsigned char>(nickname[i]);
			if( !std::isalnum(c) && c != '_' )
				return false;
		}
		return true;
	}

	//***************************************************************************
	// @brief ASCII 전용 narrow -> TCHAR 변환.
	// @details nickname/16진 해시 문자열은 전부 ASCII로만 구성됨이 이미
	//          검증되어 있으므로, 코드포인트가 128 미만인 문자는 ANSI/
	//          UNICODE 빌드 어느 쪽이든 값 자체가 동일하다 — 일반적인
	//          사용자 텍스트라면 EncodingConvert.h의 AnsiToUnicode() 등
	//          정식 인코딩 변환 함수를 써야 하지만, 여기서는 해당하지 않는다.
	//***************************************************************************
	_tstring ToTStringAscii(const std::string& s)
	{
		return _tstring(s.begin(), s.end());
	}

	std::string FromTStringAscii(const TCHAR* s, size_t len)
	{
		// std::string(s, s+len) 이터레이터 범위 생성자를 그대로 쓰면
		// UNICODE 빌드에서 TCHAR(wchar_t) -> char 암묵적 축소 변환으로
		// C4244 경고가 난다(ASCII 전용이라 실제 데이터 손실은 없지만
		// 경고는 명시적 캐스팅으로 없애는 게 맞다).
		std::string result;
		result.reserve(len);
		for( size_t i = 0; i < len; ++i )
			result.push_back(static_cast<char>(s[i]));
		return result;
	}

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
// @details [설계 변경] DECLARE_DBASYNC_HANDLER 매크로 채택 — 이 매크로가
//          만드는 핸들러 클래스는 기본 생성자만 가지므로(생성자 파라미터로
//          풀을 주입받을 수 없음), 다른 DB 핸들러들과 동일하게 매 호출마다
//          COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool()로 풀을 새로
//          얻는다. 이 등록 자체는 정적 초기화 시점(main() 진입 전)에
//          COdbcAsyncSrv::Instance()->Regist()를 통해 이루어지는데, 그
//          시점엔 아직 StartService()로 풀이 만들어지기 전이므로 애초에
//          "생성자에서 풀을 미리 캡처해두는" 설계 자체가 위험했다 — 이
//          패턴을 따르면 그 문제가 구조적으로 사라진다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(COdbcAsyncSrv, kDbCallIdent_Signup)
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

	OdbcConnGuard guard(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
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

		_tstring nicknameT = ToTStringAscii(nickname);
		_tstring hashHexT = ToTStringAscii(hashHex);
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

		_tstring nicknameT = ToTStringAscii(nickname);
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

		const std::string storedHashHex = FromTStringAscii(hashBuf, ::_tcslen(hashBuf));
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

		_tstring rotatedHashHexT = ToTStringAscii(rotatedHashHex);
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