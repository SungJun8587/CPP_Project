
//***************************************************************************
// ChatServerMainAccount.cpp : CChatServerMain 구현 — 계정 관련
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"
#include "DBSignupRequest.h"
#include "DBChangeNicknameRequest.h"
#include "DBSetProfileImageUrlRequest.h"
#include "DBSelectProfileImageRequest.h"
#include "DBDeleteProfileImageRequest.h"

#include <cstring>

//***************************************************************************
// @brief 회원가입/재접속 검증을 DB 비동기 워커에 요청합니다.
// @details
// [스레드 이관] AccountDBHandler.cpp의 핸들러는 DB 비동기 워커 스레드
// (MEMBER_DB_ASYNC 내부 워커)에서 실행되며, 그 안에서 req->onComplete()를
// 직접 호출한다. 여기서 그 결과를 곧바로 넘기는 대신 _jobQueue로 한 번
// 이관해 CRedisService 콜백과 동일한 스레드 모델로 통일한다.
//
// [설계] 요청 생성+백프레셔+AddOutstandingRequest/Push 대칭 처리를
// PushDBAsyncRequest() 헬퍼로 위임했다. COdbcAsyncSrv 자신은 이제
// Instance()가 없어서(도메인별 다중 인스턴스를 CDbServiceManager가
// 소유) 인스턴스를 직접 넘기는 오버로드를 쓴다. 이 헬퍼의 백프레셔는
// COdbcAsyncSrv::WaitPushCapacity()(블로킹) 대신 큐 크기를 논블로킹으로
// 확인만 하고 초과 시 즉시 실패시키는 방식이다 — 이 함수가 IOCP 워커
// 스레드(ChatLoginHandler.cpp::HandleLoginReq())에서 호출되므로, 여기서
// 블로킹 대기를 걸면 그 워커가 담당하는 다른 세션들의 I/O 처리까지 함께
// 지연되기 때문이다.
//***************************************************************************
void CChatServerMain::RequestSignup(
	std::shared_ptr<CChatSession> session,
	const std::string& nickname,
	bool hasToken,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::array<BYTE, kTokenBytes>& token,
	std::function<void(ELoginResult result, const std::string& nickname,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& newToken,
		const std::string& profileImageUrl)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// DB 워커 스레드 → JobQueue로 이관하는 콜백. CChatSession의 public
	// API만 쓰면 어느 스레드가 실제로 이 잡을 실행하든 안전하다.
	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::string& completedNickname,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::array<BYTE, kTokenBytes>& newToken,
		const std::string& profileImageUrl)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] DB(AccountDBHandler.cpp)가 돌려준 image_ref가 상대
			// 경로("/images/...")로 저장돼 있으면 클라이언트에게 나가기
			// 전에 완전한 URL로 복원한다 — ToDisplayImageUrl() 참고.
			const std::string displayProfileImageUrl = ToDisplayImageUrl(profileImageUrl);

			jobQueue->DoAsync([onComplete, result, completedNickname, completedPublicId, newToken, displayProfileImageUrl]()
				{
					if( onComplete )
						onComplete(result, completedNickname, completedPublicId, newToken, displayProfileImageUrl);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SIGNUP_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_Signup,
		[&nickname, hasToken, &publicId, &token, dispatchToJobQueue](ST_SIGNUP_REQ* req)
		{
			const size_t copyLen = (std::min)(nickname.size(), sizeof(req->nickname) - 1);
			::memcpy(req->nickname, nickname.data(), copyLen);
			// 나머지는 {} 초기화로 이미 0-채움 → NUL 종단 보장

			req->hasToken = hasToken;
			if( hasToken )
			{
				::memcpy(req->publicId, publicId.data(), publicId.size());
				::memcpy(req->token, token.data(), token.size());
			}

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		// 게시 실패 — 백프레셔로 거부됐거나(큐 포화) Push() 자체가
		// 실패(서비스 종료 시점 등)한 경우. 어느 쪽이든 요청이 큐에
		// 들어가지 않았으므로 콜백을 직접(이 스레드에서) 호출해 세션이
		// 응답을 무한정 기다리지 않게 한다.
		if( onComplete )
			onComplete(ELoginResult::DbError, nickname, std::array<BYTE, kPublicIdBytes>{}, std::array<BYTE, kTokenBytes>{}, std::string());
	}
}

//***************************************************************************
// @brief 닉네임 변경을 DB 비동기 워커에 요청합니다.
// @details RequestSignup()과 완전히 동일한 구조(백프레셔/JobQueue 이관)를
// 따른다 — 차이는 요청/콜백 타입뿐이다.
//***************************************************************************
void CChatServerMain::RequestChangeNickname(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::string& newNickname,
	std::function<void(ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newNickname)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [jobQueue, onComplete](ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::string& completedNewNickname)
		{
			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedPublicId, completedNewNickname]()
				{
					if( onComplete )
						onComplete(result, completedPublicId, completedNewNickname);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_CHANGE_NICKNAME_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ChangeNickname,
		[&publicId, &newNickname, dispatchToJobQueue](ST_CHANGE_NICKNAME_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());

			const size_t newCopyLen = (std::min)(newNickname.size(), sizeof(req->newNickname) - 1);
			::memcpy(req->newNickname, newNickname.data(), newCopyLen);

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, publicId, newNickname);
	}
}

