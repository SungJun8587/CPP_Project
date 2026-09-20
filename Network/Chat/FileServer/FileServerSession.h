
//***************************************************************************
// FileServerSession.h : interface for the CFileServerSession class.
//
// [수정 — 대용량 업로드 스트리밍] HttpRequestParser.h에 새로 생긴
// SetHeadersCompleteCallback()/SetBodyStreamCallback()과 MultipartStreamParser.h
// (CMultipartStreamParser)를 여기서 실제로 엮는다. POST /upload 요청으로
// 판단되면(생성자에서 건 콜백이 매 요청 헤더 완료 시점마다 확인) 본문을
// m_body에 통째로 쌓는 대신, "file" 파트는 임시 파일에 바로 쓰고 "token"
// 같은 작은 필드만 메모리에 유지한다 — 결과는 SPendingUploadResult에
// 담겨 FileUploadHandler가 request.GetBody() 대신 이걸 갖다 쓴다.
//***************************************************************************

#ifndef UC_FILESERVERSESSION_H
#define UC_FILESERVERSESSION_H

#include "FileServerMain.h"
#include <Network/IOCP/IocpSession.h>
#include <Network/HTTP/HttpRequestParser.h>
#include <Network/HTTP/MultipartFormParser.h>
#include <Network/HTTP/MultipartStreamParser.h>
#include <Crypto/CryptoUtil.h>

#include <filesystem>
#include <string>
#include <memory>
#include <fstream>
#include <cstdint>

class CFileServerMain;

//***************************************************************************
// @struct SPendingUploadResult
// @brief 스트리밍 업로드 파싱 결과 — CFileServerSession이 채우고,
//        FileUploadHandler가 소비한다.
//***************************************************************************
struct SPendingUploadResult
{
	bool	attempted = false;			// 이번 요청이 POST /upload + 유효한 boundary로 판단돼 스트리밍을 시도했는지
	bool	multipartOk = false;		// CMultipartStreamParser가 끝까지 형식 오류 없이 파싱을 마쳤는지(IsDone())

	bool	hasFile = false;
	std::string	tempFilePath;			// "file" 파트가 저장된 임시 파일의 전체 경로(hasFile==true일 때만 유효)
	std::string	originalFileName;
	std::string	fileContentType;
	int64_t	fileSizeBytes = 0;

	bool	hasToken = false;
	std::string	tokenValue;
};

//***************************************************************************
// @class CFileServerSession
// @brief CIocpSession을 상속받는 파일 서버 전용 HTTP 세션.
// @details 채팅 서버(CChatSession)와 달리 고정 크기 패킷 프레이밍이 아니라
// CHttpRequestParser로 증분 파싱한다 — OnRecv()가 들어온 바이트를 그대로
// Feed()에 흘려보내고, Complete가 되면 CFileServerMain에 라우팅을 위임한
// 뒤 Reset()해서 같은 연결(keep-alive)에서 다음 요청을 받을 준비를 한다.
//***************************************************************************
class CFileServerSession : public CIocpSession
{
public:
	explicit CFileServerSession(CFileServerMain* server);
	virtual ~CFileServerSession() = default;

protected:
	// CIocpSession의 상위 콘텐츠 레이어 훅 오버라이드
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

public:
	//***************************************************************************
	// @brief 완성된 HTTP 응답 문자열(상태줄+헤더+바디)을 그대로 전송합니다.
	//***************************************************************************
	void SendRaw(const std::string& data);

	//***************************************************************************
	// @brief [추가] 방금 완료된 요청이 스트리밍 업로드였다면 그 결과를 반환한다.
	//        FileUploadHandler::Handle()이 request.GetBody() 대신 이걸 쓴다.
	// @details HandleRequest() 호출 시점(=이번 요청이 막 Complete된 직후)에만
	//          유효한 값이 들어있다 — 다음 요청을 위해 곧이어 초기화된다.
	//***************************************************************************
	const SPendingUploadResult& GetPendingUploadResult() const { return _pendingUpload; }

private:
	//***************************************************************************
	// @brief _parser의 HeadersCompleteCallback — 매 요청 헤더가 다 파싱된
	//        직후(본문이 오기 전) 호출된다. POST /upload + 유효한 boundary면
	//        스트리밍 모드로 전환한다.
	//***************************************************************************
	void SetupUploadStreamingIfNeeded(CHttpRequestParser& parser);

	//***************************************************************************
	// @brief _parser의 BodyStreamCallback — 본문 바이트가 도착할 때마다
	//        _multipartParser로 그대로 넘긴다.
	//***************************************************************************
	void OnUploadBodyChunk(const char* data, size_t len);

	// _multipartParser의 콜백 세 개.
	void OnMultipartPartBegin(const HTTP::SMultipartPartInfo& info);
	void OnMultipartPartData(const char* data, size_t len);
	void OnMultipartPartEnd();

private:
	CFileServerMain* _server = nullptr;
	CHttpRequestParser	_parser;

	// [추가] 대용량 업로드 스트리밍 상태 — 전부 요청 1건 한정(다음 요청
	// 전에 초기화됨).
	std::unique_ptr<HTTP::CMultipartStreamParser>	_multipartParser;
	SPendingUploadResult	_pendingUpload;
	std::ofstream			_uploadFileStream;		// hasFile 파트를 쓰는 중인 임시 파일 스트림
	bool					_currentPartIsFile = false;	// 지금 열려있는 멀티파트 파트가 "file"인지
	std::string				_currentSmallFieldName;		// 파일이 아니면 그 파트의 필드 이름("token" 등)
	std::string				_currentSmallFieldValue;	// 위 필드의 누적 값
};

#endif // ndef UC_FILESERVERSESSION_H