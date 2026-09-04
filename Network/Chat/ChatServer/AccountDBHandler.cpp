
//***************************************************************************
// AccountDBHandler.cpp: implementation of the CAccountDBHandler class.
//
//***************************************************************************

#include "pch.h"
#include "AccountDBHandler.h"

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
		// [수정] std::string(s, s+len)의 이터레이터 범위 생성자를 그대로
		// 쓰면 UNICODE 빌드에서 TCHAR(wchar_t) -> char 암묵적 축소 변환이
		// 발생해 C4244 경고가 난다(ASCII 전용이라 실제 데이터 손실은
		// 없지만, 경고는 명시적 캐스팅으로 없애는 게 맞다).
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
//***************************************************************************
EDBReturnType CAccountDBHandler::ProcessAsyncCall(st_DBAsyncRq* pStAsync)
{
	// [가정] callIdent로 이미 라우팅된 뒤이므로 static_cast로 안전하다는
	// 전제 — RTTI 기반 관례라면 dynamic_cast + 널 체크로 바꿔야 한다.
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

	if( _pool == nullptr )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyToken);
		return EDBReturnType::OK;
	}

	OdbcConnGuard guard(_pool);
	if( guard == nullptr )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyToken);
		return EDBReturnType::OK;
	}

	// 스키마 가정:
	//   CREATE TABLE users (
	//     nickname   VARCHAR(31)  NOT NULL PRIMARY KEY,
	//     token_hash CHAR(64)     NOT NULL,   -- SHA-256 hex (Crypto::CCryptoUtil::HashSHA256 출력 그대로)
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
			return EDBReturnType::OK;
		}

		const std::string hashHex = HashTokenHex(newToken.data(), newToken.size());

		if( !guard->PrepareQuery(_T("INSERT INTO users (nickname, token_hash, created_at, updated_at) VALUES (?, ?, NOW(), NOW())")) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		_tstring nicknameT = ToTStringAscii(nickname);
		_tstring hashHexT = ToTStringAscii(hashHex);
		SQLLEN nicknameLenInd = SQL_NTS;
		SQLLEN hashHexLenInd = SQL_NTS;

		guard->BindParamInput(1, nicknameT.c_str(), nicknameLenInd);
		guard->BindParamInput(2, hashHexT.c_str(), hashHexLenInd);

		if( guard->Execute() )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, nickname, newToken);
			return EDBReturnType::OK;
		}

		// INSERT 실패 — 원인이 "중복 키"인지 그 외 오류인지 구분해야 한다.
		// CDBError는 SQLHENV/statement 핸들이 필요한데 CBaseODBC(BaseODBC.h)가
		// 그 핸들이나 최종 에러 정보를 외부에 노출하는 public 메서드가 없어
		// 여기서 직접 호출할 수 없다. 대신 핸들 접근이 필요 없는 방법으로
		// 판별한다: 실패가 이미 확정된 뒤 "이 닉네임이 지금 존재하는가"만
		// 다시 조회한다. 이 조회는 진단 목적일 뿐 정확성에 관여하지 않는다
		// (애초에 피하려던 "SELECT 먼저" 레이스는 삽입 시도 자체를 SELECT로
		// 대체하는 것이었지, 실패가 이미 확정된 뒤의 사후 확인이 아니다).
		guard->ClearStmt();
		if( guard->PrepareQuery(_T("SELECT 1 FROM users WHERE nickname = ?")) )
		{
			SQLLEN checkLenInd = SQL_NTS;
			guard->BindParamInput(1, nicknameT.c_str(), checkLenInd);

			if( guard->Execute() && guard->Fetch() )
			{
				// 지금 존재함 — 이번 INSERT 실패는 중복 키였다고 결론.
				// (그 사이 다른 요청이 성공적으로 먼저 넣은 경우도 결과적으로
				// "이 닉네임은 이제 못 씀"이라는 점에서 사용자에게 보여줄
				// 결과는 동일하므로 구분할 필요가 없다.)
				if( req->onComplete )
					req->onComplete(ELoginResult::NicknameTaken, nickname, emptyToken);
				return EDBReturnType::OK;
			}
		}

		// 존재하지 않는데도 INSERT가 실패 — 중복 키가 아닌 다른 오류(연결
		// 끊김, 문법 오류, 제약조건 위반 등).
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyToken);
		return EDBReturnType::OK;
	}
	else
	{
		// ── 재접속 검증 ────────────────────────────────────────────
		if( !guard->PrepareQuery(_T("SELECT token_hash FROM users WHERE nickname = ?")) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		_tstring nicknameT = ToTStringAscii(nickname);
		SQLLEN nicknameLenInd = SQL_NTS;
		guard->BindParamInput(1, nicknameT.c_str(), nicknameLenInd);

		if( !guard->Execute() )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		if( !guard->Fetch() )
		{
			// 결과 없음 — 해당 닉네임 계정이 없다.
			if( req->onComplete )
				req->onComplete(ELoginResult::AccountNotFound, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		TCHAR hashBuf[65] = {};
		int32 hashBufLen = static_cast<int32>(std::size(hashBuf));
		if( !guard->GetData(1, hashBuf, hashBufLen) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		const std::string storedHashHex = FromTStringAscii(hashBuf, ::_tcslen(hashBuf));
		const std::string providedHashHex = HashTokenHex(req->token, sizeof(req->token));

		// 상수시간 비교 — 두 해시 문자열 길이(항상 64)는 알려져 있으므로
		// 미리 길이만 확인하고 내용 비교는 ConstantTimeEquals로.
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
			return EDBReturnType::OK;
		}

		const std::string rotatedHashHex = HashTokenHex(rotatedToken.data(), rotatedToken.size());

		guard->ClearStmt();
		if( !guard->PrepareQuery(_T("UPDATE users SET token_hash = ?, updated_at = NOW() WHERE nickname = ?")) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		_tstring rotatedHashHexT = ToTStringAscii(rotatedHashHex);
		SQLLEN rotatedHashLenInd = SQL_NTS;
		guard->BindParamInput(1, rotatedHashHexT.c_str(), rotatedHashLenInd);
		guard->BindParamInput(2, nicknameT.c_str(), nicknameLenInd);

		if( !guard->Execute() )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyToken);
			return EDBReturnType::OK;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, nickname, rotatedToken);
		return EDBReturnType::OK;
	}
}