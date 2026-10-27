
//***************************************************************************
// FileServerRouter.cpp: implementation of the CFileServerRouter class.
//
//***************************************************************************

#include "pch.h"
#include "../ChatCommon/ImageRef.h"
#include "FileServerRouter.h"

CFileServerRouter::CFileServerRouter(
	IFileStorage* storage,
	CRedisService* redisService,
	std::string publicBaseUrl)
	: _uploadHandler(storage, redisService, publicBaseUrl)
	, _downloadHandler(storage)
{
}

//***************************************************************************
// @brief 완성된 HTTP 요청 하나를 메서드+경로로 라우팅합니다.
//***************************************************************************
void CFileServerRouter::HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request)
{
	const std::string& method = request.GetMethod();
	const std::string_view path = request.GetPath();
	const bool keepAlive = request.IsKeepAlive();

	constexpr std::string_view kImagesPrefix = ImageRef::kImagesPath;
	const bool isImagesPath = path.size() > kImagesPrefix.size() && path.compare(0, kImagesPrefix.size(), kImagesPrefix) == 0;

	// [설계] 삭제 라우트는 없다 — 파일 삭제는 채팅 서버가 소유자 검증 후
	// Redis 예약 삭제 큐에 적고 파일 서버가 소비한다(인증 없는 HTTP 삭제 제거).
	if( method == "POST" && path == "/upload" )
	{
		_uploadHandler.Handle(session, request, keepAlive);
	}
	else if( method == "GET" && isImagesPath )
	{
		const std::string relativePath(path.substr(kImagesPrefix.size()));
		_downloadHandler.Handle(session, request, relativePath, keepAlive);
	}
	else
	{
		if( session != nullptr )
			session->SendSimple(404, "Not Found", "text/plain", "404 Not Found", keepAlive);
	}
}