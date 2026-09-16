
//***************************************************************************
// FileServerMain.cpp: implementation of the CFileServerMain class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerMain.h"
#include "FileServerSession.h"
#include "LocalFileImageStorage.h"
#include "ImageResizeUtil.h"
#include <Network/HTTP/MultipartFormParser.h>
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
	_tstring storageDir, std::string publicBaseUrl, int32 maxUploadBytes, int32 maxImageDimension)
{
	_publicBaseUrl = std::move(publicBaseUrl);
	_maxUploadBytes = maxUploadBytes;
	_maxImageDimension = maxImageDimension;

	// 0. GDI+ 초기화 — 업로드된 이미지 리사이즈용(ImageResizeUtil). 실패해도
	// 서버 자체는 계속 띄운다 — 리사이즈만 건너뛰고 원본을 그대로 저장하는
	// 형태로 계속 동작 가능하다(ResizeIfLarger()가 항상 안전하게 실패 처리함).
	if( !ImageResizeUtil::Startup() )
		LOG_ERROR(_T("CFileServerMain::Start: ImageResizeUtil::Startup 실패 — 이미지 리사이즈 없이 원본 그대로 저장됩니다."));

	// 1. IOCP 코어 + JobQueue(Redis 콜백을 안전한 스레드로 넘기는 용도)
	_iocpCore = MakeShared<CIocpCore>();
	_jobQueue = std::make_shared<CJobQueue>();

	// 2. Redis 초기화 — 채팅 서버가 발급한 업로드 토큰을 검증하는 용도.
	_redisService = std::make_unique<CRedisService>(_iocpCore, _jobQueue);
	if( !_redisService->Init(redisNodeVec, redisPoolSize) )
		return false;

	// 3. 이미지 저장소 초기화.
	_imageStorage = std::make_unique<CLocalFileImageStorage>(storageDir);

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
	// 한 번 더 발화할 수 있다. _redisService/_imageStorage를 nullptr로
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

	_redisService.reset();
	_imageStorage.reset();
	_jobQueue.reset();
	_iocpCore.reset();

	ImageResizeUtil::Shutdown();
}

//***************************************************************************
// @brief 완성된 HTTP 요청 하나를 메서드+경로로 라우팅합니다.
//***************************************************************************
void CFileServerMain::HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request)
{
	const std::string& method = request.GetMethod();
	const std::string_view path = request.GetPath();
	const bool keepAlive = request.IsKeepAlive();

	constexpr std::string_view kImagesPrefix = "/images/";

	if( method == "POST" && path == "/upload" )
	{
		HandleUpload(session, request, keepAlive);
	}
	else if( method == "GET" && path.size() > kImagesPrefix.size() && path.compare(0, kImagesPrefix.size(), kImagesPrefix) == 0 )
	{
		const std::string relativePath(path.substr(kImagesPrefix.size()));
		HandleGetImage(session, relativePath, keepAlive);
	}
	else if( method == "DELETE" && path.size() > kImagesPrefix.size() && path.compare(0, kImagesPrefix.size(), kImagesPrefix) == 0 )
	{
		const std::string relativePath(path.substr(kImagesPrefix.size()));
		HandleDeleteImage(session, relativePath, keepAlive);
	}
	else
	{
		SendSimpleResponse(session, 404, "Not Found", "text/plain", "404 Not Found", keepAlive);
	}
}

