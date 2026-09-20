
//***************************************************************************
// FileServerRouter.cpp: implementation of the CFileServerRouter class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerRouter.h"
#include "FileServerSession.h"
#include <Network/HTTP/HttpPacketBuilder.h>

CFileServerRouter::CFileServerRouter(
	IFileStorage* storage,
	CRedisService* redisService,
	CFileMetadataRepository* metadataRepo,
	std::string publicBaseUrl,
	int64 maxUploadBytes,
	int32 maxProfileImageDimension)
	: _uploadHandler(storage, redisService, metadataRepo, publicBaseUrl, maxUploadBytes, maxProfileImageDimension)
	, _downloadHandler(storage)
	, _deleteHandler(storage, metadataRepo)
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

	constexpr std::string_view kImagesPrefix = "/images/";

	if( method == "POST" && path == "/upload" )
	{
		_uploadHandler.Handle(session, request, keepAlive);
	}
	else if( method == "GET" && path.size() > kImagesPrefix.size() && path.compare(0, kImagesPrefix.size(), kImagesPrefix) == 0 )
	{
		const std::string relativePath(path.substr(kImagesPrefix.size()));
		_downloadHandler.Handle(session, request, relativePath, keepAlive);
	}
	else if( method == "DELETE" && path.size() > kImagesPrefix.size() && path.compare(0, kImagesPrefix.size(), kImagesPrefix) == 0 )
	{
		const std::string relativePath(path.substr(kImagesPrefix.size()));
		_deleteHandler.Handle(session, relativePath, keepAlive);
	}
	else
	{
		SendSimpleResponse(session, 404, "Not Found", "text/plain", "404 Not Found", keepAlive);
	}
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(매칭 실패 404 전용).
// @details [수정] CHttpResponseBuilder(HttpPacketBuilder.h)로 교체 — 직접
//          "HTTP/1.1 " + ... 문자열을 이어붙이던 방식보다 헤더 종료 빈 줄/
//          Content-Length 계산 등을 이 빌더가 대신 처리해준다. contentType/
//          body/statusText는 전부 이 함수의 매개변수(호출 스택에 살아있는
//          진짜 변수)라서 AddHeader()/SetBody()가 참조하는 string_view가
//          Build() 호출 시점까지 안전하게 유효하다 — 만약 std::to_string()
//          같은 임시값을 그 자리에서 바로 넘기면 댕글링되므로 주의(CFileDownloadHandler
//          쪽처럼 로컬 std::string 변수에 먼저 담아둬야 함).
//***************************************************************************
void CFileServerRouter::SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
	const std::string& contentType, const std::string& body, bool keepAlive)
{
	if( session == nullptr )
		return;

	CHttpResponseBuilder builder;
	builder.SetStatus(statusCode, statusText)
		.AddHeader("Content-Type", contentType)
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close")
		.SetBody(body);

	auto [data, len] = builder.Build();
	session->SendRaw(std::string(data, len));
}