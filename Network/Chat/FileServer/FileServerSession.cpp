
//***************************************************************************
// FileServerSession.cpp: implementation of the CFileServerSession class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerSession.h"

//***************************************************************************
// @brief CFileServerSession 클래스의 생성자
// @details [추가] 헤더 완료 콜백을 여기서 한 번만 건다 — CHttpRequestParser::
//          Reset()이 이 콜백을 지우지 않으므로, 이 세션이 살아있는 동안
//          (keep-alive로 여러 요청을 처리해도) 매 요청마다 다시 걸 필요가
//          없다. 콜백 자체는 "이번 요청이 스트리밍 대상인지" 매번 새로
//          판단한다(SetupUploadStreamingIfNeeded() 참고).
//***************************************************************************
CFileServerSession::CFileServerSession(CFileServerMain* server)
	: _server(server)
{
	_parser.SetHeadersCompleteCallback([this](CHttpRequestParser& parser)
		{
			SetupUploadStreamingIfNeeded(parser);
		});
}

//***************************************************************************
// @brief 연결 수립 시점 훅 — 파일 서버는 로그인 개념이 없어 특별히 할 일이 없다.
//***************************************************************************
void CFileServerSession::OnConnected()
{
}

//***************************************************************************
// @brief 연결 종료 시점 훅 — 진행 중이던 업로드 임시 파일과 다운로드 상태를 정리한다.
//***************************************************************************
void CFileServerSession::OnDisconnected()
{
	CleanupTempUpload();

	{
		std::lock_guard<std::mutex> lock(_downloadMutex);
		_download.reset();
	}
	_inflightBytes.store(0);
	_closeAfterSend.store(false);

	// 인증 결과를 기다리던 최종 처리는 응답할 곳이 없으므로 버린다(임시 파일 가드가 정리된다).
	if( _auth != nullptr )
	{
		std::function<void(bool, const std::string&)> abandoned;
		{
			std::lock_guard<std::mutex> lock(_auth->mutex);
			abandoned = std::move(_auth->waiter);
			_auth->waiter = nullptr;
		}
		_auth.reset();
	}
}

//***************************************************************************
// @brief 이번 요청의 업로드 토큰 인증 결과를 받는다(헤더 설명 참고).
//***************************************************************************
void CFileServerSession::WaitUploadAuth(std::function<void(bool valid, const std::string& ownerPublicIdHex)> onResult)
{
	std::shared_ptr<SUploadAuth> auth = _auth;
	if( auth == nullptr )
	{
		onResult(false, std::string());
		return;
	}

	bool ready = false;
	bool valid = false;
	std::string owner;
	{
		std::lock_guard<std::mutex> lock(auth->mutex);
		if( auth->state != SUploadAuth::EState::Pending )
		{
			ready = true;
			valid = (auth->state == SUploadAuth::EState::Valid);
			owner = auth->owner;
		}
		else
		{
			auth->waiter = std::move(onResult);
		}
	}

	if( ready )
		onResult(valid, owner);
}

//***************************************************************************
// @brief 열려있는 업로드 임시 파일을 닫고 지운다.
//***************************************************************************
void CFileServerSession::CleanupTempUpload()
{
	if( _uploadFileStream.is_open() )
		_uploadFileStream.close();

	if( _pendingUpload.hasFile && !_pendingUpload.tempFilePath.empty() )
	{
		std::error_code ec;
		std::filesystem::remove(_pendingUpload.tempFilePath, ec); // 이미 없으면 조용히 무시
	}
}

//***************************************************************************
// @brief 업로드를 거부하고 연결을 끊는다(남은 본문은 받지 않는다).
//***************************************************************************
void CFileServerSession::RejectUpload(const TCHAR* reason)
{
	if( _uploadRejected.exchange(true) )
		return;

	LOG_WARNING(_T("CFileServerSession: 업로드 거부 (%s)"), reason);
	Disconnect(Iocp::CloseReason::ForcedClose);
}

