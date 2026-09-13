
//***************************************************************************
// ListProfileImagesDBHandler.cpp : DBASYNC_LIST_PROFILE_IMAGES_REQ 핸들러
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBListProfileImagesRequest.h"

#include <cstring>

//***************************************************************************
// @brief 로그인된 계정이 갖고 있는 프로필 이미지 전체 목록을 최신순으로 조회합니다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ListProfileImages)
{
	ST_LIST_PROFILE_IMAGES_REQ* req = static_cast<ST_LIST_PROFILE_IMAGES_REQ*>(pStAsync);

	std::array<BYTE, kPublicIdBytes> publicId{};
	::memcpy(publicId.data(), req->publicId, publicId.size());
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	_tstring publicIdHexT = Utf8ToTString(publicIdHex);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_ListProfileImages: No available ODBC connection in pool."));
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
		return EDBReturnType::INVALID;
	}

	if( !guard->PrepareQuery(_T("SELECT image_id, image_ref, status FROM user_profile_images WHERE user_public_id = ? ORDER BY created_at DESC")) )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
		return EDBReturnType::INVALID;
	}

	SQLLEN publicIdLenInd = SQL_NTS;
	guard->BindParamInput(1, publicIdHexT.c_str(), publicIdLenInd);

	if( !guard->Execute() )
	{
		guard->ClearStmt();
		if( req->onComplete )
			req->onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
		return EDBReturnType::INVALID;
	}

	std::vector<SProfileImageEntry> images;
	while( guard->Fetch() )
	{
		TCHAR idBuf[32] = {};
		int32 idBufLen = static_cast<int32>(sizeof(idBuf));
		if( !guard->GetData(1, idBuf, idBufLen) )
			continue;

		TCHAR refBuf[kProfileImageUrlBytes] = {};
		int32 refBufLen = static_cast<int32>(sizeof(refBuf));
		if( !guard->GetData(2, refBuf, refBufLen) )
			continue;

		TCHAR statusBuf[8] = {};
		int32 statusBufLen = static_cast<int32>(sizeof(statusBuf));
		bool isActive = false;
		if( guard->GetData(3, statusBuf, statusBufLen) )
			isActive = (_ttoi(statusBuf) != 0);

		SProfileImageEntry entry;
		entry.imageId = static_cast<int64>(_ttoi64(idBuf));
		entry.imageRef = TStringToUtf8(_tstring(refBuf));
		entry.isActive = isActive;
		images.push_back(std::move(entry));
	}
	guard->ClearStmt();

	if( req->onComplete )
		req->onComplete(ELoginResult::Ok, images);
	return EDBReturnType::OK;
}