//***************************************************************************
// @brief POST /upload 처리.
//***************************************************************************
void CFileServerMain::HandleUpload(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, bool keepAlive)
{
	const std::string_view contentType = request.FindHeader("Content-Type");

	std::string boundary;
	if( !HTTP::ExtractBoundary(contentType, boundary) )
	{
		LOG_ERROR(_T("CFileServerMain::HandleUpload: boundary 추출 실패 (Content-Type=%hs)"), std::string(contentType).c_str());
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Missing multipart boundary", keepAlive);
		return;
	}

	std::vector<HTTP::SMultipartField> fields;
	if( !HTTP::ParseMultipartFormData(request.GetBody(), boundary, fields) )
	{
		LOG_ERROR(_T("CFileServerMain::HandleUpload: 멀티파트 파싱 실패"));
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Malformed multipart body", keepAlive);
		return;
	}

	const HTTP::SMultipartField* tokenField = HTTP::FindMultipartField(fields, "token");
	const HTTP::SMultipartField* fileField = HTTP::FindMultipartField(fields, "file");

	if( tokenField == nullptr || fileField == nullptr || fileField->data.empty() )
	{
		LOG_ERROR(_T("CFileServerMain::HandleUpload: token 또는 file 필드 누락 (token=%d, file=%d)"),
			tokenField != nullptr, fileField != nullptr);
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Missing token or file field", keepAlive);
		return;
	}

	if( static_cast<int32>(fileField->data.size()) > _maxUploadBytes )
	{
		SendSimpleResponse(session, 413, "Payload Too Large", "text/plain", "File too large", keepAlive);
		return;
	}

	std::string fileExtension;
	{
		const size_t dotPos = fileField->filename.find_last_of('.');
		if( dotPos != std::string::npos )
			fileExtension = fileField->filename.substr(dotPos);
	}
	if( fileExtension.empty() )
		fileExtension = ".bin";

	// 콜백(비동기 Redis 왕복 이후)에서 쓸 값들은 전부 값으로 복사해둔다 —
	// request/fields는 이 함수가 끝나면 소멸되는 참조/지역 객체이므로.
	const std::string fileBytes = fileField->data;
	const std::string tokenHex = tokenField->data;

	std::weak_ptr<CFileServerSession> sessionWeak = session;
	IImageStorage* storage = _imageStorage.get();

	VerifyAndConsumeUploadToken(tokenHex,
		[this, sessionWeak, storage, fileBytes, fileExtension, keepAlive](bool success, const std::string& ownerPublicIdHex)
		{
			auto session = sessionWeak.lock();
			if( session == nullptr )
			{
				LOG_ERROR(_T("CFileServerMain::HandleUpload: 응답 준비 전에 세션이 끊김"));
				return;
			}

			if( !success )
			{
				SendSimpleResponse(session, 401, "Unauthorized", "text/plain", "Invalid or expired upload token", keepAlive);
				return;
			}

			const std::vector<BYTE> data(fileBytes.begin(), fileBytes.end());

			// [추가] 해상도가 크면 줄여서 저장 용량을 아낀다. 실패하거나
			// 애초에 작으면 ResizeIfLarger()가 false를 돌려주고, 그 경우
			// resizedData는 비워둔 채 원본(data)을 그대로 저장한다 —
			// 리사이즈 버그가 있어도 업로드 기능 자체는 절대 안 깨지게.
			std::vector<BYTE> resizedData;
			const bool wasResized = ImageResizeUtil::ResizeIfLarger(data, fileExtension, _maxImageDimension, resizedData);
			const std::vector<BYTE>& dataToSave = wasResized ? resizedData : data;

			std::string relativePath;
			if( storage == nullptr || !storage->SaveImage(ownerPublicIdHex, fileExtension, dataToSave, relativePath) )
			{
				LOG_ERROR(_T("CFileServerMain::HandleUpload: SaveImage 실패(storage=%d)"), storage != nullptr);
				SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Failed to save file", keepAlive);
				return;
			}

			const std::string url = BuildPublicUrl(relativePath);
			SendSimpleResponse(session, 200, "OK", "text/plain", url, keepAlive);
		});
}

//***************************************************************************
// @brief GET /images/{path} 처리.
//***************************************************************************
void CFileServerMain::HandleGetImage(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive)
{
	if( _imageStorage == nullptr )
	{
		SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Storage not initialized", keepAlive);
		return;
	}

	std::vector<BYTE> data;
	if( !_imageStorage->LoadImage(relativePath, data) )
	{
		SendSimpleResponse(session, 404, "Not Found", "text/plain", "Image not found", keepAlive);
		return;
	}

	const std::string contentType = GuessContentType(relativePath);

	// 바이너리 body라 SendSimpleResponse(문자열 전용)로는 못 담는다 — 직접 조립.
	std::string response;
	response.reserve(256 + data.size());
	response += "HTTP/1.1 200 OK\r\n";
	response += "Content-Type: " + contentType + "\r\n";
	response += "Content-Length: " + std::to_string(data.size()) + "\r\n";
	response += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
	response += "\r\n";
	response.append(reinterpret_cast<const char*>(data.data()), data.size());

	session->SendRaw(response);
}

