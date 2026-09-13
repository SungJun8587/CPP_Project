
//***************************************************************************
// DeleteProfileImageDBHandler.cpp : DBASYNC_DELETE_PROFILE_IMAGE_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBDeleteProfileImageRequest.h"

#include <cstring>

//***************************************************************************
// @brief 갤러리에서 이미지 하나를 삭제합니다. 삭제 "전"의 image_ref/대표
// 여부를 onComplete로 돌려줘서, 호출부(SelectProfileImageHandler.cpp 계열)가
// 필요하면 실제 파일 삭제/세션 값 갱신까지 이어갈 수 있게 한다 — 이
// 핸들러 자신은 DB 행만 지우고 파일 시스템은 건드리지 않는다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_DeleteProfileImage)
{
	ST_DELETE_PROFILE_IMAGE_REQ* req = static_cast<ST_DELETE_PROFILE_IMAGE_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> publicId{};
	::memcpy(publicId.data(), req->publicId, publicId.size());
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	_tstring publicIdHexT = Utf8ToTString(publicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_DeleteProfileImage: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, false, std::string());
		return EDBReturnType::INVALID;
	}

	TCHAR imageIdBuf[32] = {};
	_sntprintf_s(imageIdBuf, _countof(imageIdBuf), _TRUNCATE, _T("%lld"), static_cast<long long>(req->imageId));

	// 1) 삭제 전에 image_ref/status를 먼저 조회 — 소유자 검증도 겸함(WHERE에
	//    user_public_id 포함 — 남의 이미지 정보를 훔쳐볼 수 없음).
	if( !guard->PrepareQuery(_T("SELECT image_ref, status FROM user_profile_images WHERE image_id = ? AND user_public_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, false, std::string());
		return EDBReturnType::INVALID;
	}

	SQLLEN idLenInd = SQL_NTS;
	SQLLEN ownerLenInd = SQL_NTS;
	guard->BindParamInput(1, imageIdBuf, idLenInd);
	guard->BindParamInput(2, publicIdHexT.c_str(), ownerLenInd);

	if( !guard->Execute() || !guard->Fetch() )
	{
		// 존재하지 않거나 내 것이 아님 — "지울 게 없음"으로 처리.
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::AccountNotFound, req->imageId, false, std::string());
		return EDBReturnType::FETCH_NOT_FIND;
	}

	TCHAR imageRefBuf[kProfileImageUrlBytes] = {};
	int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
	guard->GetData(1, imageRefBuf, imageRefBufLen);
	const std::string imageRef = TStringToUtf8(_tstring(imageRefBuf));

	TCHAR statusBuf[8] = {};
	int32 statusBufLen = static_cast<int32>(sizeof(statusBuf));
	bool wasActive = false;
	if( guard->GetData(2, statusBuf, statusBufLen) )
		wasActive = (_ttoi(statusBuf) != 0);

	guard->ClearStmt();

	// 2) 실제 삭제
	if( !guard->PrepareQuery(_T("DELETE FROM user_profile_images WHERE image_id = ? AND user_public_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, false, std::string());
		return EDBReturnType::INVALID;
	}

	SQLLEN delIdLenInd = SQL_NTS;
	SQLLEN delOwnerLenInd = SQL_NTS;
	guard->BindParamInput(1, imageIdBuf, delIdLenInd);
	guard->BindParamInput(2, publicIdHexT.c_str(), delOwnerLenInd);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, false, std::string());
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, req->imageId, wasActive, imageRef);
	return EDBReturnType::OK;
}