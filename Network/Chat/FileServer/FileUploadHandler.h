
//***************************************************************************
// FileUploadHandler.h : interface for the CFileUploadHandler class.
//
// POST /upload 처리: 업로드 토큰(Redis, 1회용)은 token 파트가 끝나는 즉시
// 원자적으로 소모되고(세션이 VerifyUploadToken 호출), 무효면 본문을 더 받기 전에
// 거부한다. 이 핸들러는 스트리밍으로 받아둔 임시 파일을 그 인증 결과(소유자)와
// 함께 IFileStorage에 저장하고 공개 URL을 응답한다.
//***************************************************************************

#ifndef FILEUPLOADHANDLER_H
#define FILEUPLOADHANDLER_H

#include "FileStorage.h"
#include "FileServerSession.h"
#include <Network/HTTP/HttpPacketBuilder.h>
#include <Network/HTTP/HttpRequestParser.h>
#include <Redis/RedisService.h>
#include <Redis/RedisResultSet.h>

#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>

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
		std::string publicBaseUrl);

	//***************************************************************************
	// @brief POST /upload 요청 하나를 처리한다.
	//***************************************************************************
	void Handle(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, bool keepAlive);

	//***************************************************************************
	// @brief Redis에서 업로드 토큰을 소모합니다(GET+DEL 원자적, 1회용).
	// @param onComplete valid==true면 ownerPublicIdHex가 채워진다. Redis 콜백
	//        스레드(CJobQueue)에서 호출된다. 세션이 토큰 파트 직후 호출한다.
	//***************************************************************************
	void VerifyUploadToken(const std::string& tokenHex, std::function<void(bool valid, const std::string& ownerPublicIdHex)> onComplete);

private:
	//***************************************************************************
	// @brief 토큰 소모용 Lua 스크립트를 Redis에 올려(SCRIPT LOAD) EVALSHA로 쓸 수 있게
	//        한다. 첫 소모는 EVAL로 처리하고, 적재가 끝나면 이후부터 SHA만 보낸다.
	//***************************************************************************
	void EnsureConsumeScriptLoaded();

	//***************************************************************************
	// @brief relativePath로 접근 가능한 공개 URL을 만든다.
	//***************************************************************************
	std::string BuildPublicUrl(const std::string& relativePath) const;


private:
	IFileStorage* _storage;		// CFileServerMain 소유 — 빌려 씀(수명 비소유)
	CRedisService* _redisService;	// 위와 동일

	std::string	_publicBaseUrl;

	std::mutex			_scriptMutex;
	std::string			_consumeScriptSha;		// 적재된 스크립트의 SHA1(비어있으면 아직 없음)
	std::atomic<bool>	_scriptLoading{ false };
};

#endif // ndef FILEUPLOADHANDLER_H