//***************************************************************************
// @brief 수신 바이트를 CHttpRequestParser에 흘려보내고, 완성된 요청이
//        생기면 CFileServerMain에 라우팅을 위임합니다.
// @details [설계] 채팅 서버(ChatSession::OnRecv)는 고정 헤더+size 기반으로
// "패킷 하나가 왔는지"를 판단하지만, HTTP는 그런 고정 프레이밍이 없다 —
// 그래서 상태를 세션 안에 들고 있는 CHttpRequestParser에게 매번 있는
// 그대로의 바이트를 넘기고, 파서가 알아서 "요청 하나가 완성됐는지"를
// 판단하게 한다. 반환값(processedLen)은 파서가 내부적으로 이미 흡수한
// 바이트 수와 같다 — 파싱 중간 상태는 전부 _parser 멤버 안에 남아있으므로,
// 프레임워크가 별도로 바이트를 보관해줄 필요가 없다.
// @details [수정 — 대용량 업로드] Complete 시점에 _multipartParser가
//          있었다면(=이번 요청이 스트리밍 업로드였다면) IsDone() 결과를
//          _pendingUpload.multipartOk에 반영한 뒤 HandleRequest()를
//          호출한다 — FileUploadHandler가 그 값을 보고 성공/실패를 판단한다.
//          그 다음 _multipartParser를 리셋해서 다음 요청에 이전 상태가
//          새어 들어가지 않게 한다(_pendingUpload 자체는
//          SetupUploadStreamingIfNeeded()가 매 요청 시작 시 새로 초기화함).
//***************************************************************************
int32 CFileServerSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processedLen = 0;

	while( processedLen < len )
	{
		const char* data = reinterpret_cast<const char*>(buffer + processedLen);
		const size_t remaining = static_cast<size_t>(len - processedLen);

		const HTTP::EParseState state = _parser.Feed(data, remaining);
		const size_t consumed = _parser.GetLastFeedConsumed();
		processedLen += static_cast<int32>(consumed);

		if( state == HTTP::EParseState::Complete )
		{
			if( _multipartParser != nullptr )
				_pendingUpload.multipartOk = _multipartParser->IsDone();

			const bool keepAlive = _parser.IsKeepAlive();

			// 요청 수신이 끝났음을 표시한다 — 이 뒤에 도착하는 토큰 인증 실패는 연결을 끊지 않고
			// WaitUploadAuth()로 기다리는 최종 처리가 401로 응답한다.
			if( _auth != nullptr )
			{
				std::lock_guard<std::mutex> lock(_auth->mutex);
				_auth->requestComplete = true;
			}

			if( _uploadRejected.load() )
			{
				// 이미 거부되어 연결을 끊는 중 — 응답 없이 정리만 한다.
				CleanupTempUpload();
				_multipartParser.reset();
				_parser.Reset();
				break;
			}

			auto sessionRef = std::static_pointer_cast<CFileServerSession>(shared_from_this());
			if( _server != nullptr )
				_server->HandleRequest(sessionRef, _parser);

			// 다음 요청을 위해 이번 요청 한정 상태를 정리한다. _pendingUpload
			// 자체는 다음 요청의 SetupUploadStreamingIfNeeded()가 시작하자마자
			// 새로 초기화하므로 여기서 굳이 지울 필요는 없지만, _multipartParser/
			// _uploadFileStream은 리소스(파일 핸들 등)를 쥐고 있으므로 명시적으로
			// 정리한다.
			_multipartParser.reset();
			if( _uploadFileStream.is_open() )
				_uploadFileStream.close();

			_parser.Reset();

			if( !keepAlive )
			{
				// 응답의 마지막 바이트가 실제로 나간 뒤에 닫는다(OnSend에서 확인).
				_closeAfterSend.store(true);
				TryCloseAfterSend();
				break;
			}

			// 다운로드 본문을 보내는 중이면 응답 순서가 섞이지 않게 이어지는
			// 요청은 처리하지 않는다(HTTP 파이프라이닝 미지원).
			{
				std::lock_guard<std::mutex> lock(_downloadMutex);
				if( _download != nullptr )
					break;
			}
			// keep-alive면 루프를 계속 돌려 같은 recv 버퍼에 파이프라이닝된
			// 다음 요청의 선두 바이트가 있는지 마저 처리한다.
		}
		else if( state == HTTP::EParseState::Error )
		{
			// 형식이 깨진 요청 — 응답 없이 바로 연결을 끊는다. 스트리밍 중이던
			// 임시 파일은 OnDisconnected()와 별개로 여기서도 정리한다.
			CleanupTempUpload();

			Disconnect(Iocp::CloseReason::InternalError);
			break;
		}
		else
		{
			// 아직 요청이 덜 도착함 — 이번에 받은 바이트는 파서가 전부
			// 내부 버퍼로 흡수했을 것이므로(consumed == remaining이 보통),
			// 더 받을 때까지 대기.
			break;
		}
	}

	return processedLen;
}

