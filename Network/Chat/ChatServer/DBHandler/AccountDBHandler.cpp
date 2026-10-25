
//***************************************************************************
// AccountDBHandler.cpp : kDbCallIdent_Account 핸들러 — 계정 DB 연산 전체
//
// 회원가입/재접속 검증과 닉네임 변경을 EAccountOp로 구분해 처리한다. 연산마다 입력과
// 콜백이 달라 요청 구조체는 연산별(DBAccountRequest.h)로 두고, 이 파일은 op를 보고
// 알맞은 함수로 나눠 준다. 모든 연산은 어떤 경로에서도 onComplete를 정확히 한 번
// 호출한다.
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "../ChatCommon/Protocol/ChatPacketUtil.h"
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include "DBAccountRequest.h"
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
	// @brief 회원가입(hasToken==false) 또는 재접속 검증(hasToken==true)을 처리합니다.
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
	EDBReturnType HandleSignup(ST_SIGNUP_REQ* req, OdbcConnGuard& guard)
	{
		const std::array<BYTE, kTokenBytes> emptyToken{};
		const std::array<BYTE, kPublicIdBytes> emptyPublicId{};

		// 실패 응답 — 닉네임만 채우고 나머지는 비워서 돌려준다.
		auto complete = [req, &emptyToken, &emptyPublicId](ELoginResult result, const std::string& nickname)
			{
				if( req->onComplete )
					req->onComplete(result, nickname, emptyPublicId, emptyToken, std::string());
			};

		if( guard == nullptr )
		{
			complete(ELoginResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 스키마: create_chat_db.sql 참고
		//   CREATE TABLE users (
		//     uid        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
		//     public_id  CHAR(32)     NOT NULL UNIQUE,   -- 16바이트 난수의 16진 인코딩
		//     nickname   VARCHAR(16)  NOT NULL UNIQUE,
		//     token_hash CHAR(64)     NOT NULL,   -- SHA-256 hex
		//     created_at DATETIME     NOT NULL,
		//     updated_at DATETIME     NOT NULL
		//   );
		// 프로필 이미지는 users가 아니라 user_profile_images(status=1인 행이 대표)에 있다.

		if( !req->hasToken )
		{
			// ── 신규 가입 ──────────────────────────────────────────────
			const size_t nicknameLen = ::strnlen(req->nickname, sizeof(req->nickname));
			const std::string nickname(req->nickname, nicknameLen);
			if( !IsValidNickname(req->nickname, nicknameLen) )
			{
				complete(ELoginResult::InvalidNickname, nickname);
				return EDBReturnType::OK;
			}

			std::array<BYTE, kPublicIdBytes> newPublicId{};
			std::array<BYTE, kTokenBytes> newToken{};
			if( !Crypto::CCryptoUtil::GenerateRandomBytes(newPublicId.data(), newPublicId.size()) ||
				!Crypto::CCryptoUtil::GenerateRandomBytes(newToken.data(), newToken.size()) )
			{
				complete(ELoginResult::DbError, nickname);
				return EDBReturnType::INVALID;
			}

			const std::string publicIdHex = PublicIdToHex(newPublicId);
			const std::string hashHex = HashTokenHex(newToken.data(), newToken.size());

			if( sql.Exec(_T("INSERT INTO users (public_id, nickname, token_hash, created_at, updated_at) VALUES (?, ?, ?, NOW(), NOW())"),
				{ publicIdHex, nickname, hashHex }) )
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::Ok, nickname, newPublicId, newToken, std::string());
				return EDBReturnType::OK;
			}

			// INSERT 실패 — 원인이 "중복 키"인지 그 외 오류인지 구분해야 한다. 오류 코드를
			// 읽을 방법이 없어서, 실패가 확정된 뒤 "이 닉네임이 지금 존재하는가"만 다시
			// 조회한다(진단 목적일 뿐 정확성에 관여하지 않음). public_id는 128비트 난수라
			// 충돌 확률이 사실상 0이므로, 이 시점의 중복 키는 거의 항상 nickname 쪽이다.
			bool taken = false;
			if( sql.Exists(_T("SELECT 1 FROM users WHERE nickname = ?"), { nickname }, taken) && taken )
			{
				complete(ELoginResult::NicknameTaken, nickname);
				return EDBReturnType::OK;
			}

			complete(ELoginResult::DbError, nickname);
			return EDBReturnType::INVALID;
		}

		// ── 재접속 검증 ────────────────────────────────────────────
		const std::string publicIdHex = PublicIdToHex(req->publicId);

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT nickname, token_hash FROM users WHERE public_id = ?"), { publicIdHex }, 2, rows) )
		{
			complete(ELoginResult::DbError, std::string());
			return EDBReturnType::INVALID;
		}

		if( rows.empty() )
		{
			complete(ELoginResult::AccountNotFound, std::string());
			return EDBReturnType::FETCH_NOT_FIND;
		}

		const std::string nickname = rows.front()[0];
		const std::string& storedHashHex = rows.front()[1];

		// 프로필 이미지는 user_profile_images(status=1)에서 별도 쿼리로 가져온다. 행이 없는
		// 것(한 번도 설정 안 함/최근 해제)은 에러가 아니라 "미설정"인 정상 상태이므로, 조회
		// 실패도 로그인 자체를 실패시키지 않고 빈 문자열로 둔다.
		std::string profileImageUrl;
		{
			std::vector<DbUtil::Row> imageRows;
			if( sql.Query(_T("SELECT image_ref FROM user_profile_images WHERE user_public_id = ? AND status = 1 LIMIT 1"),
				{ publicIdHex }, 1, imageRows) && !imageRows.empty() )
			{
				profileImageUrl = imageRows.front()[0];
			}
		}

		PublicId publicId = ToPublicId(req->publicId);
		const std::string providedHashHex = HashTokenHex(req->token, sizeof(req->token));

		const bool tokenMatches =
			storedHashHex.size() == providedHashHex.size() &&
			Crypto::CCryptoUtil::ConstantTimeEquals(
				reinterpret_cast<const unsigned char*>(storedHashHex.data()),
				reinterpret_cast<const unsigned char*>(providedHashHex.data()),
				storedHashHex.size());

		if( !tokenMatches )
		{
			complete(ELoginResult::TokenMismatch, nickname);
			return EDBReturnType::OK;
		}

		// 토큰 일치 — 회전(새 토큰 발급) 후 갱신.
		std::array<BYTE, kTokenBytes> rotatedToken{};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(rotatedToken.data(), rotatedToken.size()) )
		{
			complete(ELoginResult::DbError, nickname);
			return EDBReturnType::INVALID;
		}

		const std::string rotatedHashHex = HashTokenHex(rotatedToken.data(), rotatedToken.size());
		if( !sql.Exec(_T("UPDATE users SET token_hash = ?, updated_at = NOW() WHERE public_id = ?"),
			{ rotatedHashHex, publicIdHex }) )
		{
			complete(ELoginResult::DbError, nickname);
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, nickname, publicId, rotatedToken, profileImageUrl);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 로그인된 계정(publicId로 식별)의 닉네임을 newNickname으로 바꿉니다.
	// @details [설계 변경] nickname은 더 이상 PRIMARY KEY가 아니다
	// (create_chat_db.sql — uid가 내부 PK, public_id가 외부 안정 식별자). 그래서
	// 대상 행을 publicId로 찾아 nickname 컬럼만 UPDATE한다 — "지금 닉네임이
	// 뭐였는지"는 이 요청 처리 과정에서 몰라도 되고, 알 필요도 없다.
	//
	// MySQL은 새 nickname 값이 이미 다른 행에 있으면 UNIQUE 제약 위반(1062)을
	// 낸다 — 회원가입(HandleSignup)과 똑같은 이유(CDBError가
	// 필요로 하는 핸들 접근 불가)로, 실패 원인을 CDBError 대신 "실패 후
	// 존재확인 SELECT"로 판별한다.
	//
	// [주의 — RowCount()의 이중 의미] UPDATE가 오류 없이 끝나도 RowCount()가
	// 0일 수 있는 경우가 두 가지 있다: (a) publicId에 해당하는 행 자체가 없음,
	// (b) newNickname이 이미 지금 값과 똑같아서 MySQL이 "실제 변경 없음"으로
	// 0을 보고함(자기 자신과의 UNIQUE 충돌은 애초에 발생하지 않으므로 Execute()
	// 자체는 정상 성공함). 이 둘을 구분하려면 별도로 publicId 존재 여부를
	// 확인해야 한다 — 아래 두 번째 SELECT가 그 역할.
	//***************************************************************************
	//***************************************************************************
	EDBReturnType HandleChangeNickname(ST_CHANGE_NICKNAME_REQ* req, OdbcConnGuard& guard)
	{
		const std::string newNickname = FixedBuf::ReadStr(req->newNickname);

		PublicId publicId = ToPublicId(req->publicId);

		// [설계 변경] public_id는 DB에 BINARY(16)이 아니라 CHAR(32)(16진 문자열)로
		// 저장한다 — token_hash와 동일한 이유(ODBC 바이너리 파라미터 바인딩의
		// 드라이버별 불확실성 회피). 와이어 프로토콜/세션은 여전히 raw
		// 16바이트를 쓰고, DB 파라미터 바인딩 시점에만 문자열로 변환한다.
		const std::string publicIdHex = PublicIdToHex(publicId);

		auto complete = [req, publicId, &newNickname](ELoginResult result)
			{
				if( req->onComplete )
					req->onComplete(result, publicId, newNickname);
			};

		if( !IsValidNickname(req->newNickname, newNickname.size()) )
		{
			complete(ELoginResult::InvalidNickname);
			return EDBReturnType::OK;
		}

		if( guard == nullptr )
		{
			complete(ELoginResult::DbError);
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		int64 affected = 0;
		if( !sql.Exec(_T("UPDATE users SET nickname = ?, updated_at = NOW() WHERE public_id = ?"),
			{ newNickname, publicIdHex }, &affected) )
		{
			// UPDATE 실패 — 원인이 "새 닉네임이 이미 다른 계정에 있음(UNIQUE 위반)"인지
			// 그 외 오류인지 구분한다. 실패 확정 후 새 닉네임의 존재 여부만 사후 조회한다
			// (이 조회는 진단 목적일 뿐 정확성에 관여하지 않음).
			bool taken = false;
			if( sql.Exists(_T("SELECT 1 FROM users WHERE nickname = ?"), { newNickname }, taken) && taken )
			{
				complete(ELoginResult::NicknameTaken);
				return EDBReturnType::OK;
			}

			complete(ELoginResult::DbError);
			return EDBReturnType::INVALID;
		}

		// UPDATE 자체는 "오류 없음"으로 끝났어도 RowCount()가 0이면 두 가지 경우가
		// 섞여 있다(계정이 없음 / 값이 이미 같아서 변경이 없음) — publicId 존재 여부로 구분한다.
		if( affected <= 0 )
		{
			bool accountExists = false;
			if( sql.Exists(_T("SELECT 1 FROM users WHERE public_id = ?"), { publicIdHex }, accountExists) && accountExists )
			{
				// 새 닉네임이 지금 값과 같아서 실제 변경이 없었을 뿐 — 정상 성공으로 처리한다.
				complete(ELoginResult::Ok);
				return EDBReturnType::OK;
			}

			LOG_ERROR(_T("kDbCallIdent_Account: no row matched publicId during UPDATE (account vanished?)"));
			complete(ELoginResult::DbError);
			return EDBReturnType::INVALID;
		}

		complete(ELoginResult::Ok);
		return EDBReturnType::OK;
	}

}

