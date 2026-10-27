
//***************************************************************************
// ProfileImageDBHandler.cpp : kDbCallIdent_ProfileImage 핸들러 — 프로필 이미지 DB 연산 전체
//
// 갤러리 항목 등록/대표 지정/목록/삭제를 EProfileImageOp로 구분해 처리한다. 연산마다
// 입력과 콜백이 달라 요청 구조체는 연산별(DBProfileImageRequest.h)로 두고, 이 파일은
// op를 보고 알맞은 함수로 나눠 준다. 모든 연산은 어떤 경로에서도 onComplete를 정확히
// 한 번 호출한다.
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "DBProfileImageRequest.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief URL을 갤러리의 대표(status=1) 이미지로 지정합니다. 빈 문자열이면
	//        기존 대표를 0으로 내리기만 합니다.
	// @details [설계] 같은 URL이 이 유저의 갤러리에 이미 있으면 새 행을 만들지 않고
	//          그 행을 대표로 올린다 — 같은 참조를 가리키는 행이 둘이 되면 한쪽을
	//          지울 때 파일 서버의 실제 파일이 삭제돼 다른 행이 깨지기 때문이다.
	//          없으면 status=0으로 먼저 INSERT한 뒤, SelectProfileImage와 같은 단일
	//          UPDATE(CASE)로 대표를 전환한다. INSERT가 실패하면 기존 대표가 그대로
	//          남고, UPDATE가 실패해도 기존 대표가 유지되므로(새 행은 일반 이미지로
	//          남음) "대표가 하나도 없는" 상태가 생기지 않는다.
	//***************************************************************************
	EDBReturnType HandleSet(ST_SET_PROFILE_IMAGE_URL_REQ* req, OdbcConnGuard& guard)
	{
		const size_t urlLen = ::strnlen(req->url, sizeof(req->url));
		const std::string newUrl(req->url, urlLen);

		PublicId publicId = ToPublicId(req->publicId);
		const std::string publicIdHex = PublicIdToHex(publicId);

		auto fail = [req, publicId, &newUrl]()
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::DbError, publicId, newUrl, 0);
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 빈 문자열 = "해제"만 하고 끝 — 새로 등록할 URL이 없다.
		if( newUrl.empty() )
		{
			if( !sql.Exec(_T("UPDATE user_profile_images SET status = 0 WHERE user_public_id = ? AND status = 1"),
				{ publicIdHex }) )
			{
				fail();
				return EDBReturnType::INVALID;
			}

			if( req->onComplete )
				req->onComplete(ELoginResult::Ok, publicId, newUrl, 0);
			return EDBReturnType::OK;
		}

		// 같은 URL이 이미 있으면 재사용(URL은 대소문자를 구분하므로 이진 비교).
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT image_id FROM user_profile_images WHERE user_public_id = ? AND image_ref = BINARY ? ORDER BY image_id LIMIT 1"),
			{ publicIdHex, newUrl }, 1, rows) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		int64 imageId = 0;
		if( !rows.empty() )
		{
			imageId = DbUtil::ToI64(rows.front()[0]);
		}
		else
		{
			// file_size는 항상 0 — 이미지 저장을 파일 서버로 분리했으므로 채팅 서버는 크기를 모른다.
			if( !sql.Exec(_T("INSERT INTO user_profile_images (user_public_id, image_ref, file_size, status) VALUES (?, ?, 0, 0)"),
				{ publicIdHex, newUrl }) || !sql.LastInsertId(imageId) || imageId <= 0 )
			{
				fail();
				return EDBReturnType::INVALID;
			}
		}

		const std::string imageIdText = std::to_string(imageId);
		if( !sql.Exec(_T("UPDATE user_profile_images SET status = CASE WHEN image_id = ? THEN 1 ELSE 0 END ")
			_T("WHERE user_public_id = ? AND (status = 1 OR image_id = ?)"),
			{ imageIdText, publicIdHex, imageIdText }) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, publicId, newUrl, imageId);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 로그인된 계정이 갖고 있는 프로필 이미지 전체 목록을 최신순으로 조회합니다.
	//***************************************************************************
	EDBReturnType HandleList(ST_LIST_PROFILE_IMAGES_REQ* req, OdbcConnGuard& guard)
	{
		const std::string publicIdHex = PublicIdToHex(ToPublicId(req->publicId));

		if( guard == nullptr )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT image_id, image_ref, status FROM user_profile_images WHERE user_public_id = ? ORDER BY created_at DESC, image_id DESC"),
			{ publicIdHex }, 3, rows) )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
			return EDBReturnType::INVALID;
		}

		std::vector<SProfileImageEntry> images;
		images.reserve(rows.size());
		for( const DbUtil::Row& row : rows )
		{
			if( row[0].empty() || row[1].empty() )
				continue;	// 기존과 동일: image_id/image_ref가 비어 있는 행은 건너뜀

			SProfileImageEntry entry;
			entry.imageId = DbUtil::ToI64(row[0]);
			entry.imageRef = row[1];
			entry.isActive = (DbUtil::ToI64(row[2]) != 0);
			images.push_back(std::move(entry));
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, images);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 갤러리에 이미 있는 이미지 하나를 대표(status=1)로 지정합니다.
	// @details 소유 여부는 먼저 SELECT로 확인하고(없거나 남의 것이면 AccountNotFound),
	// 대표 교체는 UPDATE 한 문장으로 처리한다. 기존 대표 해제와 새 대표 지정이
	// 한 문장 안에서 일어나므로 중간에 실패해도 대표가 없는 상태가 남지 않는다.
	//***************************************************************************
	EDBReturnType HandleSelect(ST_SELECT_PROFILE_IMAGE_REQ* req, OdbcConnGuard& guard)
	{
		const std::string publicIdHex = PublicIdToHex(ToPublicId(req->publicId));
		const std::string imageIdText = std::to_string(req->imageId);

		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::DbError, req->imageId, std::string());
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 0) 지정한 imageId가 실제로 이 계정 소유인지 + image_ref를 먼저 조회.
		//    존재하지 않거나 남의 것이면 여기서 걸러진다.
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT image_ref FROM user_profile_images WHERE image_id = ? AND user_public_id = ?"),
			{ imageIdText, publicIdHex }, 1, rows) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( rows.empty() )
		{
			if( req->onComplete )
				req->onComplete(ELoginResult::AccountNotFound, req->imageId, std::string());
			return EDBReturnType::FETCH_NOT_FIND;
		}

		const std::string selectedImageRef = rows.front()[0];

		// 지정한 imageId를 대표(status=1)로, 기존 대표는 해제(status=0)로 한 문장에 바꾼다.
		if( !sql.Exec(_T("UPDATE user_profile_images SET status = CASE WHEN image_id = ? THEN 1 ELSE 0 END ")
			_T("WHERE user_public_id = ? AND (status = 1 OR image_id = ?)"),
			{ imageIdText, publicIdHex, imageIdText }) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, req->imageId, selectedImageRef);
		return EDBReturnType::OK;
	}

	//***************************************************************************
	// @brief 갤러리에서 이미지 하나를 삭제합니다. 삭제 "전"의 image_ref/대표
	// 여부를 onComplete로 돌려줘서, 호출부(ChatProfileImageHandler.cpp 계열)가
	// 필요하면 실제 파일 삭제/세션 값 갱신까지 이어갈 수 있게 한다 — 이
	// 핸들러 자신은 DB 행만 지우고 파일 시스템은 건드리지 않는다.
	//***************************************************************************
	EDBReturnType HandleDelete(ST_DELETE_PROFILE_IMAGE_REQ* req, OdbcConnGuard& guard)
	{
		const std::string publicIdHex = PublicIdToHex(ToPublicId(req->publicId));
		const std::string imageIdText = std::to_string(req->imageId);

		auto fail = [req]()
			{
				if( req->onComplete )
					req->onComplete(ELoginResult::DbError, req->imageId, false, std::string());
			};

		if( guard == nullptr )
		{
			fail();
			return EDBReturnType::INVALID;
		}
		DbUtil::CSql sql(guard);

		// 1) 삭제 전에 image_ref/status를 먼저 조회 — 소유자 검증도 겸함(WHERE에
		//    user_public_id 포함 — 남의 이미지 정보를 훔쳐볼 수 없음).
		std::vector<DbUtil::Row> rows;
		if( !sql.Query(_T("SELECT image_ref, status FROM user_profile_images WHERE image_id = ? AND user_public_id = ?"),
			{ imageIdText, publicIdHex }, 2, rows) )
		{
			// 조회 실행 자체가 실패한 것은 "지울 게 없음"이 아니라 DB 오류다.
			fail();
			return EDBReturnType::INVALID;
		}

		if( rows.empty() )
		{
			// 존재하지 않거나 내 것이 아님 — "지울 게 없음"으로 처리.
			if( req->onComplete )
				req->onComplete(ELoginResult::AccountNotFound, req->imageId, false, std::string());
			return EDBReturnType::FETCH_NOT_FIND;
		}

		const std::string imageRef = rows.front()[0];
		const bool wasActive = (DbUtil::ToI64(rows.front()[1]) != 0);

		// 2) 실제 삭제
		if( !sql.Exec(_T("DELETE FROM user_profile_images WHERE image_id = ? AND user_public_id = ?"),
			{ imageIdText, publicIdHex }) )
		{
			fail();
			return EDBReturnType::INVALID;
		}

		if( req->onComplete )
			req->onComplete(ELoginResult::Ok, req->imageId, wasActive, imageRef);
		return EDBReturnType::OK;
	}

}

