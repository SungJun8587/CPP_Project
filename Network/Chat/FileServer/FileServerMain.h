
//***************************************************************************
// FileServerMain.h : interface for the CFileServerMain class.
//
//***************************************************************************

#ifndef UC_FILESERVERMAIN_H
#define UC_FILESERVERMAIN_H

#include <ServerConnectInfo.h>
#include <Network/NetworkCommon.h>
#include <Redis/RedisService.h>
#include <Network/HTTP/HttpRequestParser.h>

#include "FileStorage.h"
#include "FileMetadataRepository.h"
#include "FileServerRouter.h"

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <atomic>

class CFileServerSession;

//***************************************************************************
// @class CFileServerMain
// @brief 프로필 이미지 업로드/서빙을 전담하는 파일 서버 파사드.
// @details [설계] 채팅 서버(CChatServerMain)와 완전히 같은 인프라
// (CIocpCore/CIocpServerService/CRedisService)를 재사용한다 — 세션 클래스만
// CChatSession 대신 CFileServerSession(HTTP)으로 바꿨을 뿐이다.
//
// 채팅 서버와는 직접 통신하지 않는다 — 채팅 서버가 Redis에
// "UploadToken:{tokenHex}" 키(값=계정 public_id 16진)로 발급해둔 업로드
// 토큰을, 이 서버가 같은 Redis를 조회해서 검증한다(두 서버가 공유
// 인프라를 통해서만 간접 조율하는 구조 — ChatServerMain::RequestUploadToken()
// 주석 참고). DB(user_profile_images)는 여전히 채팅 서버만 쓴다 — 이
// 서버는 업로드가 끝나도 그 결과를 어디에도 기록하지 않는다. 클라이언트가
// 업로드 완료 후 돌아온 URL을 직접 채팅 서버의 SetProfileImageUrlReq로
// 다시 등록해야 갤러리에 반영된다.
//***************************************************************************
class CFileServerMain
{
public:
	CFileServerMain() = default;
	virtual ~CFileServerMain();

	//***************************************************************************
	// @brief 파일 서버 구동.
	// @param storageDir 업로드된 이미지를 저장할 로컬 디스크 경로.
	// @param publicBaseUrl 클라이언트에게 돌려줄 URL의 기본 주소(예:
	//        "http://192.168.0.10:8081") — 이 뒤에 "/images/{path}"를 붙인다.
	// @param maxUploadBytes 업로드 가능한 최대 파일 크기(바이트).
	// @param maxProfileImageDimension 저장할 이미지의 가로/세로 허용 최댓값(픽셀) —
	//        넘으면 비율 유지한 채 줄여서 저장(ImageResizeUtil 참고).
	//***************************************************************************
	bool Start(
		const _tstring& bindIp, uint16 bindPort,
		CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
		int32 maxSessionCount, uint32 workerThreadCount,
		_tstring storageDir, std::string publicBaseUrl, int64 maxUploadBytes, int32 maxProfileImageDimension);

	void Stop();

	//***************************************************************************
	// @brief 완성된 HTTP 요청 하나를 라우팅합니다(CFileServerSession::OnRecv()에서 호출).
	//***************************************************************************
	void HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request);

	//***************************************************************************
	// @brief [추가] 설정된 업로드 크기 상한(바이트)을 반환합니다.
	// @details CFileServerSession이 CHttpRequestParser::SetBodyStreamCallback()의
	//          maxBodyLenOverride 인자로 이 값을 그대로 전달한다 — 스트리밍
	//          모드의 상한이 실제 설정값(FileServerConfig의 MaxUploadBytes)과
	//          항상 일치하도록 단일 진실 공급원을 유지하기 위함이다.
	//***************************************************************************
	int64 GetMaxUploadBytes() const { return _maxUploadBytes; }

private:
	//***************************************************************************
	// @brief 채팅 서버가 Redis 큐("FileServer:PendingDeletions")에 남겨둔
	//        "지울 파일" 목록을 주기적으로 소비하는 폴링을 시작합니다.
	// @details [설계] 두 서버는 서로 직접 통신하지 않는다 — 채팅 서버는
	//          삭제가 확정되면 이 큐에 상대 경로만 적어두고(RPUSH), 이
	//          서버가 스스로 비운다(LPOP). ChatServerMain::
	//          ScheduleFileDeletionIfOwned() 참고.
	//***************************************************************************
	void StartPendingDeletionPolling();

	//***************************************************************************
	// @brief 큐에서 하나를 꺼내(LPOP) 있으면 지우고 곧바로 다시 시도(드레인),
	//        없으면 kPollingIntervalSec 뒤 다시 폴링하도록 예약합니다.
	//***************************************************************************
	void PollPendingDeletions();

	//***************************************************************************
	// @brief 일정 시간 뒤 PollPendingDeletions()를 다시 호출하도록 예약합니다.
	// @details [알려진 한계] 이 프로젝트의 정확한 내부 타이머 API를 확신하지
	//          못해, 매 주기마다 짧게 sleep하는 detached std::thread로
	//          구현했다 — 폴링 주기가 초 단위로 느긋해서(kPollingIntervalSec)
	//          매번 스레드를 새로 띄우는 비용은 무시할 만한 수준이라고
	//          판단했다. 더 정교한 타이머 인프라가 확인되면 그걸로 바꾸는
	//          것을 권장한다.
	//***************************************************************************
	void ScheduleNextPoll();

private:
	static constexpr const char* kPendingDeletionsKey = "FileServer:PendingDeletions";
	static constexpr int kPollingIntervalSec = 5;

	std::atomic<bool>	_stopPolling{ false };

private:
	CIocpCoreRef				_iocpCore;
	CIocpServerServiceRef		_service;
	CJobQueueRef				_jobQueue;
	std::unique_ptr<CRedisService>			_redisService;
	std::unique_ptr<IFileStorage>			_fileStorage;
	std::unique_ptr<CFileMetadataRepository>	_metadataRepo;
	std::unique_ptr<CFileServerRouter>		_router;

	std::string					_publicBaseUrl;
	int64						_maxUploadBytes = 0;
	int32						_maxProfileImageDimension = 0;
};

#endif // ndef UC_FILESERVERMAIN_H