//***************************************************************************
// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(모든 핸들러 공용).
//***************************************************************************
void CFileServerSession::SendSimple(int statusCode, const std::string& statusText,
	const std::string& contentType, const std::string& body, bool keepAlive)
{
	CHttpResponseBuilder builder;
	builder.SetStatus(statusCode, statusText)
		.AddHeader("Content-Type", contentType)
		.AddHeader("Connection", keepAlive ? "keep-alive" : "close")
		.SetBody(body);

	auto [data, len] = builder.Build();
	SendRaw(data, len);
}

//***************************************************************************
// @brief 완성된 HTTP 응답(상태줄+헤더+바디)을 그대로 전송합니다.
// @details CIocpSession::Send()는 한 번에 Iocp::kMaxSendSize(65535)바이트까지 받아 내부에서
//          SendBuffer 청크 단위로 나눠 한 번의 락 구간에 큐잉하므로, 여기서는 그 상한 단위로만 나눈다.
//          TCP는 스트림이라 나눠 보내도 수신측에는 하나로 보낸 것과 같다.
//***************************************************************************
void CFileServerSession::SendRaw(const char* data, size_t len)
{
	for( size_t offset = 0; offset < len; )
	{
		const size_t take = (std::min)(static_cast<size_t>(Iocp::kMaxSendSize), len - offset);

		// 완료 통지(OnSend)보다 먼저 올려둬야 카운터가 음수로 내려가지 않는다.
		_inflightBytes.fetch_add(static_cast<int64_t>(take));
		if( !Send(data + offset, static_cast<uint16>(take)) )
		{
			_inflightBytes.fetch_sub(static_cast<int64_t>(take)); // 큐잉 실패(연결 끊김 등)
			return;
		}

		offset += take;
	}
}

//***************************************************************************
// @brief 송신 완료 통지 — 미전송 바이트를 줄이고 다음 다운로드 청크/종료를 처리한다.
//***************************************************************************
void CFileServerSession::OnSend(int32 len)
{
	// OnDisconnected()가 0으로 리셋한 뒤 늦게 온 완료 통지로 음수가 되지 않게 보정한다.
	if( _inflightBytes.fetch_sub(static_cast<int64_t>(len)) < static_cast<int64_t>(len) )
		_inflightBytes.store(0);

	PumpDownload();
	TryCloseAfterSend();
}

//***************************************************************************
// @brief 파일 본문 전송을 시작한다.
//***************************************************************************
void CFileServerSession::BeginDownload(std::unique_ptr<CFileStream> stream, int64_t length, bool keepAlive)
{
	{
		std::lock_guard<std::mutex> lock(_downloadMutex);
		_download = std::make_unique<SDownloadState>();
		_download->stream = std::move(stream);
		_download->remaining = length;
		_download->keepAlive = keepAlive;
	}

	PumpDownload();
}

