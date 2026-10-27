
//***************************************************************************
// FileServerMain.cpp: implementation of the CFileServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerMain.h"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <filesystem>

//***************************************************************************
// @brief 소멸자 — 아직 실행 중이면 Stop()으로 정리합니다.
//***************************************************************************
CFileServerMain::~CFileServerMain()
{
	Stop();
}

//***************************************************************************
// @brief 파일 서버 구동. 채팅 서버(CChatServerMain::Start())와 동일한
//        인프라 초기화 순서 — IOCP 코어/JobQueue -> Redis -> IOCP 서비스.
//***************************************************************************
bool CFileServerMain::Start(
	const _tstring& bindIp, uint16 bindPort,
	CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
	int32 maxSessionCount, uint32 workerThreadCount,
	_tstring storageDir, std::string publicBaseUrl, int64 maxUploadBytes)
{
	_publicBaseUrl = std::move(publicBaseUrl);
	_maxUploadBytes = maxUploadBytes;

	// 1. IOCP 코어 + JobQueue(Redis 콜백을 안전한 스레드로 넘기는 용도)
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 채팅 서버가 발급한 업로드 토큰을 검증하는 용도.
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisNodeVec, redisPoolSize) )
		return false;

	// 3. 파일 저장소 초기화.
	_fileStorage = std::make_unique<CLocalFileStorage>(storageDir);

	// 3-1. 업로드 임시 디렉터리 — 저장소와 같은 볼륨이어야 저장이 rename 한
	// 번으로 끝난다. 이전 실행이 비정상 종료되며 남긴 임시 파일은 비운다.
	// (".tmp"는 CLocalFileStorage가 owner 세그먼트로 받지 않으므로 다운로드로 노출되지 않는다.)
	{
		std::error_code ec;
		const std::filesystem::path tempDir = std::filesystem::path(storageDir) / ".tmp";
		std::filesystem::remove_all(tempDir, ec);
		std::filesystem::create_directories(tempDir, ec);
		if( ec )
		{
			LOG_ERROR(_T("CFileServerMain::Start: 임시 디렉터리 생성 실패 (%hs)"), ec.message().c_str());
			return false;
		}
		_uploadTempDir = tempDir.string();
	}

	// 3-2. 라우터 초기화 — 업로드/다운로드 처리는 이 라우터가 소유한 핸들러에
	// 위임한다(FileServerRouter.h 참고).
	_router = std::make_unique<CFileServerRouter>(
		_fileStorage.get(), _redisService.get(), _publicBaseUrl);

	// 4. IOCP 서버 서비스 시작 — 세션 팩토리가 CFileServerSession을 생성.
	SessionFactory factory = [this]() -> CSessionRef
		{
			return std::make_shared<CFileServerSession>(this);
		};

	EngineCoreRef engineCore = _iocpCore;
	CNetServiceRef service = CNetworkFactory::CreateServerService(
		engineCore, CNetAddress(bindIp, bindPort), factory, maxSessionCount, workerThreadCount);

	_service = std::static_pointer_cast<CIocpServerService>(service);
	if( _service == nullptr )
		return false;

	// 접속만 하고 아무것도 보내지 않거나 요청을 느리게 흘리는 연결이 세션을 영구히 차지하지 못하게 한다.
	// - 유휴 타임아웃: 수신/송신 완료가 없는 상태가 이 시간 이상이면 종료(큰 파일을 올리거나 받는 중에는 I/O 완료가 계속 들어와 걸리지 않는다)
	// - keep-alive: FIN/RST 없이 사라진 피어 감지
	// - 접속 수 상한: 설정의 MaxSessionCount를 실제 상한으로 적용
	constexpr uint32 kIdleTimeoutMs = 60 * 1000;
	CIocpSession::SocketOptions socketOptions;
	socketOptions.keepAlive = true;
	_service->SetSessionSocketOptions(socketOptions);
	_service->SetIdleTimeout(kIdleTimeoutMs);
	_service->SetEnforceMaxSessionCount(true);

	if( !_service->Start() )
	{
		_service.reset();
		return false;
	}

	// 5. 채팅 서버가 Redis 큐에 남겨둔 "지울 파일" 소비 스레드 시작.
	_stopPolling.store(false);
	StartPendingDeletionThread();

	return true;
}

//***************************************************************************
// @brief 파일 서버를 정지합니다.
//***************************************************************************
void CFileServerMain::Stop()
{
	// 삭제 소비 스레드를 먼저 깨워 join한다 — 이후 _redisService/_fileStorage를
	// 정리해도 스레드가 쓰는 일이 없다.
	{
		std::lock_guard<std::mutex> lock(_pollMutex);
		_stopPolling.store(true);
	}
	_pollCv.notify_all();
	if( _pollThread.joinable() )
		_pollThread.join();

	if( _service != nullptr )
	{
		// [수정] CIocpServerService에 Stop()은 없고 Close()가 맞다 — 모든
		// 세션 종료까지 블로킹 대기한다(ChatServerMain::Stop()의 실제
		// 구현을 그대로 따름).
		_service->Close();
		_service.reset();
	}

	// [주의] _router는 _fileStorage/_redisService를 raw pointer로만 빌려
	// 쓴다 — 그것들보다 먼저 정리해야 매달린 포인터를 들고 있는 창을
	// 만들지 않는다.
	_router.reset();
	_redisService.reset();
	_fileStorage.reset();
	_jobQueue.reset();
	_iocpCore.reset();
}

