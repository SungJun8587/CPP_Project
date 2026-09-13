
//***************************************************************************
// AccountDBHandler.cpp : DBASYNC_SIGNUP_REQ(회원가입/재접속 검증) 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

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

	//***************************************************************************
	// @brief public_id(원문 16바이트)를 16진 소문자 문자열로 변환합니다.
	// @details [설계 변경] public_id를 DB에 BINARY(16)이 아니라 CHAR(32)(16진
	//          문자열)로 저장하기로 했다 — token_hash와 동일한 이유(ODBC
	//          바이너리 파라미터 바인딩이 드라이버에 따라 불확실할 수 있어,
	//          이미 검증된 문자열 바인딩 경로로 통일). 와이어 프로토콜
	//          (ChatPacket.h)이나 세션/클라이언트 로컬 파일은 여전히 raw
	//          16바이트를 쓴다 — 이 변환은 DB 파라미터 바인딩 시점에만 필요.
	//***************************************************************************
	std::string PublicIdToHex(const BYTE* publicId, size_t len)
	{
		return Crypto::CCryptoUtil::ToHex(publicId, len);
	}
}

//***************************************************************************
// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true)을 처리합니다.
// @details [설계 변경] COdbcAsyncSrv 자신의 Instance()가 없어져서(도메인별
//          다중 인스턴스를 지원하도록 CDbServiceManager로 소유권이 옮겨감),
//          DECLARE_DBASYNC_HANDLER_VIA(command, instanceExpr) 매크로로
//          "이 요청은 MEMBER_DB_ASYNC 인스턴스에
//          등록된다"는 걸 명시한다. 매크로가 만드는 핸들러 클래스는
//          기본 생성자만 가지므로(생성자로 풀을 주입받을 수 없음), 매
//          호출마다 MEMBER_DB_ASYNC.GetOdbcConnPool()로
//          풀을 새로 얻는다. 이 등록 자체는 정적 초기화 시점(main() 진입
//          전)에 일어나는데, CDbServiceManager::Instance()는 함수 지역
//          static(최초 사용 시점 생성)이라 다른 정적 초기화식과의 순서
//          경쟁에서 자유롭다 — 이 시점엔 아직 StartService()로 실제 DB
//          풀이 만들어지기 전이지만, "생성자에서 풀을 미리 캡처"하는 게
//          아니라 매 호출마다 다시 조회하므로 문제되지 않는다.
//
// [설계 변경 — uid/public_id 분리] nickname은 더 이상 PRIMARY KEY가
// 아니다(create_chat_db.sql 참고: uid가 내부 전용 순번 PK, public_id가
// 외부 노출용 안정 식별자). 그래서:
//   - 가입 시 public_id를 서버가 새로 생성해 함께 INSERT한다.
//   - 재접속은 nickname이 아니라 publicId로 행을 찾는다 — 클라이언트가
//     보내는 nickname은 신규 가입 의도로만 쓰이고, 재접속 경로에서는
//     아예 읽지 않는다(그 시점의 진짜 닉네임은 DB 값이 기준).
//   - INSERT 중복은 이제 nickname의 UNIQUE 제약(PK 아님)에서 나므로,
//     원인 판별용 사후 SELECT 로직은 그대로 유효하다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Signup)
{
	ST_SIGNUP_REQ* req = static_cast<ST_SIGNUP_REQ*>(pStAsync);

	const std::array<BYTE, kTokenBytes> emptyToken{};
	const std::array<BYTE, kPublicIdBytes> emptyPublicId{};

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_Signup: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::string(), emptyPublicId, emptyToken);
		return EDBReturnType::INVALID;
	}

	// 스키마: create_chat_db.sql 참고
	//   CREATE TABLE users (
	//     uid        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
	//     public_id  CHAR(32)     NOT NULL UNIQUE,   -- 16바이트 난수의 16진 인코딩
	//     nickname   VARCHAR(16)  NOT NULL UNIQUE,
	//     token_hash CHAR(64)     NOT NULL,   -- SHA-256 hex
	//     created_at DATETIME     NOT NULL,
	//     updated_at DATETIME     NOT NULL
	//   );

	if( !req->hasToken )
	{
		// ── 신규 가입 ──────────────────────────────────────────────
		const size_t nicknameLen = ::strnlen(req->nickname, sizeof(req->nickname));
		if( !IsValidNickname(req->nickname, nicknameLen) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::InvalidNickname, std::string(req->nickname, nicknameLen), emptyPublicId, emptyToken);
			return EDBReturnType::OK;
		}

		const std::string nickname(req->nickname, nicknameLen);

		std::array<BYTE, kPublicIdBytes> newPublicId{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(newPublicId.data(), newPublicId.size()) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		std::array<BYTE, kTokenBytes> newToken{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(newToken.data(), newToken.size()) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		const std::string publicIdHex = PublicIdToHex(newPublicId.data(), newPublicId.size());
		const std::string hashHex = HashTokenHex(newToken.data(), newToken.size());

		if( !guard->PrepareQuery(_T("INSERT INTO users (public_id, nickname, token_hash, created_at, updated_at) VALUES (?, ?, ?, NOW(), NOW())")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		_tstring publicIdHexT = Utf8ToTString(publicIdHex);
		_tstring nicknameT = Utf8ToTString(nickname);
		_tstring hashHexT = Utf8ToTString(hashHex);
		SQLLEN publicIdLenInd = SQL_NTS;
		SQLLEN nicknameLenInd = SQL_NTS;
		SQLLEN hashHexLenInd = SQL_NTS;

		guard->BindParamInput(1, publicIdHexT.c_str(), publicIdLenInd);
		guard->BindParamInput(2, nicknameT.c_str(), nicknameLenInd);
		guard->BindParamInput(3, hashHexT.c_str(), hashHexLenInd);

		if( guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, nickname, newPublicId, newToken);
			return EDBReturnType::OK;
		}

		// INSERT 실패 — 원인이 "중복 키"인지 그 외 오류인지 구분해야 한다.
		// CDBError는 SQLHENV/statement 핸들이 필요한데 CBaseODBC(BaseODBC.h)가
		// 그 핸들이나 최종 에러 정보를 외부에 노출하는 public 메서드가 없어
		// 여기서 직접 호출할 수 없다. 대신 핸들 접근이 필요 없는 방법으로
		// 판별한다: 실패가 이미 확정된 뒤 "이 닉네임이 지금 존재하는가"만
		// 다시 조회한다. 이 조회는 진단 목적일 뿐 정확성에 관여하지 않는다.
		// [참고] public_id는 128비트 진짜 난수라 충돌 확률이 사실상 0에
		// 수렴하므로, 이 시점의 중복 키는 거의 항상 nickname 쪽이다 — 그래도
		// nickname 존재 여부로 판별하는 게 유일하게 확인 가능한 신호이므로
		// 그대로 사용한다.
		guard->ClearStmt();
		if( guard->PrepareQuery(_T("SELECT 1 FROM users WHERE nickname = ?")) )
		{
			SQLLEN checkLenInd = SQL_NTS;
			guard->BindParamInput(1, nicknameT.c_str(), checkLenInd);

			if( guard->Execute() && guard->Fetch() )
			{
				guard->ClearStmt();
				if( req->onComplete )
					req->onComplete(ELoginResult::NicknameTaken, nickname, emptyPublicId, emptyToken);
				return EDBReturnType::OK;
			}
			guard->ClearStmt();
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
		return EDBReturnType::INVALID;
	}
	else
	{
		// ── 재접속 검증 ────────────────────────────────────────────
		if( !guard->PrepareQuery(_T("SELECT nickname, token_hash FROM users WHERE public_id = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::string(), emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		const std::string publicIdHex = PublicIdToHex(req->publicId, sizeof(req->publicId));
		_tstring publicIdHexT = Utf8ToTString(publicIdHex);
		SQLLEN publicIdLenInd = SQL_NTS;
		guard->BindParamInput(1, publicIdHexT.c_str(), publicIdLenInd);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::string(), emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		if( !guard->Fetch() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::AccountNotFound, std::string(), emptyPublicId, emptyToken);
			return EDBReturnType::FETCH_NOT_FIND;
		}

		TCHAR nicknameBuf[kNicknameBytes] = {};
		// [수정 — 문자열 잘림 버그] std::size(nicknameBuf)는 "요소 개수"를
		// 준다. 그런데 CBaseODBC::GetData()는 내부에서 UNICODE 빌드일 때
		// SQL_C_WCHAR로 SQLGetData()를 호출하는데, ODBC 스펙상 SQL_C_WCHAR의
		// BufferLength는 항상 "바이트 수"로 해석된다 — "문자 개수"가 아니다.
		// TCHAR(=wchar_t, 2바이트)에서 요소 개수를 그대로 넘기면 드라이버가
		// 실제 버퍼 크기의 절반으로 착각해 그 지점에서 문자열이 잘린다.
		// sizeof()는 ANSI 빌드(TCHAR=char, 1바이트)에서도 std::size()와
		// 값이 같으므로 두 빌드 모두에서 안전하다.
		int32 nicknameBufLen = static_cast<int32>(sizeof(nicknameBuf));
		if( !guard->GetData(1, nicknameBuf, nicknameBufLen) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::string(), emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		TCHAR hashBuf[65] = {};
		// [수정 — 실제 TokenMismatch 버그의 직접 원인] 위 nicknameBufLen과
		// 동일한 이유 — std::size(hashBuf)(=65, 요소 개수)를 그대로 넘기면
		// SQL_C_WCHAR가 "65바이트"로 착각해 실제로는 약 31글자에서 잘린
		// 문자열을 받게 된다. 그래서 저장된 해시(64자)가 항상 31자로
		// 잘려 들어와 토큰 비교가 매번 실패했다.
		int32 hashBufLen = static_cast<int32>(sizeof(hashBuf));
		if( !guard->GetData(2, hashBuf, hashBufLen) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::string(), emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();

		const std::string nickname = TStringToUtf8(_tstring(nicknameBuf));
		std::array<BYTE, kPublicIdBytes> publicId{};
		::memcpy(publicId.data(), req->publicId, publicId.size());

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
				req->onComplete(ELoginResult::TokenMismatch, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::OK;
		}

		// 토큰 일치 — 회전(새 토큰 발급) 후 갱신.
		std::array<BYTE, kTokenBytes> rotatedToken{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(rotatedToken.data(), rotatedToken.size()) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		const std::string rotatedHashHex = HashTokenHex(rotatedToken.data(), rotatedToken.size());

		if( !guard->PrepareQuery(_T("UPDATE users SET token_hash = ?, updated_at = NOW() WHERE public_id = ?")) )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}

		_tstring rotatedHashHexT = Utf8ToTString(rotatedHashHex);
		SQLLEN rotatedHashLenInd = SQL_NTS;
		SQLLEN publicIdLenInd2 = SQL_NTS;
		guard->BindParamInput(1, rotatedHashHexT.c_str(), rotatedHashLenInd);
		guard->BindParamInput(2, publicIdHexT.c_str(), publicIdLenInd2);

		if( !guard->Execute() )
		{
			guard->ClearStmt();
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, nickname, emptyPublicId, emptyToken);
			return EDBReturnType::INVALID;
		}
		guard->ClearStmt();

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, nickname, publicId, rotatedToken);
		return EDBReturnType::OK;
	}
}