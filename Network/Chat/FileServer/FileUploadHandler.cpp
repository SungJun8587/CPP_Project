
//***************************************************************************
// FileUploadHandler.cpp: implementation of the CFileUploadHandler class.
//
//***************************************************************************

#include "pch.h"
#include "../ChatCommon/ImageRef.h"
#include "FileUploadHandler.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	// 토큰 소모 스크립트: GET+DEL을 원자적으로 수행하고 값을 돌려준다(없으면 nil).
	constexpr const char* kConsumeScript = "local v=redis.call('GET',KEYS[1]) if v then redis.call('DEL',KEYS[1]) end return v";

	//***************************************************************************
	// @brief 스코프를 벗어나면 파일을 삭제하는 RAII 가드 — 실패 경로마다
	//        임시 파일 정리를 잊지 않기 위함. 항상 shared_ptr로 만들어
	//        써야 한다 — WaitUploadAuth()의 결과 처리는 비동기일 수 있어
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
}

CFileUploadHandler::CFileUploadHandler(
	IFileStorage* storage,
	CRedisService* redisService,
	std::string publicBaseUrl)
	: _storage(storage)
	, _redisService(redisService)
	, _publicBaseUrl(std::move(publicBaseUrl))
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
		LOG_WARNING(_T("CFileUploadHandler::Handle: boundary 추출 실패 또는 스트리밍 미설정"));
		session->SendSimple(400, "Bad Request", "text/plain", "Missing multipart boundary", keepAlive);
		return;
	}

	// [주의] 반드시 shared_ptr로 만들어야 한다 — WaitUploadAuth()의
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
		LOG_WARNING(_T("CFileUploadHandler::Handle: 멀티파트 파싱 실패 또는 필드 누락 (multipartOk=%d, hasFile=%d, hasToken=%d)"),
			upload.multipartOk, upload.hasFile, upload.hasToken);
		session->SendSimple(400, "Bad Request", "text/plain", "Malformed multipart body or missing fields", keepAlive);
		return; // tempFileGuard가 여기서 소멸되며 임시 파일 정리(있었다면)
	}

	// 크기 상한은 스트리밍 파서가 본문 수신 중에 강제한다(SetupUploadStreamingIfNeeded).

	std::string fileExtension;
	{
		const size_t dotPos = upload.originalFileName.find_last_of('.');
		if( dotPos != std::string::npos )
			fileExtension = upload.originalFileName.substr(dotPos);
	}
	// 확장자 검증/정제(영숫자 8자 이하, 아니면 .bin)는 저장소가 한다.

	// 서버는 이미지를 가공하지 않고 받은 그대로 저장한다 — 프로필 이미지 크기
	// 제한은 클라이언트가 업로드 전에 검사한다(ChatClientForm.Media.cs의
	// kMaxProfileImageBytes 참고).

	const std::string tempFilePath = upload.tempFilePath;

	std::weak_ptr<CFileServerSession> sessionWeak = session;
	IFileStorage* storage = _storage;

	// 토큰은 토큰 파트가 끝날 때 이미 소모됐다 — 그 인증 결과(소유자)를 받아 저장한다.
	session->WaitUploadAuth(
		[this, sessionWeak, storage, tempFileGuard, tempFilePath, fileExtension, keepAlive]
		(bool success, const std::string& ownerPublicIdHex)
		{
			// [주의] tempFileGuard를 그대로 값 캡처했다 — 이 람다(=인증 결과
			// 처리 클로저) 자체가 이 shared_ptr의 참조를 하나 쥐고 있는
			// 동안은 임시 파일이 절대 지워지지 않는다. 이 람다가 끝나고
			// 클로저가 소멸되는 순간(대부분 바로 이 호출 직후) 참조가
			// 0이 되며 Release() 안 했다면 자동 정리된다.
			auto session = sessionWeak.lock();
			if( session == nullptr )
			{
				LOG_WARNING(_T("CFileUploadHandler::Handle: 응답 준비 전에 세션이 끊김"));
				return;
			}

			if( !success )
			{
				session->SendSimple(401, "Unauthorized", "text/plain", "Invalid or expired upload token", keepAlive);
				return;
			}

			std::string relativePath;
			if( storage == nullptr || !storage->SaveFileFromPath(ownerPublicIdHex, fileExtension, tempFilePath, relativePath) )
			{
				LOG_ERROR(_T("CFileUploadHandler::Handle: SaveFileFromPath 실패(storage=%d)"), storage != nullptr);
				session->SendSimple(500, "Internal Server Error", "text/plain", "Failed to save file", keepAlive);
				return;
			}

			// SaveFileFromPath()가 성공하면 tempFilePath의 파일은 이미 최종
			// 위치로 옮겨져 그 자리에 더 이상 존재하지 않는다 — 가드가 그
			// (이미 없는) 경로를 다시 지우려 시도하지 않도록 해제해둔다.
			if( tempFileGuard )
				tempFileGuard->Release();

			const std::string url = BuildPublicUrl(relativePath);
			session->SendSimple(200, "OK", "text/plain", url, keepAlive);
		});
}

