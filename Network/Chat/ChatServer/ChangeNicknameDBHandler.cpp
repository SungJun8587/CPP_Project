
//***************************************************************************
// ChangeNicknameDBHandler.cpp : DBASYNC_CHANGE_NICKNAME_REQ(닉네임 변경) 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
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
// @brief 로그인된 계정의 닉네임을 newNickname으로 바꿉니다.
// @details 스키마(create_chat_db.sql): nickname이 PRIMARY KEY이므로 이건
// 사실상 "그 행의 PK를 바꾸는 UPDATE"다. MySQL은 새 값이 이미 다른 행의
// PK와 겹치면 INSERT와 동일한 중복 키 오류(1062)를 낸다 — AccountDBHandler.cpp의
// 회원가입 핸들러와 똑같은 이유(CDBError가 필요로 하는 핸들 접근 불가)로,
// 실패 원인을 CDBError 대신 "실패 후 존재확인 SELECT"로 판별한다.
//
// 그리고 UPDATE는 SQL 문법상 "WHERE 절에 매치되는 행이 0개"여도 오류가
// 아니라 그냥 영향받은 행 0개로 성공 취급된다 — oldNickname이 이미
// 사라졌거나(동시에 다른 곳에서 지워짐 등) 잘못 넘어온 경우를 놓치지
// 않으려면 RowCount()로 실제 영향받은 행 수를 확인해야 한다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ChangeNickname)
{
	ST_CHANGE_NICKNAME_REQ* req = static_cast<ST_CHANGE_NICKNAME_REQ*>(pStAsync);

	const size_t oldLen = ::strnlen(req->oldNickname, sizeof(req->oldNickname));
	const size_t newLen = ::strnlen(req->newNickname, sizeof(req->newNickname));
	const std::string oldNickname(req->oldNickname, oldLen);
	const std::string newNickname(req->newNickname, newLen);

	if( !IsValidNickname(req->newNickname, newLen) )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::InvalidNickname, oldNickname, newNickname);
		return EDBReturnType::OK;
	}

	if( oldNickname == newNickname )
	{
		// 바꾸려는 닉네임이 지금과 똑같음 — DB를 건드릴 필요 없이 그대로 성공 처리.
		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, oldNickname, newNickname);
		return EDBReturnType::OK;
	}

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ChangeNickname: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, oldNickname, newNickname);
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T("UPDATE users SET nickname = ?, updated_at = NOW() WHERE nickname = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, oldNickname, newNickname);
		return EDBReturnType::INVALID;
	}

	_tstring newNicknameT = Utf8ToTString(newNickname);
	_tstring oldNicknameT = Utf8ToTString(oldNickname);
	SQLLEN newLenInd = SQL_NTS;
	SQLLEN oldLenInd = SQL_NTS;

	guard->BindParamInput(1, newNicknameT.c_str(), newLenInd);
	guard->BindParamInput(2, oldNicknameT.c_str(), oldLenInd);

	if( !guard->Execute() )
	{
		// UPDATE 실패 — 원인이 "새 닉네임이 이미 다른 계정에 있음(중복
		// PK)"인지 그 외 오류인지 구분. AccountDBHandler.cpp의 회원가입
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
					req->onComplete(ELoginResult::NicknameTaken, oldNickname, newNickname);
				return EDBReturnType::OK;
			}
			guard->ClearStmt();
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, oldNickname, newNickname);
		return EDBReturnType::INVALID;
	}

	// UPDATE 자체는 "오류 없음"으로 끝났어도, WHERE oldNickname=?에 매치되는
	// 행이 실제로 있었는지는 별개다 — RowCount()로 확인한다.
	if( guard->RowCount() <= 0 )
	{
		guard->ClearStmt();
		LOG_ERROR(_T("kDbCallIdent_ChangeNickname: no row matched oldNickname during UPDATE (account vanished?)"));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, oldNickname, newNickname);
		return EDBReturnType::INVALID;
	}

	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, oldNickname, newNickname);
	return EDBReturnType::OK;
}