//***************************************************************************
// @brief 미전송 바이트가 상한 아래인 동안 다음 청크를 읽어 보낸다.
//***************************************************************************
void CFileServerSession::PumpDownload()
{
	constexpr int64_t kChunkBytes = 64 * 1024;
	constexpr int64_t kHighWaterBytes = 256 * 1024;

	bool finishedNonKeepAlive = false;
	bool abortDownload = false;
	{
		std::lock_guard<std::mutex> lock(_downloadMutex);
		while( _download != nullptr && _inflightBytes.load() < kHighWaterBytes )
		{
			if( _download->remaining <= 0 )
			{
				finishedNonKeepAlive = !_download->keepAlive;
				_download.reset();
				break;
			}

			const size_t toRead = static_cast<size_t>((std::min)(kChunkBytes, _download->remaining));
			std::string& chunk = _download->chunk; // 청크 버퍼는 다운로드 동안 재사용한다
			chunk.resize(toRead);
			const size_t actuallyRead = _download->stream->ReadChunk(reinterpret_cast<BYTE*>(chunk.data()), toRead);
			if( actuallyRead == 0 )
			{
				// 헤더에서 약속한 Content-Length를 채울 수 없다(파일이 그 사이 바뀜 등)
				// — 클라이언트가 타임아웃까지 기다리지 않도록 연결을 끊는다.
				LOG_ERROR(_T("CFileServerSession::PumpDownload: 예상보다 일찍 EOF (남은 %lld바이트)"), static_cast<long long>(_download->remaining));
				_download.reset();
				abortDownload = true;
				break;
			}

			chunk.resize(actuallyRead);
			_download->remaining -= static_cast<int64_t>(actuallyRead);
			SendRaw(chunk);
		}
	}

	// Disconnect()는 이미 종료가 시작된 세션에서 OnDisconnected()를 같은 스레드에서 바로 부를 수 있고,
	// 그 안에서 _downloadMutex를 잡으므로 락을 놓은 뒤에 호출한다.
	if( abortDownload )
	{
		Disconnect(Iocp::CloseReason::InternalError);
		return;
	}

	if( finishedNonKeepAlive )
	{
		_closeAfterSend.store(true);
		TryCloseAfterSend();
	}
}

//***************************************************************************
// @brief Connection: close 응답이 모두 나갔으면 연결을 끊는다.
//***************************************************************************
void CFileServerSession::TryCloseAfterSend()
{
	if( !_closeAfterSend.load() || _inflightBytes.load() > 0 )
		return;

	{
		std::lock_guard<std::mutex> lock(_downloadMutex);
		if( _download != nullptr )
			return;
	}

	if( _closeAfterSend.exchange(false) )
		Disconnect(Iocp::CloseReason::ForcedClose);
}

//***************************************************************************
// @brief 이번 요청이 POST /upload + 유효한 boundary인지 확인하고, 맞으면
//        본문 처리를 스트리밍 모드로 전환한다.
// @details [설계] 여기서 스트리밍 여부를 파일 크기가 아니라 "경로가
//          /upload인지"만으로 결정한다 — 크기는 아직(Content-Length는
//          이미 헤더에 있지만) 파일이 진짜 몇 바이트인지와 무관하게,
//          POST /upload는 항상 같은 방식(스트리밍)으로 처리하는 게
//          FileUploadHandler 쪽 코드를 "스트리밍 경로/비스트리밍 경로"
//          둘로 안 쪼개도 되게 해준다 — 작은 파일이어도 스트리밍 오버헤드는
//          미미하다(임시 파일 하나 여닫는 정도).
//***************************************************************************
void CFileServerSession::SetupUploadStreamingIfNeeded(CHttpRequestParser& parser)
{
	// 새 요청 시작 — 이전 요청의 인증 상태를 이어받지 않는다(그 요청의 콜백은 자기 상태 객체에만 쓴다).
	_auth.reset();

	// 매 요청마다 이전 요청의 잔재가 안 남게 새로 초기화.
	_pendingUpload = SPendingUploadResult{};
	_multipartParser.reset();
	_currentPartIsFile = false;
	_currentSmallFieldName.clear();
	_currentSmallFieldValue.clear();

	if( parser.GetMethod() != "POST" || parser.GetPath() != "/upload" )
		return; // 업로드 요청이 아님 — 기존 방식(비스트리밍) 그대로 둔다

	std::string boundary;
	if( !HTTP::ExtractBoundary(parser.FindHeader("Content-Type"), boundary) )
		return; // boundary가 없음 — FileUploadHandler가 이 경우도 처리(400 응답)

	_pendingUpload.attempted = true;

	_multipartParser = std::make_unique<HTTP::CMultipartStreamParser>(boundary);
	_multipartParser->SetCallbacks(
		[this](const HTTP::SMultipartPartInfo& info) { OnMultipartPartBegin(info); },
		[this](const char* data, size_t len) { OnMultipartPartData(data, len); },
		[this]() { OnMultipartPartEnd(); });

	const size_t maxUploadBytes = (_server != nullptr) ? static_cast<size_t>(_server->GetMaxUploadBytes()) : 0;

	parser.SetBodyStreamCallback(
		[this](const char* data, size_t len) { OnUploadBodyChunk(data, len); },
		maxUploadBytes);
}

