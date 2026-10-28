
//***************************************************************************
// FileServerMain.h : interface for the CFileServerMain class.
//
//***************************************************************************

#ifndef FILESERVERMAIN_H
#define FILESERVERMAIN_H

#include <ServerConnectInfo.h>
#include <Network/NetworkCommon.h>
#include <Redis/RedisService.h>
#include <Network/HTTP/HttpRequestParser.h>
#include <Redis/RedisResultSet.h>
#include "FileServerSession.h"
#include "FileStorage.h"
#include "LocalFileStorage.h"
#include "FileServerRouter.h"
#include "FileServerOptions.h"

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

class CFileServerSession;

//***************************************************************************
// @class CFileServerMain
// @brief 파일 업로드/서빙/예약 삭제를 전담하는 파일 서버 파사드.
// @details 채팅 서버(CChatServerMain)와 같은 인프라(CIocpCore/
// CIocpServerService/CRedisService)를 재사용하고, 세션만 HTTP를 해석하는
// CFileServerSession이다.
//
// 채팅 서버와는 직접 통신하지 않고 같은 Redis만 바라본다(키 규약은
// Common/FileServerKeys.h):
//   - 업로드 토큰: 채팅 서버가 발급한 1회용 토큰을 업로드 시 조회·소모해
//     소유 계정(public_id)을 알아낸다.
//   - 삭제 예약: 채팅 서버가 소유권을 확인한 뒤 큐에 넣은 상대 경로를
//     전용 스레드가 꺼내 지운다. 삭제 실패 항목은 실패 목록에 보관한다.
// DB는 채팅 서버만 쓴다 — 이 서버는 업로드 결과를 URL로 돌려줄 뿐이다.
//***************************************************************************
class CFileServerMain
{
public:
	CFileServerMain() = default;
	virtual ~CFileServerMain();

	//***************************************************************************
	// @brief 파일 서버 구동.
	// @param options 구동 옵션(FileServerOptions.h) — 바인드 주소, 세션/스레드 수,
	//        유휴 타임아웃, Redis 노드, 저장 경로(하위 ".tmp"는 업로드 임시 파일용 —
	//        같은 볼륨이라 저장 시 rename 한 번으로 끝난다), 공개 기본 URL
	//        (이 뒤에 "/images/{path}"를 붙인다), 업로드 최대 크기.
	// @return 모든 초기화 단계가 성공하면 true
	//***************************************************************************
	bool Start(const SFileServerOptions& options);

	void Stop();

	//***************************************************************************
	// @brief 완성된 HTTP 요청 하나를 라우팅합니다(CFileServerSession::OnRecv()에서 호출).
	//***************************************************************************
	void HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request);

	//***************************************************************************
	// @brief 설정된 업로드 크기 상한(바이트)을 반환합니다 — 스트리밍 모드의
	//        상한이 실제 설정값(MaxUploadBytes)과 항상 일치하도록 단일 진실
	//        공급원을 유지하기 위함이다.
	//***************************************************************************
	int64 GetMaxUploadBytes() const { return _maxUploadBytes; }

	//***************************************************************************
	// @brief 업로드 토큰을 확인합니다(CFileUploadHandler::VerifyUploadToken 참고).
	//***************************************************************************
	void VerifyUploadToken(const std::string& tokenHex, std::function<void(bool valid, const std::string& ownerPublicIdHex)> onComplete)
	{
		if( _router != nullptr )
			_router->VerifyUploadToken(tokenHex, std::move(onComplete));
		else if( onComplete )
			onComplete(false, std::string());
	}

	//***************************************************************************
	// @brief 업로드 임시 파일을 만들 디렉터리(저장소와 같은 볼륨)를 반환합니다.
	//***************************************************************************
	const std::string& GetUploadTempDir() const { return _uploadTempDir; }

private:
	//***************************************************************************
	// @brief 예약 삭제 소비 스레드를 시작합니다.
	//***************************************************************************
	void StartPendingDeletionThread();

	//***************************************************************************
	// @brief 스레드 본체 — 큐가 비어있지 않으면 대기 없이 연속으로 비우고,
	//        비면 kPollingIntervalSec 동안(Stop 시 즉시 깨어남) 대기한다.
	//***************************************************************************
	void PendingDeletionLoop();

	//***************************************************************************
	// @brief 큐에서 하나를 꺼냅니다(LPOP, 응답을 최대 kRedisWaitSec까지 대기).
	// @return 꺼냈으면 true(outRelativePath 채움), 비었거나 실패면 false.
	//***************************************************************************
	bool PopPendingDeletion(std::string& outRelativePath);

	//***************************************************************************
	// @brief 파일을 지우고 성공이면 로그, 실패면 실패 목록(RPUSH)에 남깁니다.
	//        큐에는 되돌리지 않는다(핫루프 방지).
	//***************************************************************************
	void ProcessDeletion(const std::string& relativePath);

private:
	static constexpr int kPollingIntervalSec = 5;
	static constexpr int kRedisWaitSec = 10;

	std::atomic<bool>		_stopPolling{ false };
	std::thread				_pollThread;
	std::mutex				_pollMutex;
	std::condition_variable	_pollCv;

private:
	CIocpCoreRef				_iocpCore;
	CIocpServerServiceRef		_service;
	CJobQueueRef				_jobQueue;
	std::unique_ptr<CRedisService>			_redisService;
	std::unique_ptr<IFileStorage>			_fileStorage;
	std::unique_ptr<CFileServerRouter>		_router;

	std::string					_publicBaseUrl;
	std::string					_uploadTempDir;
	int64						_maxUploadBytes = 0;
};

#endif // ndef FILESERVERMAIN_H