//***************************************************************************
// @brief Redis에서 업로드 토큰을 확인합니다.
// @details GET+DEL을 Lua 스크립트 하나로 실행해, 같은 토큰을 동시에 검증해도
//          한쪽만 값을 받는다(1회용 토큰의 원자적 소모).
//***************************************************************************
void CFileUploadHandler::VerifyUploadToken(const std::string& tokenHex, std::function<void(bool valid, const std::string& ownerPublicIdHex)> onComplete)
{
	// 채팅 서버가 만드는 형식이 아니면 Redis를 부르지 않고 거부한다.
	if( _redisService == nullptr || !FileServerKeys::IsValidUploadToken(tokenHex) )
	{
		if( onComplete )
			onComplete(false, std::string());
		return;
	}

	const std::string redisKey = FileServerKeys::kUploadTokenPrefix + tokenHex;

	// 토큰 소모 스크립트: GET+DEL을 원자적으로. 적재된 SHA가 있으면 EVALSHA로 보내
	// 스크립트 본문 전송을 피하고, 없으면 EVAL로 처리하면서 백그라운드로 적재한다.
	std::string scriptSha;
	{
		std::lock_guard<std::mutex> lock(_scriptMutex);
		scriptSha = _consumeScriptSha;
	}

	CVector<std::string> args;
	if( !scriptSha.empty() )
	{
		args.push_back("EVALSHA");
		args.push_back(scriptSha);
	}
	else
	{
		args.push_back("EVAL");
		args.push_back(kConsumeScript);
		EnsureConsumeScriptLoaded();
	}
	args.push_back("1");
	args.push_back(redisKey);

	// 콜백은 CRedisService가 생성자에 받은 CJobQueue 스레드에서 실행된다.
	// SendCommand가 false(풀 없음/큐 가득)면 콜백이 안 오므로 직접 실패 처리한다.
	const bool usedSha = !scriptSha.empty();
	if( !_redisService->SendCommand(args, [this, tokenHex, usedSha, onComplete](const RedisValue& res)
		{
			// Redis가 스크립트 캐시를 잃은 경우(재시작/FLUSH, NOSCRIPT 오류) —
			// SHA를 버리고 EVAL로 한 번 다시 시도한다(이때 스크립트도 다시 적재된다).
			if( usedSha && res.eType == ERedisType::Error )
			{
				{
					std::lock_guard<std::mutex> lock(_scriptMutex);
					_consumeScriptSha.clear();
				}
				VerifyUploadToken(tokenHex, onComplete);
				return;
			}

			// 결과는 문자열(채팅 서버가 저장한 public_id 16진) 또는 nil.
			CRedisResultSet resultSet(res);

			std::string ownerPublicIdHex;
			const bool valid = !resultSet.IsEmpty() && resultSet.GetData(ownerPublicIdHex) && !ownerPublicIdHex.empty();

			if( onComplete )
				onComplete(valid, valid ? ownerPublicIdHex : std::string());
		}) )
	{
		if( onComplete )
			onComplete(false, std::string());
	}
}

//***************************************************************************
// @brief 토큰 소모 스크립트를 Redis에 적재해 SHA를 보관한다(동시에 한 번만).
//***************************************************************************
void CFileUploadHandler::EnsureConsumeScriptLoaded()
{
	if( _redisService == nullptr || _scriptLoading.exchange(true) )
		return;

	CVector<std::string> args;
	args.push_back("SCRIPT");
	args.push_back("LOAD");
	args.push_back(kConsumeScript);

	const bool sent = _redisService->SendCommand(args, [this](const RedisValue& res)
		{
			CRedisResultSet resultSet(res);
			std::string sha;
			if( res.eType != ERedisType::Error && !resultSet.IsEmpty() && resultSet.GetData(sha) && !sha.empty() )
			{
				std::lock_guard<std::mutex> lock(_scriptMutex);
				_consumeScriptSha = sha;
			}
			_scriptLoading.store(false); // 실패했으면 다음 소모 때 다시 시도한다
		});

	if( !sent )
		_scriptLoading.store(false);
}

//***************************************************************************
// @brief relativePath로 접근 가능한 공개 URL을 만든다.
//***************************************************************************
std::string CFileUploadHandler::BuildPublicUrl(const std::string& relativePath) const
{
	return ImageRef::BuildPublicUrl(_publicBaseUrl, relativePath);
}