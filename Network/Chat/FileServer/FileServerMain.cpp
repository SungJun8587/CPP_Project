
//***************************************************************************
// FileServerMain.cpp: implementation of the CFileServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerMain.h"
#include "FileServerSession.h"
#include "LocalFileStorage.h"
#include "ImageResizeUtil.h"
#include <Redis/RedisResultSet.h>

#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

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
	_tstring storageDir, std::string publicBaseUrl, int64 maxUploadBytes, int32 maxProfileImageDimension)
{
	_publicBaseUrl = std::move(publicBaseUrl);
	_maxUploadBytes = maxUploadBytes;
	_maxProfileImageDimension = maxProfileImageDimension;

	// 1. IOCP 코어 + JobQueue(Redis 콜백을 안전한 스레드로 넘기는 용도)
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 채팅 서버가 발급한 업로드 토큰을 검증하는 용도.
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisNodeVec, redisPoolSize) )
		return false;

	// 3. 파일 저장소 초기화.
	_fileStorage = std::make_unique<CLocalFileStorage>(storageDir);

	// 3-1. 메타데이터 리포지토리 초기화(인메모리 — FileMetadataRepository.h 참고).
	_metadataRepo = std::make_unique<CFileMetadataRepository>();

	// 3-2. 라우터 초기화 — 실제 요청 처리(업로드/다운로드/삭제)는 전부
	// 이 라우터가 소유한 세 핸들러에 위임한다(FileServerRouter.h 참고).
	_router = std::make_unique<CFileServerRouter>(
		_fileStorage.get(), _redisService.get(), _metadataRepo.get(),
		_publicBaseUrl, _maxUploadBytes, _maxProfileImageDimension);

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

	if( !_service->Start() )
	{
		_service.reset();
		return false;
	}

	// 5. 채팅 서버가 Redis 큐에 남겨둔 "지울 파일" 폴링 시작.
	_stopPolling.store(false);
	StartPendingDeletionPolling();

	return true;
}

//***************************************************************************
// @brief 파일 서버를 정지합니다.
//***************************************************************************
void CFileServerMain::Stop()
{
	// 진행 중인(또는 대기 중인) 폴링이 이 시점 이후로는 더 이상 새로운
	// LPOP/재예약을 하지 않게 먼저 알린다. [알려진 한계] detached 스레드
	// 기반이라 완전한 join 보장은 아니다 — 이 함수가 리턴한 직후에도
	// 아주 짧은 창(이미 잠들어 있던 스레드가 막 깨어난 순간) 동안 콜백이
	// 한 번 더 발화할 수 있다. _redisService/_fileStorage를 nullptr로
	// 만들기 직전에 플래그부터 세팅해서 그 확률을 최소화한다.
	_stopPolling.store(true);

	if( _service != nullptr )
	{
		// [수정] CIocpServerService에 Stop()은 없고 Close()가 맞다 — 모든
		// 세션 종료까지 블로킹 대기한다(ChatServerMain::Stop()의 실제
		// 구현을 그대로 따름).
		_service->Close();
		_service.reset();
	}

	// [주의] _router는 _fileStorage/_redisService/_metadataRepo를 raw
	// pointer로만 빌려 쓴다 — 그것들보다 먼저 정리해야 매달린 포인터를
	// 들고 있는 창을 만들지 않는다.
	_router.reset();
	_redisService.reset();
	_fileStorage.reset();
	_metadataRepo.reset();
	_jobQueue.reset();
	_iocpCore.reset();
}

//***************************************************************************
// @brief 완성된 HTTP 요청 하나를 메서드+경로로 라우팅합니다.
//***************************************************************************
//***************************************************************************
// @brief 완성된 HTTP 요청 하나를 라우팅합니다. 실제 처리는 _router에게
//        전부 위임한다(FileServerRouter.h/FileUploadHandler.h/
//        FileDownloadHandler.h/FileDeleteHandler.h 참고) — 이 메서드는
//        CFileServerSession::OnRecv()가 부르는 진입점 자리만 유지한다.
//***************************************************************************
void CFileServerMain::HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request)
{
	if( _router == nullptr )
		return;

	_router->HandleRequest(session, request);
}

//***************************************************************************
// @brief 예약 삭제 폴링을 시작합니다(첫 번째 폴링을 곧바로 트리거).
//***************************************************************************
void CFileServerMain::StartPendingDeletionPolling()
{
	PollPendingDeletions();
}

//***************************************************************************
// @brief 큐에서 하나를 꺼내(LPOP) 있으면 지우고 곧바로 다시 시도, 없으면
//        일정 시간 뒤 다시 폴링하도록 예약합니다.
//***************************************************************************
void CFileServerMain::PollPendingDeletions()
{
	if( _stopPolling.load() || _redisService == nullptr )
		return;

	CVector<std::string> args;
	args.push_back("LPOP");
	args.push_back(kPendingDeletionsKey);

	IFileStorage* storage = _fileStorage.get();
	CFileMetadataRepository* metadataRepo = _metadataRepo.get();

	_redisService->SendCommand(args, [this, storage, metadataRepo](const RedisValue& res)
		{
			if( _stopPolling.load() )
				return;

			// CRedisResultSet으로 타입 안전하게 추출 — LPOP 결과는 문자열
			// 하나(성공) 또는 nil(큐가 비어있음)이다.
			CRedisResultSet resultSet(res);
			std::string relativePath;
			const bool hasValue = !resultSet.IsEmpty() && resultSet.GetData(relativePath) && !relativePath.empty();

			if( hasValue )
			{
				if( storage != nullptr && !storage->DeleteFile(relativePath) )
				{
					LOG_ERROR(_T("CFileServerMain::PollPendingDeletions: 파일 삭제 실패 (%hs)"), relativePath.c_str());
				}
				else
				{
					// [추가] 파일과 함께 메타데이터도 정리한다 — 채팅 서버가
					// 발신한 예약 삭제 경로도 FileDeleteHandler(즉시 삭제
					// 경로)와 동일하게 메타데이터를 남기지 않아야 한다.
					if( metadataRepo != nullptr )
						metadataRepo->Remove(relativePath);

					LOG_INFO(_T("CFileServerMain::PollPendingDeletions: 예약된 삭제 처리 완료 (%hs)"), relativePath.c_str());
				}

				// 큐에 더 남아있을 수 있으니 대기 없이 곧바로 한 번 더 확인
				// (드레인) — 비어있으면 이 재귀 호출 자체가 자연스럽게
				// ScheduleNextPoll() 경로로 빠진다.
				PollPendingDeletions();
				return;
			}

			// 큐가 비어있음 — 일정 간격 뒤 다시 확인.
			ScheduleNextPoll();
		});
}

//***************************************************************************
// @brief kPollingIntervalSec 뒤 PollPendingDeletions()를 다시 호출합니다.
//***************************************************************************
void CFileServerMain::ScheduleNextPoll()
{
	if( _stopPolling.load() )
		return;

	std::thread([this]()
		{
			std::this_thread::sleep_for(std::chrono::seconds(kPollingIntervalSec));
			if( !_stopPolling.load() )
				PollPendingDeletions();
		}).detach();
}