//***************************************************************************
// @brief 본문 바이트를 그대로 _multipartParser에 흘려보낸다.
//***************************************************************************
void CFileServerSession::OnUploadBodyChunk(const char* data, size_t len)
{
	if( _multipartParser == nullptr )
		return;

	if( !_multipartParser->Feed(data, len) )
	{
		// 형식이 깨짐 — 이후 파트 콜백은 더 안 온다. _pendingUpload.multipartOk는
		// OnRecv()가 Complete 시점에 IsDone()==false로 확인해 자연스럽게
		// false로 남으므로, 여기서는 열려있던 파일 스트림만 정리한다.
		if( _uploadFileStream.is_open() )
			_uploadFileStream.close();
	}
}

//***************************************************************************
// @brief 새 멀티파트 파트의 헤더가 파싱된 직후 호출됨 — filename 유무로
//        "file" 파트인지 판단해 이후 데이터를 어디로 보낼지 결정한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartBegin(const HTTP::SMultipartPartInfo& info)
{
	if( !info.filename.empty() )
	{
		_currentPartIsFile = true;

		// 파일 파트는 첫 번째 것만 받는다 — 추가 파일 파트는 무시한다(임시
		// 파일이 새로 만들어져 tempFilePath를 덮어쓰고 앞의 파일이 고아로
		// 남는 것을 방지). 첫 파트는 이미 닫혀있으므로 Data/End는 자연히 no-op.
		if( _pendingUpload.hasFile )
			return;

		// 토큰 파트가 파일보다 먼저 와야 한다 — 인증 전에는 디스크에 쓰지 않는다.
		if( !_pendingUpload.hasToken )
		{
			RejectUpload(_T("token part must precede file part"));
			return;
		}

		// 임시 파일명은 무작위로 — 동시 업로드(여러 세션) 간 충돌 방지.
		BYTE randomBytes[8] = {};
		if( !Crypto::CCryptoUtil::GenerateRandomBytes(randomBytes, sizeof(randomBytes)) )
		{
			// 실패하면 0으로 남은 이름이 모든 세션에서 같아져 동시 업로드가 서로의 임시 파일을 덮어쓴다.
			RejectUpload(_T("temp file name generation failed"));
			return;
		}
		const std::string randomHex = Crypto::CCryptoUtil::ToHex(randomBytes, sizeof(randomBytes));

		// 저장소와 같은 볼륨의 임시 디렉터리 — 저장이 rename 한 번으로 끝난다.
		const std::filesystem::path tempDir = (_server != nullptr) ? std::filesystem::path(_server->GetUploadTempDir()) : std::filesystem::path(".");
		const std::string tempPath = (tempDir / ("upload_" + randomHex + ".tmp")).string();

		_uploadFileStream.open(tempPath, std::ios::binary | std::ios::trunc);
		if( _uploadFileStream.is_open() )
		{
			_pendingUpload.hasFile = true;
			_pendingUpload.tempFilePath = tempPath;
			_pendingUpload.originalFileName = info.filename;
			_pendingUpload.fileContentType = info.contentType;
			_pendingUpload.fileSizeBytes = 0;
		}
		else
		{
			LOG_ERROR(_T("CFileServerSession::OnMultipartPartBegin: 임시 파일 열기 실패 (%hs)"), tempPath.c_str());
			RejectUpload(_T("temp file open failed"));
		}
	}
	else
	{
		_currentPartIsFile = false;
		_currentSmallFieldName = info.name;
		_currentSmallFieldValue.clear();
	}
}

