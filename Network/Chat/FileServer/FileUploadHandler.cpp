
//***************************************************************************
// FileUploadHandler.cpp: implementation of the CFileUploadHandler class.
//
// [설계 변경 — 대용량 업로드 스트리밍] 예전엔 request.GetBody()(멀티파트
// 본문 전체가 메모리에 있다는 전제) + HTTP::ParseMultipartFormData()로
// "token"/"file" 필드를 한 번에 뽑아냈다 — 1GB급 파일이면 그 본문 전체가
// 서버 메모리에 올라가야 했다.
//
// 이제는 CFileServerSession이 본문을 받는 시점에 이미 스트리밍으로
// 처리해뒀다(FileServerSession.cpp의 SetupUploadStreamingIfNeeded()/
// CMultipartStreamParser 참고) — "file" 파트는 임시 파일에 바로 쓰여있고,
// "token" 값만 작은 문자열로 메모리에 있다. 이 핸들러는 request.GetBody()를
// 더 이상 쓰지 않고 session->GetPendingUploadResult()가 돌려주는 이 결과를
// 그대로 받아 처리한다 — 실제로 큰 파일 바이트를 만지는 부분은
// IFileStorage::SaveFileFromPath()(이미 디스크에 있는 임시 파일을 최종
// 위치로 rename/copy)뿐이라, 이 핸들러 자체는 여전히 파일 크기와 무관하게
// 가벼운 코드로 남는다.
//***************************************************************************

#include "pch.h"
#include "FileUploadHandler.h"

namespace fs = std::filesystem;

namespace
{
	//***************************************************************************
	// @brief 스코프를 벗어나면 파일을 삭제하는 RAII 가드 — 실패 경로마다
	//        임시 파일 정리를 잊지 않기 위함. 항상 shared_ptr로 만들어
	//        써야 한다 — VerifyAndConsumeUploadToken()의 콜백은 비동기라
	//        Handle() 함수 자체가 먼저 반환된 뒤에야 실행되므로, 로컬
	//        스택 변수로 만들면 콜백이 오기도 전에 이 가드가 소멸되며
	//        파일을 지워버리는 사고가 난다(실제로 처음 구현에서 이 버그가
	//        났었다). shared_ptr로 감싸 콜백 클로저에 캡처해두면, 그
	//        콜백이 실행을 마칠 때까지(=마지막 참조가 사라질 때까지) 파일이
	//        살아있는 게 보장된다.
	//***************************************************************************
	struct STempFileGuard
	{
		std::string path;
		bool released = false;

		explicit STempFileGuard(std::string p) : path(std::move(p)) {}

		~STempFileGuard()
		{
			if( !released && !path.empty() )
			{
				std::error_code ec;
				fs::remove(path, ec);
			}
		}

		// 성공 경로(파일이 이미 SaveFileFromPath()로 옮겨져 더 이상 이
		// 경로에 존재하지 않음)에서는 삭제를 시도할 필요가 없다는 걸 표시.
		void Release() { released = true; }
	};

	//***************************************************************************
	// @brief maxBytes보다 작은 이미지 파일만 리사이즈를 시도한다.
	// @details 대용량 업로드(동영상 등)에까지 ImageResizeUtil을 태우면
	//          그 자체가 파일 전체를 메모리에 올리는 부담을 재도입하게
	//          된다 — 원래 문제로 돌아가는 것을 막기 위한 안전장치. 이
	//          상한을 넘는 파일은 확장자가 이미지여도 리사이즈 없이 원본
	//          그대로 저장한다(프로필 사진이 이 정도로 클 일은 실질적으로
	//          없고, 있다 해도 리사이즈를 건너뛰는 것뿐 업로드 자체는
	//          정상 처리된다).
	//***************************************************************************
	constexpr int64 kMaxBytesForResize = 50LL * 1024 * 1024; // 50MB
}

CFileUploadHandler::CFileUploadHandler(
	IFileStorage* storage,
	CRedisService* redisService,
	CFileMetadataRepository* metadataRepo,
	std::string publicBaseUrl,
	int64 maxUploadBytes,
	int32 maxProfileImageDimension)
	: _storage(storage)
	, _redisService(redisService)
	, _metadataRepo(metadataRepo)
	, _publicBaseUrl(std::move(publicBaseUrl))
	, _maxUploadBytes(maxUploadBytes)
	, _maxProfileImageDimension(maxProfileImageDimension)
{
}

