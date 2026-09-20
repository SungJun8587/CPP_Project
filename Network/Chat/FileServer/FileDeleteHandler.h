
//***************************************************************************
// FileDeleteHandler.h : interface for the CFileDeleteHandler class.
//
// [설계 — Router/Handler 분리] CFileServerMain::HandleDeleteImage()에 있던
// 로직을 그대로 옮겼다. IImageStorage -> IFileStorage로 교체했고, 삭제
// 성공 시 CFileMetadataRepository에서도 항목을 제거하도록 추가했다.
//***************************************************************************

#ifndef UC_FILEDELETEHANDLER_H
#define UC_FILEDELETEHANDLER_H

#include "FileStorage.h"
#include "FileMetadataRepository.h"
#include "FileServerSession.h"
#include <Network/HTTP/HttpPacketBuilder.h>

#include <memory>
#include <string>

class CFileServerSession;

//***************************************************************************
// @class CFileDeleteHandler
// @brief DELETE /images/{path} 처리 전담 핸들러.
// @details [설계] 이 요청 자체엔 별도 인증이 없다 — 채팅 서버가
//          DeleteProfileImageHandler.cpp에서 DB 삭제에 성공한 뒤에만
//          내부적으로 호출하는 것을 전제로 한다(사용자가 직접 이
//          엔드포인트를 두드릴 경로가 없음 — 클라이언트 프로토콜엔 이
//          요청이 아예 없다). 인터넷에 노출되는 배포라면 두 서버 사이에서만
//          통하는 별도 인증(공유 비밀 헤더 등)을 추가하는 게 안전하다 —
//          지금은 데모 범위에서 생략(FileServerMain.h의 기존 주석과 동일한
//          한계).
//***************************************************************************
class CFileDeleteHandler
{
public:
	CFileDeleteHandler(IFileStorage* storage, CFileMetadataRepository* metadataRepo);

	//***************************************************************************
	// @brief DELETE /images/{path} 요청 하나를 처리한다.
	//***************************************************************************
	void Handle(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive);

private:
	//***************************************************************************
	// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다.
	//***************************************************************************
	static void SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

private:
	IFileStorage* _storage;		// CFileServerMain 소유 — 빌려 씀(수명 비소유)
	CFileMetadataRepository* _metadataRepo;	// 위와 동일
};

#endif // ndef UC_FILEDELETEHANDLER_H