//***************************************************************************
// @brief 프로필 이미지 URL 설정을 DB 비동기 워커에 요청합니다.
// @details RequestChangeNickname()과 완전히 동일한 구조 — 차이는 요청/콜백 타입뿐이다.
//***************************************************************************
void CChatServerMain::RequestSetProfileImageUrl(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::string& newUrl,
	std::function<void(ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::string& newUrl,
		int64 newImageId)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// [추가] DB엔 "{fileServerUrl}/images/{경로}" 형태 대신 "/images/{경로}"만
	// 저장한다 — ToStorableImageRef() 참고. 외부 URL이면 그대로 통과된다.
	const std::string storableUrl = ToStorableImageRef(newUrl);

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result,
		const std::array<BYTE, kPublicIdBytes>& completedPublicId,
		const std::string& completedNewUrl,
		int64 newImageId)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] DB(SetProfileImageUrlDBHandler.cpp)는 방금 저장한 값을
			// 그대로 에코해주므로, 상대 경로로 저장했다면 이 시점의
			// completedNewUrl도 상대 경로다 — 클라이언트에겐 항상 완전한
			// URL을 줘야 하므로 여기서 다시 복원한다.
			const std::string displayUrl = ToDisplayImageUrl(completedNewUrl);

			jobQueue->DoAsync([onComplete, result, completedPublicId, displayUrl, newImageId]()
				{
					if( onComplete )
						onComplete(result, completedPublicId, displayUrl, newImageId);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SET_PROFILE_IMAGE_URL_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SetProfileImageUrl,
		[&publicId, &storableUrl, dispatchToJobQueue](ST_SET_PROFILE_IMAGE_URL_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());

			const size_t urlCopyLen = (std::min)(storableUrl.size(), sizeof(req->url) - 1);
			::memcpy(req->url, storableUrl.data(), urlCopyLen);

			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, publicId, newUrl, 0);
	}
}

//***************************************************************************
// @brief 파일 서버 업로드용 임시 토큰을 발급합니다.
// @details Redis에 "UploadToken:{tokenHex}" 키로 이 계정의 public_id(16진)를
// 값으로, TTL 60초로 저장한다. 파일 서버는 이 키를 조회해서 토큰을
// 검증한다 — 채팅 서버와 파일 서버는 서로 직접 통신하지 않는다.
//***************************************************************************
void CChatServerMain::RequestUploadToken(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	std::function<void(bool success, const std::string& uploadToken, const std::string& fileServerUrl)> onComplete)
{
	if( session == nullptr )
		return;

	if( _redisService == nullptr || _fileServerUrl.empty() )
	{
		// Redis가 없거나 파일 서버 주소가 설정 안 돼 있으면 업로드 기능
		// 자체를 못 쓰는 상태 — 실패로 응답한다.
		if( onComplete )
			onComplete(false, std::string(), std::string());
		return;
	}

	// 토큰 = 무작위 32바이트를 16진 인코딩(64자) — 추측 불가능한 값이어야
	// 다른 사람이 남의 토큰을 짐작해서 도용할 수 없다.
	BYTE randomBytes[32] = {};
	if( !Crypto::CCryptoUtil::GenerateRandomBytes(randomBytes, sizeof(randomBytes)) )
	{
		if( onComplete )
			onComplete(false, std::string(), std::string());
		return;
	}

	const std::string tokenHex = Crypto::CCryptoUtil::ToHex(randomBytes, sizeof(randomBytes));
	const std::string publicIdHex = Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size());
	const std::string redisKey = "UploadToken:" + tokenHex;
	const std::string fileServerUrl = _fileServerUrl;

	// SET key value EX seconds — 한 커맨드로 등록+TTL을 동시에 건다
	// (RedisServerHeartbeat.cpp의 HSET+EXPIRE 두 단계보다 간단 — 여긴 필드가
	// 값 하나뿐이라 SET의 EX 옵션만으로 충분하다).
	CVector<std::string> args;
	args.push_back("SET");
	args.push_back(redisKey);
	args.push_back(publicIdHex);
	args.push_back("EX");
	args.push_back("60");

	_redisService->SendCommand(args, [onComplete, tokenHex, fileServerUrl](const RedisValue& /*res*/)
		{
			// [참고] RedisValue의 성공/에러 판별 API를 이 헤더만으론 확인 못해,
			// OnUserLogin()과 마찬가지로 콜백이 왔다는 것 자체를 "등록 완료"로
			// 본다(TODO: 에러 체크 메서드가 있다면 감싸는 것을 권장).
			// [수정] CRedisService::SendCommand()의 콜백은 이미 생성자에
			// 넘긴 CJobQueue 스레드에서 안전하게 실행된다(RedisService.cpp
			// 내부에서 DoAsync로 이관해줌) — 여기서 또 감싸는 건 불필요한
			// 이중 디스패치였다.
			if( onComplete )
				onComplete(true, tokenHex, fileServerUrl);
		});
}