//***************************************************************************
// @brief DELETE /images/{path} 처리.
//***************************************************************************
void CFileServerMain::HandleDeleteImage(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive)
{
	if( _imageStorage == nullptr )
	{
		SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Storage not initialized", keepAlive);
		return;
	}

	// IImageStorage::DeleteImage() 문서 참고 — 파일이 애초에 없었던 경우도
	// 성공으로 취급한다. 경로 조작 시도("../")만 별도로 실패 처리된다.
	if( !_imageStorage->DeleteImage(relativePath) )
	{
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Invalid path", keepAlive);
		return;
	}

	SendSimpleResponse(session, 200, "OK", "text/plain", "deleted", keepAlive);
}
void CFileServerMain::VerifyAndConsumeUploadToken(const std::string& tokenHex, std::function<void(bool success, const std::string& ownerPublicIdHex)> onComplete)
{
	if( _redisService == nullptr )
	{
		if( onComplete )
			onComplete(false, std::string());
		return;
	}

	const std::string redisKey = "UploadToken:" + tokenHex;
	CRedisService* redisService = _redisService.get();

	CVector<std::string> getArgs;
	getArgs.push_back("GET");
	getArgs.push_back(redisKey);

	redisService->SendCommand(getArgs, [redisService, onComplete, redisKey](const RedisValue& res)
		{
			// CRedisResultSet으로 타입 안전하게 추출 — GET의 결과는 문자열
			// 하나(성공, 채팅 서버가 저장해둔 public_id 16진) 또는 nil
			// (키 없음/만료)이다. IsEmpty()는 nil일 때 true.
			CRedisResultSet resultSet(res);

			std::string ownerPublicIdHex;
			const bool success = !resultSet.IsEmpty() && resultSet.GetData(ownerPublicIdHex) && !ownerPublicIdHex.empty();

			if( success )
			{
				// 1회용 토큰 — 검증에 성공했으면 즉시 삭제해서 재사용을 막는다.
				CVector<std::string> delArgs;
				delArgs.push_back("DEL");
				delArgs.push_back(redisKey);
				redisService->SendCommand(delArgs, [](const RedisValue& /*res*/) {});
			}

			// [수정] CRedisService::SendCommand()의 콜백은 이미 생성자에
			// 넘긴 CJobQueue 스레드에서 안전하게 실행된다 — 여기서 또
			// jobQueue->DoAsync()로 감싸는 건 불필요한 이중 디스패치였다.
			if( onComplete )
				onComplete(success, ownerPublicIdHex);
		});
}

//***************************************************************************
// @brief relativePath로 접근 가능한 공개 URL을 만든다.
//***************************************************************************
std::string CFileServerMain::BuildPublicUrl(const std::string& relativePath) const
{
	std::string base = _publicBaseUrl;
	if( !base.empty() && base.back() == '/' )
		base.pop_back();

	return base + "/images/" + relativePath;
}

//***************************************************************************
// @brief 확장자로부터 Content-Type을 추정한다.
//***************************************************************************
std::string CFileServerMain::GuessContentType(const std::string& path)
{
	const size_t dotPos = path.find_last_of('.');
	if( dotPos == std::string::npos )
		return "application/octet-stream";

	std::string ext = path.substr(dotPos + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });

	if( ext == "png" ) return "image/png";
	if( ext == "jpg" || ext == "jpeg" ) return "image/jpeg";
	if( ext == "gif" ) return "image/gif";
	if( ext == "bmp" ) return "image/bmp";
	if( ext == "webp" ) return "image/webp";

	return "application/octet-stream";
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다.
//***************************************************************************
void CFileServerMain::SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
	const std::string& contentType, const std::string& body, bool keepAlive)
{
	if( session == nullptr )
		return;

	std::string response;
	response.reserve(256 + body.size());
	response += "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
	response += "Content-Type: " + contentType + "\r\n";
	response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	response += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
	response += "\r\n";
	response += body;

	session->SendRaw(response);
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

	IImageStorage* storage = _imageStorage.get();

	_redisService->SendCommand(args, [this, storage](const RedisValue& res)
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
				if( storage != nullptr && !storage->DeleteImage(relativePath) )
					LOG_ERROR(_T("CFileServerMain::PollPendingDeletions: 파일 삭제 실패 (%hs)"), relativePath.c_str());
				else
					LOG_INFO(_T("CFileServerMain::PollPendingDeletions: 예약된 삭제 처리 완료 (%hs)"), relativePath.c_str());

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