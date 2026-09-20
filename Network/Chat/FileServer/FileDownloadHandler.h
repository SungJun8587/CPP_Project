
//***************************************************************************
// FileDownloadHandler.h : interface for the CFileDownloadHandler class.
//
// [설계 — Router/Handler 분리] CFileServerMain::HandleGetImage()에 있던
// 로직(Range 지원 포함)을 그대로 옮겼다. IImageStorage -> IFileStorage로
// 교체한 것 외에 동작 변화는 없다.
//***************************************************************************

#ifndef UC_FILEDOWNLOADHANDLER_H
#define UC_FILEDOWNLOADHANDLER_H

#include "FileStorage.h"
#include "FileSendBufferPool.h"
#include "FileServerSession.h"
#include <Network/HTTP/HttpRange.h>
#include <Network/HTTP/HttpPacketBuilder.h>
#include <Network/HTTP/HttpRequestParser.h>

#include <algorithm>
#include <memory>
#include <string>

class CFileServerSession;

//***************************************************************************
// @class CFileDownloadHandler
// @brief GET /images/{path} 처리 전담 핸들러. Range 헤더가 있으면 206
//        Partial Content로, 없거나 처리 못 하는 형태면 200 OK 전체 응답으로
//        돌려준다(HttpRange.h로 요청 헤더 파싱, CHttpResponseBuilder로
//        응답 조립 — HttpPacketBuilder.h 참고).
//***************************************************************************
class CFileDownloadHandler
{
public:
	explicit CFileDownloadHandler(IFileStorage* storage);

	//***************************************************************************
	// @brief GET /images/{path} 요청 하나를 처리한다.
	//***************************************************************************
	void Handle(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, const std::string& relativePath, bool keepAlive);

private:
	//***************************************************************************
	// @brief 확장자(".png" 등)로부터 Content-Type을 추정한다. 모르는
	//        확장자면 "application/octet-stream".
	//***************************************************************************
	static std::string GuessContentType(const std::string& path);

	//***************************************************************************
	// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(에러 응답 전용).
	//***************************************************************************
	static void SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

	//***************************************************************************
	// @brief 요청 Range가 파일 크기를 벗어나는 등 처리 불가능할 때 416
	//        Range Not Satisfiable을 보낸다.
	//***************************************************************************
	static void SendRangeNotSatisfiable(std::shared_ptr<CFileServerSession> session, int64_t totalSize, bool keepAlive);

private:
	IFileStorage* _storage;	// CFileServerMain 소유 — 빌려 씀(수명 비소유)
};

#endif // ndef UC_FILEDOWNLOADHANDLER_H