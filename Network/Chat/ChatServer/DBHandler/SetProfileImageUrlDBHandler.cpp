
//***************************************************************************
// SetProfileImageUrlDBHandler.cpp : DBASYNC_SET_PROFILE_IMAGE_URL_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBSetProfileImageUrlRequest.h"

#include <cstring>

//***************************************************************************
// @brief 외부 URL을 user_profile_images에 새 행으로 등록하고 대표(status=1)로
//        지정합니다. 빈 문자열이면 기존 대표 행을 0으로 내리기만 합니다.
// @details [설계] 두 단계 쿼리로 처리한다:
//          1) UPDATE user_profile_images SET status=0 WHERE user_public_id=? AND status=1
//             — 기존에 대표였던 행이 있으면 먼저 내린다.
//          2) (빈 문자열이 아니면) INSERT ... VALUES (..., status=1)
//             — 새 행을 곧바로 대표로 등록한다.
//          이 두 쿼리는 하나의 명시적 트랜잭션으로 묶여있지 않다 — 1번
//          성공 후 서버가 죽으면 "대표가 하나도 없는" 상태로 잠깐 남을 수
//          있다(create_chat_db.sql 주석 참고, 실용적 타협).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_SetProfileImageUrl)
{
	ST_SET_PROFILE_IMAGE_URL_REQ* req = static_cast<ST_SET_PROFILE_IMAGE_URL_REQ*>(pStAsync);

	const size_t urlLen = ::strnlen(req->url, sizeof(req->url));
	const std::string newUrl(req->url, urlLen);

	std::array<BYTE, kPublicIdBytes> publicId{};
	::memcpy(publicId.data(), req->publicId, publicId.size());

	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	_tstring publicIdHexT = Utf8ToTString(publicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_SetProfileImageUrl: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
		return EDBReturnType::INVALID;
	}

	// 1) 기존 대표 이미지 해제
	if( !guard->PrepareQuery(_T("UPDATE user_profile_images SET status = 0 WHERE user_public_id = ? AND status = 1")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
		return EDBReturnType::INVALID;
	}

	SQLLEN unsetPublicIdLenInd = SQL_NTS;
	guard->BindParamInput(1, publicIdHexT.c_str(), unsetPublicIdLenInd);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	// 빈 문자열 = "해제"만 하고 끝 — 새로 등록할 URL이 없다.
	if( newUrl.empty() )
	{
		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, publicId, newUrl, 0);
		return EDBReturnType::OK;
	}

	// 2) 새 URL을 대표로 등록 — file_size는 항상 0. 실제 파일 크기는 이제
	// 채팅 서버가 알 방법이 없다(이미지 저장을 파일 서버로 분리했으므로).
	if( !guard->PrepareQuery(_T("INSERT INTO user_profile_images (user_public_id, image_ref, file_size, status) VALUES (?, ?, 0, 1)")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
		return EDBReturnType::INVALID;
	}

	_tstring newUrlT = Utf8ToTString(newUrl);

	SQLLEN insertPublicIdLenInd = SQL_NTS;
	SQLLEN newUrlLenInd = SQL_NTS;
	guard->BindParamInput(1, publicIdHexT.c_str(), insertPublicIdLenInd);
	guard->BindParamInput(2, newUrlT.c_str(), newUrlLenInd);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
		return EDBReturnType::INVALID;
	}

	// AUTO_INCREMENT로 방금 생성된 image_id를 가져온다.
	int64 newImageId = 0;
	if( guard->PrepareQuery(_T("SELECT LAST_INSERT_ID()")) )
	{
		if( guard->Execute() && guard->Fetch() )
		{
			TCHAR idBuf[32] = {};
			int32 idBufLen = static_cast<int32>(sizeof(idBuf));
			if( guard->GetData(1, idBuf, idBufLen) )
				newImageId = static_cast<int64>(_ttoi64(idBuf));
		}
		guard->ClearStmt();
	}

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, publicId, newUrl, newImageId);
	return EDBReturnType::OK;
}