//***************************************************************************
// @brief 현재 파트의 데이터 바이트 — "file" 파트면 임시 파일에 바로 쓰고,
//        아니면(작은 필드) 메모리 버퍼에 누적한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartData(const char* data, size_t len)
{
	if( _uploadRejected.load() )
		return; // 거부된 업로드의 본문은 버린다

	if( _currentPartIsFile )
	{
		if( _uploadFileStream.is_open() )
		{
			_uploadFileStream.write(data, static_cast<std::streamsize>(len));
			if( !_uploadFileStream )
			{
				// 디스크 가득 참 등 — 잘린 파일이 저장소에 올라가 성공으로 응답되지 않게 즉시 거부한다.
				RejectUpload(_T("temp file write failed"));
				return;
			}
			_pendingUpload.fileSizeBytes += static_cast<int64_t>(len);
		}
	}
	else if( _currentSmallFieldName == "token" )
	{
		// 토큰은 16진 64자 — 상한(그 두 배)을 두어 작은 필드로 메모리를 채우는 것을 막는다.
		constexpr size_t kMaxTokenLen = FileServerKeys::kUploadTokenHexLen * 2;
		if( _currentSmallFieldValue.size() + len > kMaxTokenLen )
			RejectUpload(_T("token field too long"));
		else
			_currentSmallFieldValue.append(data, len);
	}
	// 다른 작은 필드는 쓰지 않으므로 버퍼에 쌓지 않는다.
}

//***************************************************************************
// @brief 현재 파트의 데이터가 끝남 — "file" 파트면 임시 파일을 닫고,
//        작은 필드면("token") 결과에 반영한다.
//***************************************************************************
void CFileServerSession::OnMultipartPartEnd()
{
	if( _currentPartIsFile )
	{
		if( _uploadFileStream.is_open() )
		{
			_uploadFileStream.close(); // 마지막 버퍼 flush 실패도 failbit로 드러난다
			if( _uploadFileStream.fail() )
				RejectUpload(_T("temp file flush failed"));
		}
	}
	else if( _currentSmallFieldName == "token" )
	{
		_pendingUpload.tokenValue = _currentSmallFieldValue;
		_pendingUpload.hasToken = FileServerKeys::IsValidUploadToken(_currentSmallFieldValue);

		// 토큰은 여기서 바로 소모한다(GET+DEL 원자적) — 같은 토큰으로 여러 연결이 동시에 업로드를 시작할 수
		// 없고, 무효면 본문을 더 받기 전에 연결을 끊는다. 소유자는 결과로 받아 최종 저장에 쓴다.
		if( _server != nullptr && _pendingUpload.hasToken )
		{
			auto auth = std::make_shared<SUploadAuth>();
			_auth = auth;

			std::weak_ptr<CFileServerSession> weak = std::static_pointer_cast<CFileServerSession>(shared_from_this());
			_server->VerifyUploadToken(_pendingUpload.tokenValue,
				[weak, auth](bool valid, const std::string& ownerPublicIdHex)
				{
					std::function<void(bool, const std::string&)> waiter;
					bool rejectNow = false;
					{
						std::lock_guard<std::mutex> lock(auth->mutex);
						auth->state = valid ? SUploadAuth::EState::Valid : SUploadAuth::EState::Invalid;
						if( valid )
							auth->owner = ownerPublicIdHex;
						waiter = std::move(auth->waiter);
						auth->waiter = nullptr;
						rejectNow = !valid && !waiter && !auth->requestComplete;
					}

					if( waiter )
					{
						waiter(valid, valid ? ownerPublicIdHex : std::string());
						return;
					}

					// 본문을 받는 중에 무효로 판명 — 더 받지 않고 끊는다.
					if( rejectNow )
					{
						if( auto self = weak.lock() )
							self->RejectUpload(_T("invalid upload token"));
					}
				});
		}
		else if( !_pendingUpload.hasToken )
		{
			RejectUpload(_T("malformed upload token"));
		}
	}
	// 다른 작은 필드는 무시한다.
}