//***************************************************************************
// @brief 계정 DB 요청 하나를 연산별 함수로 나눠 처리한다.
// @details 커넥션을 못 얻으면 로그는 여기서 한 번 남기고, 각 연산 함수가 자기 콜백으로
//          DbError를 돌려준다(콜백 모양이 연산마다 다르기 때문).
// [등록] COdbcAsyncSrv 자신의 Instance()가 없어져서(도메인별
//          다중 인스턴스를 지원하도록 CDbServiceManager로 소유권이 옮겨감),
//          DECLARE_DBASYNC_HANDLER_EX(instanceExpr, command) 매크로로
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
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Account)
{
	ST_ACCOUNT_REQ_BASE* base = static_cast<ST_ACCOUNT_REQ_BASE*>(pStAsync);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
		LOG_ERROR(_T("kDbCallIdent_Account: No available ODBC connection in pool."));

	switch( base->op )
	{
	case EAccountOp::Signup:			return HandleSignup(static_cast<ST_SIGNUP_REQ*>(base), guard);
	case EAccountOp::ChangeNickname:	return HandleChangeNickname(static_cast<ST_CHANGE_NICKNAME_REQ*>(base), guard);
	}

	LOG_ERROR(_T("kDbCallIdent_Account: unknown op %d"), static_cast<int>(base->op));
	return EDBReturnType::INVALID;
}