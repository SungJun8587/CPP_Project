
//***************************************************************************
// SelectProfileImageDBHandler.cpp : DBASYNC_SELECT_PROFILE_IMAGE_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBSelectProfileImageRequest.h"

#include <cstring>

//***************************************************************************
// @brief 갤러리에 이미 있는 이미지 하나를 대표(status=1)로 지정합니다.
// @details [알려진 한계] 2번 UPDATE가 실제로 행을 하나라도 바꿨는지
// (RowCount()) 확인하지 않는다 — imageId가 존재하지 않거나 남의 것이면
// WHERE에 매치되는 행이 없어 조용히 아무 일도 안 일어나는데, 그래도 이
// 핸들러는 Ok를 반환한다. 정확히 하려면 RowCount() 검증이 필요하지만,
// 이 프로젝트의 CBaseODBC 래퍼에서 RowCount() 계열 API의 정확한 시그니처를
// 확인 못 해 데모 범위에서는 생략했다 — 실제 적용 시 보강 필요.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_SelectProfileImage)
{
	ST_SELECT_PROFILE_IMAGE_REQ* req = static_cast<ST_SELECT_PROFILE_IMAGE_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> publicId{};
	::memcpy(publicId.data(), req->publicId, publicId.size());
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	_tstring publicIdHexT = Utf8ToTString(publicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_SelectProfileImage: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}

	TCHAR imageIdBuf[32] = {};
	_sntprintf_s(imageIdBuf, _countof(imageIdBuf), _TRUNCATE, _T("%lld"), static_cast<long long>(req->imageId));

	// 0) 지정한 imageId가 실제로 이 계정 소유인지 + image_ref를 먼저 조회.
	//    존재하지 않거나 남의 것이면 여기서 걸러진다.
	if( !guard->PrepareQuery(_T("SELECT image_ref FROM user_profile_images WHERE image_id = ? AND user_public_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}

	SQLLEN checkIdLenInd = SQL_NTS;
	SQLLEN checkOwnerLenInd = SQL_NTS;
	guard->BindParamInput(1, imageIdBuf, checkIdLenInd);
	guard->BindParamInput(2, publicIdHexT.c_str(), checkOwnerLenInd);

	if( !guard->Execute() || !guard->Fetch() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::AccountNotFound, req->imageId, std::string());
		return EDBReturnType::FETCH_NOT_FIND;
	}

	TCHAR imageRefBuf[kProfileImageUrlBytes] = {};
	int32 imageRefBufLen = static_cast<int32>(sizeof(imageRefBuf));
	guard->GetData(1, imageRefBuf, imageRefBufLen);
	const std::string selectedImageRef = TStringToUtf8(_tstring(imageRefBuf));
	guard->ClearStmt();

	// 1) 기존 대표 해제
	if( !guard->PrepareQuery(_T("UPDATE user_profile_images SET status = 0 WHERE user_public_id = ? AND status = 1")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}
	SQLLEN unsetLenInd = SQL_NTS;
	guard->BindParamInput(1, publicIdHexT.c_str(), unsetLenInd);
	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	// 2) 지정한 imageId를 대표로 지정
	if( !guard->PrepareQuery(_T("UPDATE user_profile_images SET status = 1 WHERE image_id = ? AND user_public_id = ?")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}

	SQLLEN setIdLenInd = SQL_NTS;
	SQLLEN setOwnerLenInd = SQL_NTS;
	guard->BindParamInput(1, imageIdBuf, setIdLenInd);
	guard->BindParamInput(2, publicIdHexT.c_str(), setOwnerLenInd);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, req->imageId, std::string());
		return EDBReturnType::INVALID;
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, req->imageId, selectedImageRef);
	return EDBReturnType::OK;
}