//***************************************************************************
// @brief 로그인된 계정이 갖고 있는 프로필 이미지 전체 목록을 조회합니다.
//***************************************************************************
void CChatServerMain::RequestListProfileImages(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	std::function<void(ELoginResult result, const std::vector<SProfileImageEntry>& images)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::vector<SProfileImageEntry>& images)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] 목록 항목마다 image_ref가 상대 경로로 저장돼 있으면
			// 완전한 URL로 복원한다 — RequestSignup()과 동일한 이유
			// (ToDisplayImageUrl() 참고). 벡터를 복사해서 그 자리에서
			// 바꿔치기한다 — 원본 images는 DB 콜백 스코프가 끝나면 사라지므로
			// 어차피 복사가 필요했다.
			std::vector<SProfileImageEntry> displayImages = images;
			for( SProfileImageEntry& entry : displayImages )
				entry.imageRef = ToDisplayImageUrl(entry.imageRef);

			jobQueue->DoAsync([onComplete, result, displayImages]()
				{
					if( onComplete )
						onComplete(result, displayImages);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_PROFILE_IMAGES_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListProfileImages,
		[&publicId, dispatchToJobQueue](ST_LIST_PROFILE_IMAGES_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, std::vector<SProfileImageEntry>());
	}
}

//***************************************************************************
// @brief 갤러리에 이미 있는 이미지 하나를 대표로 지정합니다.
//***************************************************************************
void CChatServerMain::RequestSelectProfileImage(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	int64 imageId,
	std::function<void(ELoginResult result, int64 imageId, const std::string& selectedImageRef)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [jobQueue, onComplete](ELoginResult result, int64 completedImageId, const std::string& selectedImageRef)
		{
			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedImageId, selectedImageRef]()
				{
					if( onComplete )
						onComplete(result, completedImageId, selectedImageRef);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SELECT_PROFILE_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SelectProfileImage,
		[&publicId, imageId, dispatchToJobQueue](ST_SELECT_PROFILE_IMAGE_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->imageId = imageId;
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, imageId, std::string());
	}
}

//***************************************************************************
// @brief 갤러리에서 이미지 하나를 삭제합니다. DB 레코드만 지우고, 실제
//        파일 삭제는 파일 서버 소관이라 이 서버는 관여하지 않습니다.
//***************************************************************************
void CChatServerMain::RequestDeleteProfileImage(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	int64 imageId,
	std::function<void(ELoginResult result, int64 imageId, bool wasActive, const std::string& deletedImageRef)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, int64 completedImageId,
		bool wasActive, const std::string& deletedImageRef)
		{
			// [설계] 실제 파일 삭제는 이 서버가 직접 하지 않는다 — 대신
			// deletedImageRef가 우리 파일 서버 소유면 Redis 큐에 "지울 것"만
			// 남겨두고, 파일 서버가 스스로 폴링하며 소비한다
			// (ScheduleFileDeletionIfOwned() 참고). DB 삭제가 확정된 뒤,
			// 아직 DB 워커 스레드인 이 시점에서 Redis에 적어둔다 — Redis
			// 쓰기 자체도 비동기라 이 스레드를 오래 붙잡지 않는다.
			if( result == ELoginResult::Ok )
				ScheduleFileDeletionIfOwned(deletedImageRef);

			if( jobQueue == nullptr )
				return;

			jobQueue->DoAsync([onComplete, result, completedImageId, wasActive, deletedImageRef]()
				{
					if( onComplete )
						onComplete(result, completedImageId, wasActive, deletedImageRef);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_PROFILE_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_DeleteProfileImage,
		[&publicId, imageId, dispatchToJobQueue](ST_DELETE_PROFILE_IMAGE_REQ* req)
		{
			::memcpy(req->publicId, publicId.data(), publicId.size());
			req->imageId = imageId;
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, imageId, false, std::string());
	}
}