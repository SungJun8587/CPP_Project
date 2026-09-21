
//***************************************************************************
// FileUploadHandler.h : interface for the CFileUploadHandler class.
//
// [설계 — Router/Handler 분리] CFileServerMain::HandleUpload()에 있던 로직을
// 그대로 옮겼다. 동작은 동일하고, 이번에 두 가지가 추가됐다:
//     1. IImageStorage/CLocalFileImageStorage -> IFileStorage/CLocalFileStorage
//        (범용화된 이름으로 교체 — SaveImage() -> SaveFile()).
//     2. 저장 성공 시 CFileMetadataRepository에도 등록 — 원본 파일명/크기/
//        Content-Type/업로드 시각을 서버가 기억하게 됐다(이전에는 클라이언트가
//        메시지 텍스트로만 들고 있었음).
//***************************************************************************

#ifndef UC_FILEUPLOADHANDLER_H
#define UC_FILEUPLOADHANDLER_H

#include "FileStorage.h"
#include "FileMetadataRepository.h"
#include "FileServerSession.h"
#include <Network/HTTP/HttpPacketBuilder.h>
#include <Network/HTTP/HttpRequestParser.h>
#include <Redis/RedisService.h>
#include <Redis/RedisResultSet.h>

#include <memory>
#include <string>
#include <functional>

class CFileServerSession;

//***************************************************************************
// @class CFileUploadHandler
// @brief POST /upload(multipart/form-data: token+file) 처리 전담 핸들러.
// @details 의존성(storage/redis/metadataRepo)은 전부 CFileServerMain이
//          소유한 것을 포인터로만 빌려 쓴다 — 이 핸들러 자체는 그 수명을
//          소유하지 않는다(CFileServerRouter/CFileServerMain보다 항상
//          먼저 소멸되므로 안전).
//***************************************************************************
class CFileUploadHandler
{
public:
	CFileUploadHandler(
		IFileStorage* storage,
		CRedisService* redisService,
		CFileMetadataRepository* metadataRepo,
		std::string publicBaseUrl,
		int64 maxUploadBytes);

	//***************************************************************************
	// @brief POST /upload 요청 하나를 처리한다.
	//***************************************************************************
	void Handle(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, bool keepAlive);

private:
	//***************************************************************************
	// @brief Redis에서 업로드 토큰을 조회/소모(1회용 — 검증 성공 시 삭제)합니다.
	// @param onComplete success==true면 outOwnerPublicIdHex가 채워진다.
	//***************************************************************************
	void VerifyAndConsumeUploadToken(const std::string& tokenHex, std::function<void(bool success, const std::string& ownerPublicIdHex)> onComplete);

	//***************************************************************************
	// @brief relativePath로 접근 가능한 공개 URL을 만든다.
	//***************************************************************************
	std::string BuildPublicUrl(const std::string& relativePath) const;

	//***************************************************************************
	// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다.
	//***************************************************************************
	static void SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

private:
	IFileStorage* _storage;		// CFileServerMain 소유 — 빌려 씀(수명 비소유)
	CRedisService* _redisService;	// 위와 동일
	CFileMetadataRepository* _metadataRepo;	// 위와 동일

	std::string	_publicBaseUrl;
	int64		_maxUploadBytes;
};

#endif // ndef UC_FILEUPLOADHANDLER_H