//***************************************************************************
// @brief 프로필 이미지 DB 요청 하나를 연산별 함수로 나눠 처리한다.
// @details 커넥션을 못 얻으면 로그는 여기서 한 번 남기고, 각 연산 함수가 자기 콜백으로
//          DbError를 돌려준다(콜백 모양이 연산마다 다르기 때문).
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_ProfileImage)
{
	ST_PROFILE_IMAGE_REQ_BASE* base = static_cast<ST_PROFILE_IMAGE_REQ_BASE*>(pStAsync);

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
		LOG_ERROR(_T("kDbCallIdent_ProfileImage: No available ODBC connection in pool."));

	switch( base->op )
	{
	case EProfileImageOp::Set:		return HandleSet(static_cast<ST_SET_PROFILE_IMAGE_URL_REQ*>(base), guard);
	case EProfileImageOp::List:		return HandleList(static_cast<ST_LIST_PROFILE_IMAGES_REQ*>(base), guard);
	case EProfileImageOp::Select:	return HandleSelect(static_cast<ST_SELECT_PROFILE_IMAGE_REQ*>(base), guard);
	case EProfileImageOp::Delete:	return HandleDelete(static_cast<ST_DELETE_PROFILE_IMAGE_REQ*>(base), guard);
	}

	LOG_ERROR(_T("kDbCallIdent_ProfileImage: unknown op %d"), static_cast<int>(base->op));
	return EDBReturnType::INVALID;
}