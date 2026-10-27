
//***************************************************************************
// FileServerRouter.h : interface for the CFileServerRouter class.
//
//***************************************************************************

#ifndef FILESERVERROUTER_H
#define FILESERVERROUTER_H

#include "FileServerSession.h"
#include "FileUploadHandler.h"
#include "FileDownloadHandler.h"
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
		std::string publicBaseUrl);

	//***************************************************************************
	// @brief 완성된 HTTP 요청 하나를 라우팅합니다(CFileServerSession::OnRecv()가
	//        CFileServerMain::HandleRequest()를 거쳐 호출).
	//***************************************************************************
	void HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request);

	//***************************************************************************
	// @brief 업로드 토큰 확인(CFileUploadHandler::VerifyUploadToken 참고).
	//***************************************************************************
	void VerifyUploadToken(const std::string& tokenHex, std::function<void(bool valid, const std::string& ownerPublicIdHex)> onComplete)
	{
		_uploadHandler.VerifyUploadToken(tokenHex, std::move(onComplete));
	}

private:
	CFileUploadHandler		_uploadHandler;
	CFileDownloadHandler	_downloadHandler;
};

#endif // ndef FILESERVERROUTER_H