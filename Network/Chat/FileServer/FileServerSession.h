
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

#ifndef FILESERVERSESSION_H
#define FILESERVERSESSION_H

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
#include <atomic>
#include <mutex>
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
	virtual void	OnSend(int32 len) override;

public:
	//***************************************************************************
	// @brief 완성된 HTTP 응답 문자열(상태줄+헤더+바디)을 그대로 전송합니다.
	//***************************************************************************
	void SendRaw(const char* data, size_t len);
	void SendRaw(const std::string& data) { SendRaw(data.data(), data.size()); }

	//***************************************************************************
	// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(모든 핸들러 공용).
	//***************************************************************************
	void SendSimple(int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

	//***************************************************************************
	// @brief [추가] 방금 완료된 요청이 스트리밍 업로드였다면 그 결과를 반환한다.
	//        FileUploadHandler::Handle()이 request.GetBody() 대신 이걸 쓴다.
	// @details HandleRequest() 호출 시점(=이번 요청이 막 Complete된 직후)에만
	//          유효한 값이 들어있다 — 다음 요청을 위해 곧이어 초기화된다.
	//***************************************************************************
	const SPendingUploadResult& GetPendingUploadResult() const { return _pendingUpload; }

	//***************************************************************************
	// @brief 이번 요청의 업로드 토큰 인증 결과를 받는다.
	// @details 토큰은 토큰 파트가 끝나는 즉시 Redis에서 원자적으로 소모(GET+DEL)되고, 그 결과가 요청
	//          단위로 보관된다. 결과가 이미 나왔으면 onResult를 바로 호출하고, 아직이면 도착하는 즉시
	//          Redis 콜백 스레드에서 호출한다. 인증이 시작되지 않은 요청이면 실패로 바로 호출한다.
	//          세션이 먼저 끊기면 대기 중인 onResult는 호출되지 않고 버려진다.
	// @param onResult valid==true면 ownerPublicIdHex(토큰을 발급받은 사용자)가 채워진다.
	//***************************************************************************
	void WaitUploadAuth(std::function<void(bool valid, const std::string& ownerPublicIdHex)> onResult);

	//***************************************************************************
	// @brief 파일 본문 전송을 시작한다 — 헤더는 호출 전에 SendRaw()로 이미 보냈다.
	// @details 송신 완료(OnSend)마다 미전송 바이트가 상한 아래로 내려오면 다음
	//          청크를 읽어 보낸다 — 느린 클라이언트여도 메모리에 쌓이는 양이
	//          상한으로 묶이고, IOCP 워커가 파일 전체를 읽느라 점유되지 않는다.
	//          keepAlive가 false면 마지막 바이트가 나간 뒤 연결을 닫는다.
	// @param stream 이미 Seek(start)된 상태의 열린 스트림.
	// @param length 보낼 바이트 수.
	//***************************************************************************
	void BeginDownload(std::unique_ptr<CFileStream> stream, int64_t length, bool keepAlive);

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

	//***************************************************************************
	// @brief 업로드를 거부한다 — 이후 본문을 버리고 연결을 끊는다.
	//***************************************************************************
	void RejectUpload(const TCHAR* reason);

	//***************************************************************************
	// @brief 열려있는 업로드 임시 파일을 닫고 지운다(여러 번 호출해도 안전).
	//***************************************************************************
	void CleanupTempUpload();

	//***************************************************************************
	// @brief 진행 중인 다운로드가 있으면 상한까지 다음 청크를 읽어 보낸다.
	//***************************************************************************
	void PumpDownload();

	//***************************************************************************
	// @brief 연결 종료 예약(_closeAfterSend)이 있고 보낼 것이 모두 나갔으면 끊는다.
	//***************************************************************************
	void TryCloseAfterSend();

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
	std::string				_currentSmallFieldValue;	// 위 필드의 누적 값("token"만 보관, 길이 상한 있음)
	std::atomic<bool>		_uploadRejected{ false };	// 토큰 누락/무효 등으로 이 업로드를 거부했는지

	// 업로드 토큰 인증 상태 — 요청 1건 한정. Redis 콜백 스레드와 공유하므로 요청마다 shared_ptr로 새로 만든다
	// (다음 요청이 시작돼도 이전 요청의 콜백은 자기 상태에만 쓴다).
	struct SUploadAuth
	{
		enum class EState { Pending, Valid, Invalid };

		std::mutex		mutex;
		EState			state = EState::Pending;
		std::string		owner;				// Valid일 때 토큰 발급 대상 사용자(public_id 16진)
		bool			requestComplete = false;	// 요청 본문 수신이 끝났는지(끝났다면 응답은 Handle이 책임진다)
		std::function<void(bool, const std::string&)>	waiter;	// 결과를 기다리는 최종 처리
	};
	std::shared_ptr<SUploadAuth>	_auth;

	// 다운로드 송신 상태 — 한 세션에서 한 번에 하나만 진행(HTTP/1.1 순차 처리).
	struct SDownloadState
	{
		std::unique_ptr<CFileStream>	stream;
		int64_t							remaining = 0;
		bool							keepAlive = true;
		std::string						chunk;	// 읽기 버퍼(재사용)
	};
	std::mutex					_downloadMutex;
	std::unique_ptr<SDownloadState>	_download;
	std::atomic<int64_t>		_inflightBytes{ 0 };		// SendRaw로 큐잉했지만 아직 송신 완료 통지가 안 온 바이트
	std::atomic<bool>			_closeAfterSend{ false };	// Connection: close — 다 나간 뒤 끊기
};

#endif // ndef FILESERVERSESSION_H