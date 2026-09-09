
//***************************************************************************
// ChangeNicknameDBHandler.cpp : DBASYNC_CHANGE_NICKNAME_REQ(닉네임 변경) 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DbServiceManager.h"
#include "DBChangeNicknameRequest.h"
#include "NicknameValidation.h"

#include <cstring>

namespace
{
	using NicknameValidation::IsValidNickname;
}

//***************************************************************************
// @brief 로그인된 계정(publicId로 식별)의 닉네임을 newNickname으로 바꿉니다.
// @details [설계 변경] nickname은 더 이상 PRIMARY KEY가 아니다
// (create_chat_db.sql — uid가 내부 PK, public_id가 외부 안정 식별자). 그래서
// 대상 행을 publicId로 찾아 nickname 컬럼만 UPDATE한다 — "지금 닉네임이
// 뭐였는지"는 이 요청 처리 과정에서 몰라도 되고, 알 필요도 없다.
//
// MySQL은 새 nickname 값이 이미 다른 행에 있으면 UNIQUE 제약 위반(1062)을
// 낸다 — AccountDBHandler.cpp의 회원가입 핸들러와 똑같은 이유(CDBError가
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
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ChangeNickname)
{
	ST_CHANGE_NICKNAME_REQ* req = static_cast<ST_CHANGE_NICKNAME_REQ*>(pStAsync);

	const size_t newLen = ::strnlen(req->newNickname, sizeof(req->newNickname));
	const std::string newNickname(req->newNickname, newLen);

	std::array<BYTE, kPublicIdBytes> publicId{};
	::memcpy(publicId.data(), req->publicId, publicId.size());

	// [설계 변경] public_id는 DB에 BINARY(16)이 아니라 CHAR(32)(16진 문자열)로
	// 저장한다 — token_hash와 동일한 이유(ODBC 바이너리 파라미터 바인딩의
	// 드라이버별 불확실성 회피). 와이어 프로토콜/세션은 여전히 raw
	// 16바이트를 쓰고, DB 파라미터 바인딩 시점에만 문자열로 변환한다.
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());

	if( !IsValidNickname(req->newNickname, newLen) )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::InvalidNickname, publicId, newNickname);
		return EDBReturnType::OK;
	}

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ChangeNickname: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newNickname);
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T("UPDATE users SET nickname = ?, updated_at = NOW() WHERE public_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newNickname);
		return EDBReturnType::INVALID;
	}

	_tstring newNicknameT = Utf8ToTString(newNickname);
	_tstring publicIdHexT = Utf8ToTString(publicIdHex);
	SQLLEN newLenInd = SQL_NTS;
	SQLLEN publicIdLenInd = SQL_NTS;

	guard->BindParamInput(1, newNicknameT.c_str(), newLenInd);
	guard->BindParamInput(2, publicIdHexT.c_str(), publicIdLenInd);

	if( !guard->Execute() )
	{
		// UPDATE 실패 — 원인이 "새 닉네임이 이미 다른 계정에 있음(UNIQUE
		// 위반)"인지 그 외 오류인지 구분. AccountDBHandler.cpp의 회원가입
		// 핸들러와 동일한 방식 — 실패 확정 후 새 닉네임의 존재 여부만
		// 사후 조회한다(이 조회는 진단 목적일 뿐 정확성에 관여하지 않음).
		guard->ClearStmt();
		if( guard->PrepareQuery(_T("SELECT 1 FROM users WHERE nickname = ?")) )
		{
			SQLLEN checkLenInd = SQL_NTS;
			guard->BindParamInput(1, newNicknameT.c_str(), checkLenInd);

			if( guard->Execute() && guard->Fetch() )
			{
				guard->ClearStmt();
				if( req->onComplete )
					req->onComplete(ELoginResult::NicknameTaken, publicId, newNickname);
				return EDBReturnType::OK;
			}
			guard->ClearStmt();
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newNickname);
		return EDBReturnType::INVALID;
	}

	// UPDATE 자체는 "오류 없음"으로 끝났어도, RowCount()가 0이면 두 가지
	// 경우가 섞여 있다(위 함수 설명 참고) — publicId 존재 여부로 구분한다.
	if( guard->RowCount() <= 0 )
	{
		guard->ClearStmt();

		bool accountExists = false;
		if( guard->PrepareQuery(_T("SELECT 1 FROM users WHERE public_id = ?")) )
		{
			SQLLEN existLenInd = SQL_NTS;
			guard->BindParamInput(1, publicIdHexT.c_str(), existLenInd);

			if( guard->Execute() && guard->Fetch() )
				accountExists = true;

			guard->ClearStmt();
		}

		if( accountExists )
		{
			// 새 닉네임이 이미 지금 값과 같아서 실제 변경이 없었을 뿐 —
			// 정상 성공으로 처리한다.
			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, publicId, newNickname);
			return EDBReturnType::OK;
		}

		LOG_ERROR(_T("kDbCallIdent_ChangeNickname: no row matched publicId during UPDATE (account vanished?)"));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newNickname);
		return EDBReturnType::INVALID;
	}

	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, publicId, newNickname);
	return EDBReturnType::OK;
}