//***************************************************************************
// @brief POST /upload 처리.
// @details request는 더 이상 본문 조회에 쓰이지 않는다(스트리밍 모드라
//          request.GetBody()가 항상 비어있음) — 시그니처는 CFileServerRouter
//          호출부를 안 바꾸려고 그대로 유지했다.
//***************************************************************************
void CFileUploadHandler::Handle(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& /*request*/, bool keepAlive)
{
	const SPendingUploadResult upload = session->GetPendingUploadResult();

	if( !upload.attempted )
	{
		// POST /upload이긴 했는데 boundary조차 없었던 경우 등 — 애초에
		// 스트리밍 자체가 안 걸렸다(이 경우 임시 파일도 없으므로 정리할
		// 게 없다).
		LOG_ERROR(_T("CFileUploadHandler::Handle: boundary 추출 실패 또는 스트리밍 미설정"));
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Missing multipart boundary", keepAlive);
		return;
	}

	// [주의] 반드시 shared_ptr로 만들어야 한다 — VerifyAndConsumeUploadToken()의
	// 콜백이 비동기라 이 함수(Handle)가 먼저 반환된 뒤에야 실행되므로,
	// 로컬 스택 변수로 만들면 콜백이 오기도 전에 파일이 지워진다. 이후
	// 모든 반환 경로(동기/비동기 무관)에서 이 가드가 스코프를 벗어날 때
	// (=참조 카운트가 0이 될 때) 자동으로 정리되고, 성공 경로에서만
	// Release()로 정리를 건너뛴다.
	std::shared_ptr<STempFileGuard> tempFileGuard;
	if( upload.hasFile )
		tempFileGuard = std::make_shared<STempFileGuard>(upload.tempFilePath);

	if( !upload.multipartOk || !upload.hasFile || !upload.hasToken )
	{
		LOG_ERROR(_T("CFileUploadHandler::Handle: 멀티파트 파싱 실패 또는 필드 누락 (multipartOk=%d, hasFile=%d, hasToken=%d)"),
			upload.multipartOk, upload.hasFile, upload.hasToken);
		SendSimpleResponse(session, 400, "Bad Request", "text/plain", "Malformed multipart body or missing fields", keepAlive);
		return; // tempFileGuard가 여기서 소멸되며 임시 파일 정리(있었다면)
	}

	if( upload.fileSizeBytes > _maxUploadBytes )
	{
		SendSimpleResponse(session, 413, "Payload Too Large", "text/plain", "File too large", keepAlive);
		return; // 위와 동일
	}

	std::string fileExtension;
	{
		const size_t dotPos = upload.originalFileName.find_last_of('.');
		if( dotPos != std::string::npos )
			fileExtension = upload.originalFileName.substr(dotPos);
	}
	if( fileExtension.empty() )
		fileExtension = ".bin";

	// [추가] 작은 이미지 파일이면 리사이즈를 시도한다 — 임시 파일을 통째로
	// 읽어서 처리한 뒤, 결과를 같은 임시 파일 경로에 다시 덮어쓴다(이후
	// SaveFileFromPath()가 그 최신 내용을 최종 위치로 옮기게).
	if( upload.fileSizeBytes <= kMaxBytesForResize )
	{
		std::vector<BYTE> original;
		{
			std::ifstream ifs(upload.tempFilePath, std::ios::binary | std::ios::ate);
			if( ifs.is_open() )
			{
				const std::streamsize size = ifs.tellg();
				if( size > 0 )
				{
					original.resize(static_cast<size_t>(size));
					ifs.seekg(0, std::ios::beg);
					ifs.read(reinterpret_cast<char*>(original.data()), size);
				}
			}
		}

		std::vector<BYTE> resized;
		if( !original.empty() && ImageResizeUtil::ResizeIfLarger(original, fileExtension, _maxProfileImageDimension, resized) )
		{
			std::ofstream ofs(upload.tempFilePath, std::ios::binary | std::ios::trunc);
			if( ofs.is_open() )
				ofs.write(reinterpret_cast<const char*>(resized.data()), static_cast<std::streamsize>(resized.size()));
		}
	}
	// upload.fileSizeBytes > kMaxBytesForResize면 리사이즈를 건너뛰고
	// 원본을 그대로 저장한다 — 위 네임스페이스의 주석 참고.

	const std::string tempFilePath = upload.tempFilePath;
	const std::string tokenHex = upload.tokenValue;
	const std::string originalFileName = upload.originalFileName;
	const std::string clientContentType = upload.fileContentType;

	std::weak_ptr<CFileServerSession> sessionWeak = session;
	IFileStorage* storage = _storage;
	CFileMetadataRepository* metadataRepo = _metadataRepo;

	VerifyAndConsumeUploadToken(tokenHex,
		[this, sessionWeak, storage, metadataRepo, tempFileGuard, tempFilePath, fileExtension, originalFileName, clientContentType, keepAlive]
		(bool success, const std::string& ownerPublicIdHex)
		{
			// [주의] tempFileGuard를 그대로 값 캡처했다 — 이 람다(=Redis
			// 콜백 클로저) 자체가 이 shared_ptr의 참조를 하나 쥐고 있는
			// 동안은 임시 파일이 절대 지워지지 않는다. 이 람다가 끝나고
			// 클로저가 소멸되는 순간(대부분 바로 이 호출 직후) 참조가
			// 0이 되며 Release() 안 했다면 자동 정리된다.
			auto session = sessionWeak.lock();
			if( session == nullptr )
			{
				LOG_ERROR(_T("CFileUploadHandler::Handle: 응답 준비 전에 세션이 끊김"));
				return;
			}

			if( !success )
			{
				SendSimpleResponse(session, 401, "Unauthorized", "text/plain", "Invalid or expired upload token", keepAlive);
				return;
			}

			std::string relativePath;
			if( storage == nullptr || !storage->SaveFileFromPath(ownerPublicIdHex, fileExtension, tempFilePath, relativePath) )
			{
				LOG_ERROR(_T("CFileUploadHandler::Handle: SaveFileFromPath 실패(storage=%d)"), storage != nullptr);
				SendSimpleResponse(session, 500, "Internal Server Error", "text/plain", "Failed to save file", keepAlive);
				return;
			}

			// SaveFileFromPath()가 성공하면 tempFilePath의 파일은 이미 최종
			// 위치로 옮겨져 그 자리에 더 이상 존재하지 않는다 — 가드가 그
			// (이미 없는) 경로를 다시 지우려 시도하지 않도록 해제해둔다.
			if( tempFileGuard )
				tempFileGuard->Release();

			if( metadataRepo != nullptr )
			{
				SFileMetadata metadata;
				metadata.relativePath = relativePath;
				metadata.ownerId = ownerPublicIdHex;
				metadata.originalFileName = originalFileName;
				metadata.contentType = clientContentType;
				metadata.fileSizeBytes = 0; // 정확한 최종 크기가 필요하면 이후 조회 시 파일 시스템에서 다시 확인
				metadata.uploadedAt = std::chrono::system_clock::now();
				metadataRepo->Register(std::move(metadata));
			}

			const std::string url = BuildPublicUrl(relativePath);
			SendSimpleResponse(session, 200, "OK", "text/plain", url, keepAlive);
		});
}

