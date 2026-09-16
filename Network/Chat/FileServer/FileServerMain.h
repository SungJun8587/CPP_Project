
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

#include "IImageStorage.h"

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
	// @param maxImageDimension 저장할 이미지의 가로/세로 허용 최댓값(픽셀) —
	//        넘으면 비율 유지한 채 줄여서 저장(ImageResizeUtil 참고).
	//***************************************************************************
	bool Start(
		const _tstring& bindIp, uint16 bindPort,
		CVector<CRedisNode> redisNodeVec, int32 redisPoolSize,
		int32 maxSessionCount, uint32 workerThreadCount,
		_tstring storageDir, std::string publicBaseUrl, int32 maxUploadBytes, int32 maxImageDimension);

	void Stop();

	//***************************************************************************
	// @brief 완성된 HTTP 요청 하나를 라우팅합니다(CFileServerSession::OnRecv()에서 호출).
	//***************************************************************************
	void HandleRequest(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request);

private:
	//***************************************************************************
	// @brief POST /upload 처리 — multipart/form-data에서 token/file 필드를
	//        뽑아 Redis로 토큰을 검증하고, 성공하면 저장 후 URL을 응답한다.
	//***************************************************************************
	void HandleUpload(std::shared_ptr<CFileServerSession> session, const CHttpRequestParser& request, bool keepAlive);

	//***************************************************************************
	// @brief GET /images/{path} 처리 — 저장된 파일을 그대로 응답 본문에 실어 보낸다.
	//***************************************************************************
	void HandleGetImage(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive);

	//***************************************************************************
	// @brief DELETE /images/{path} 처리 — 저장된 파일을 지운다.
	// @details [설계] 이 요청 자체엔 별도 인증이 없다 — 채팅 서버가
	//          DeleteProfileImageHandler.cpp에서 DB 삭제에 성공한 뒤에만
	//          내부적으로 호출하는 것을 전제로 한다(사용자가 직접 이
	//          엔드포인트를 두드릴 경로가 없음 — 클라이언트 프로토콜엔
	//          이 요청이 아예 없다). 인터넷에 노출되는 배포라면 두 서버
	//          사이에서만 통하는 별도 인증(공유 비밀 헤더 등)을 추가하는
	//          게 안전하다 — 지금은 데모 범위에서 생략.
	//***************************************************************************
	void HandleDeleteImage(std::shared_ptr<CFileServerSession> session, const std::string& relativePath, bool keepAlive);

	//***************************************************************************
	// @brief Redis에서 업로드 토큰을 조회/소모(1회용 — 검증 성공 시 삭제)합니다.
	// @param onComplete success==true면 outOwnerPublicIdHex가 채워진다.
	//***************************************************************************
	void VerifyAndConsumeUploadToken(const std::string& tokenHex, std::function<void(bool success, const std::string& ownerPublicIdHex)> onComplete);

	//***************************************************************************
	// @brief relativePath로 접근 가능한 공개 URL을 만든다.
	//***************************************************************************
	std::string BuildPublicUrl(const std::string& relativePath) const;

	//***************************************************************************
	// @brief 확장자(".png" 등)로부터 Content-Type을 추정한다. 모르는
	//        확장자면 "application/octet-stream".
	//***************************************************************************
	static std::string GuessContentType(const std::string& path);

	//***************************************************************************
	// @brief 텍스트 본문 HTTP 응답 하나를 조립해서 전송한다(상태줄+헤더+바디).
	// @param keepAlive request.IsKeepAlive() 값을 그대로 전달 — 세션의
	//        OnRecv()가 요청 기준으로 연결 유지/종료를 결정하므로, 응답의
	//        Connection 헤더도 반드시 그 값과 일치시켜야 한다(안 그러면
	//        클라이언트가 "keep-alive"라고 믿는 연결을 서버가 끊어버리는
	//        불일치가 생김).
	//***************************************************************************
	static void SendSimpleResponse(std::shared_ptr<CFileServerSession> session, int statusCode, const std::string& statusText,
		const std::string& contentType, const std::string& body, bool keepAlive);

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
	std::unique_ptr<CRedisService>	_redisService;
	std::unique_ptr<IImageStorage>	_imageStorage;

	std::string					_publicBaseUrl;
	int32						_maxUploadBytes = 0;
	int32						_maxImageDimension = 0;
};

#endif // ndef UC_FILESERVERMAIN_H