//***************************************************************************
// @brief 완성된 HTTP 요청 하나를 라우팅합니다. 실제 처리는 _router에게
//        전부 위임한다(FileServerRouter.h/FileUploadHandler.h/
//        FileDownloadHandler.h 참고) — 이 메서드는
//        CFileServerSession::OnRecv()가 부르는 진입점 자리만 유지한다.
//***************************************************************************
void CFileServerMain::HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request)
{
	if( _router == nullptr )
		return;

	_router->HandleRequest(session, request);
}

//***************************************************************************
// @brief 예약 삭제 소비 스레드를 시작합니다.
//***************************************************************************
void CFileServerMain::StartPendingDeletionThread()
{
	_pollThread = std::thread([this]() { PendingDeletionLoop(); });
}

//***************************************************************************
// @brief 스레드 본체.
//***************************************************************************
void CFileServerMain::PendingDeletionLoop()
{
	while( !_stopPolling.load() )
	{
		std::string relativePath;
		if( PopPendingDeletion(relativePath) )
		{
			ProcessDeletion(relativePath);
			continue; // 큐에 더 남아있을 수 있으니 대기 없이 연속으로 비운다
		}

		std::unique_lock<std::mutex> lock(_pollMutex);
		_pollCv.wait_for(lock, std::chrono::seconds(kPollingIntervalSec), [this]() { return _stopPolling.load(); });
	}
}

//***************************************************************************
// @brief 큐에서 하나를 꺼냅니다(LPOP).
// @details Redis 콜백은 CJobQueue 스레드에서 실행되므로, 결과를 공유 상태에
//          담아 이 스레드가 기다린다. 타임아웃으로 먼저 빠져나와도 콜백이
//          공유 상태(shared_ptr)만 건드리므로 안전하다.
//***************************************************************************
bool CFileServerMain::PopPendingDeletion(std::string& outRelativePath)
{
	if( _redisService == nullptr )
		return false;

	struct SPopState
	{
		std::mutex				mutex;
		std::condition_variable	cv;
		bool					done = false;
		bool					got = false;
		std::string				value;
	};
	auto state = std::make_shared<SPopState>();

	CVector<std::string> args;
	args.push_back("LPOP");
	args.push_back(FileServerKeys::kPendingDeletionsKey);

	// SendCommand()는 풀이 없거나 대기 큐가 가득 차면 false — 콜백이 오지 않는다.
	if( !_redisService->SendCommand(args, [state](const RedisValue& res)
		{
			// LPOP 결과는 문자열 하나(성공) 또는 nil(큐가 비어있음).
			CRedisResultSet resultSet(res);
			std::string value;
			const bool got = !resultSet.IsEmpty() && resultSet.GetData(value) && !value.empty();

			std::lock_guard<std::mutex> lock(state->mutex);
			state->got = got;
			state->value = std::move(value);
			state->done = true;
			state->cv.notify_all();
		}) )
	{
		return false;
	}

	// 짧은 간격으로 깨어나 중지 플래그를 확인한다. Stop()이 응답 대기 때문에 지연되지 않는다.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kRedisWaitSec);
	std::unique_lock<std::mutex> lock(state->mutex);
	while( !state->done && !_stopPolling.load() && std::chrono::steady_clock::now() < deadline )
		state->cv.wait_for(lock, std::chrono::milliseconds(100), [&state]() { return state->done; });
	if( !state->done || !state->got )
		return false;

	outRelativePath = std::move(state->value);
	return true;
}

//***************************************************************************
// @brief 파일 하나를 지웁니다. 실패하면 실패 목록에 남깁니다.
//***************************************************************************
void CFileServerMain::ProcessDeletion(const std::string& relativePath)
{
	if( _fileStorage != nullptr && _fileStorage->DeleteFile(relativePath) )
	{
		LOG_INFO(_T("CFileServerMain: 예약된 삭제 처리 완료 (%hs)"), relativePath.c_str());
		return;
	}

	LOG_ERROR(_T("CFileServerMain: 파일 삭제 실패 — 실패 목록으로 이동 (%hs)"), relativePath.c_str());

	if( _redisService == nullptr )
		return;

	CVector<std::string> failArgs;
	failArgs.push_back("RPUSH");
	failArgs.push_back(FileServerKeys::kFailedDeletionsKey);
	failArgs.push_back(relativePath);
	_redisService->SendCommand(failArgs, [](const RedisValue& /*res*/) {});
}