//***************************************************************************
// @brief Redis에서 업로드 토큰을 조회/소모합니다.
//***************************************************************************
void CFileUploadHandler::VerifyAndConsumeUploadToken(const std::string& tokenHex, std::function<void(bool success, const std::string& ownerPublicIdHex)> onComplete)
{
	if( _redisService == nullptr )
	{
		if( onComplete )
			onComplete(false, std::string());
		return;
	}

	const std::string redisKey = "UploadToken:" + tokenHex;
	CRedisService* redisService = _redisService;

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

			// CRedisService::SendCommand()의 콜백은 이미 생성자에 넘긴
			// CJobQueue 스레드에서 안전하게 실행된다 — 여기서 또
			// jobQueue->DoAsync()로 감싸는 건 불필요한 이중 디스패치다.
			if( onComplete )
				onComplete(success, ownerPublicIdHex);
		});
}

//***************************************************************************
// @brief relativePath로 접근 가능한 공개 URL을 만든다.
//***************************************************************************
std::string CFileUploadHandler::BuildPublicUrl(const std::string& relativePath) const
{
	std::string base = _publicBaseUrl;
	if( !base.empty() && base.back() == '/' )
		base.pop_back();

	return base + "/images/" + relativePath;
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다.
//***************************************************************************
void CFileUploadHandler::SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
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