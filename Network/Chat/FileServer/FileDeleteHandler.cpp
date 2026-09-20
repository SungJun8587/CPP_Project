
//***************************************************************************
// FileDeleteHandler.cpp: implementation of the CFileDeleteHandler class.
//
//***************************************************************************

#include "pch.h"
#include "FileDeleteHandler.h"

CFileDeleteHandler::CFileDeleteHandler(IFileStorage* storage, CFileMetadataRepository* metadataRepo)
	: _storage(storage)
	, _metadataRepo(metadataRepo)
{
}

//***************************************************************************
// @brief DELETE /images/{path} 처리.
//***************************************************************************
void CFileDeleteHandler::Handle(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive)
{
	if( _storage == nullptr )
	{
		SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Storage not initialized", keepAlive);
		return;
	}

	// IFileStorage::DeleteFile() 문서 참고 — 파일이 애초에 없었던 경우도
	// 성공으로 취급한다. 경로 조작 시도("../")만 별도로 실패 처리된다.
	if( !_storage->DeleteFile(relativePath) )
	{
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Invalid path", keepAlive);
		return;
	}

	// [추가] 파일 삭제가 성공(또는 애초에 없었음)했으면 메타데이터도 같이
	// 지운다 — DeleteFile()과 마찬가지로 애초에 없던 항목을 지워도 안전(no-op).
	if( _metadataRepo != nullptr )
		_metadataRepo->Remove(relativePath);

	SendSimpleResponse(session, 200, "OK", "text/plain", "deleted", keepAlive);
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다.
//***************************************************************************
void CFileDeleteHandler::SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
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