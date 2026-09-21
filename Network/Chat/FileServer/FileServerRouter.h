
//***************************************************************************
// FileServerRouter.h : interface for the CFileServerRouter class.
//
//***************************************************************************

#ifndef UC_FILESERVERROUTER_H
#define UC_FILESERVERROUTER_H

#include "FileServerSession.h"
#include "FileUploadHandler.h"
#include "FileDownloadHandler.h"
#include "FileDeleteHandler.h"
#include <Network/HTTP/HttpPacketBuilder.h>
#include <Network/HTTP/HttpRequestParser.h>

#include <memory>
#include <string>

class CFileServerSession;

//***************************************************************************
// @class CFileServerRouter
// @brief 완성된 HTTP 요청 하나를 메서드+경로로 보고 알맞은 핸들러에
//        위임한다. 매칭되는 라우트가 없으면 404를 돌려준다.
//***************************************************************************
class CFileServerRouter
{
public:
	CFileServerRouter(
		IFileStorage* storage,
		CRedisService* redisService,
		CFileMetadataRepository* metadataRepo,
		std::string publicBaseUrl,
		int64 maxUploadBytes);

	//***************************************************************************
	// @brief 완성된 HTTP 요청 하나를 라우팅합니다(CFileServerSession::OnRecv()가
	//        CFileServerMain::HandleRequest()를 거쳐 호출).
	//***************************************************************************
	void HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request);

private:
	static void SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

private:
	CFileUploadHandler		_uploadHandler;
	CFileDownloadHandler	_downloadHandler;
	CFileDeleteHandler		_deleteHandler;
};

#endif // ndef UC_FILESERVERROUTER_H