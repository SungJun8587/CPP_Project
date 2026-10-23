
//***************************************************************************
// ChatServerMainConv.cpp : CChatServerMain의 대화방(1:1/그룹) 기능 구현
//   - 대화방 DB 연산 요청(RequestConvOp)
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"
#include "DBConvRequest.h"

//***************************************************************************
// @brief 대화방 DB 연산을 비동기로 요청하고, 결과를 JobQueue로 옮겨 onComplete를 호출합니다.
// @param actor 요청자 publicId
// @param fill 요청 구조체에 연산 종류와 인자를 채우는 함수
// @param onComplete JobQueue 스레드에서 호출할 완료 콜백
// @details 결과에 담긴 프로필 이미지 참조(DB에는 상대 경로로 저장돼 있을 수 있음)는 모두
//          표시용 URL로 복원한 뒤 넘긴다(ToDisplayImageUrl()).
//***************************************************************************
void CChatServerMain::RequestConvOp(
	const std::array<BYTE, kPublicIdBytes>& actor,
	std::function<void(ST_CONV_REQ* req)> fill,
	std::function<void(const SConvOpResult& result)> onComplete)
{
	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](const SConvOpResult& result)
		{
			if( jobQueue == nullptr )
				return;

			SConvOpResult display = result;
			display.summary.peer.profileImageRef = ToDisplayImageUrl(display.summary.peer.profileImageRef);
			for( SConvSummary& summary : display.convs )
				summary.peer.profileImageRef = ToDisplayImageUrl(summary.peer.profileImageRef);
			for( SConvMember& member : display.members )
				member.profileImageRef = ToDisplayImageUrl(member.profileImageRef);
			const uint8 imageKind = static_cast<uint8>(EConvMessageKind::Image);
			for( SConvMessage& message : display.messages )
			{
				message.senderImageRef = ToDisplayImageUrl(message.senderImageRef);
				if( message.kind == imageKind )
					message.body = ToDisplayImageUrl(message.body);		// 사진 메시지 본문은 저장용 참조 -> 표시용 URL
			}
			display.newMsg.senderImageRef = ToDisplayImageUrl(display.newMsg.senderImageRef);
			if( display.newMsg.kind == imageKind )
				display.newMsg.body = ToDisplayImageUrl(display.newMsg.body);

			PostToJobQueue(jobQueue, onComplete, display);
		};

	const bool pushed = PushDb<ST_CONV_REQ>(kDbCallIdent_Conv,
		[&actor, &fill, dispatchToJobQueue](ST_CONV_REQ* req)
		{
			CopyPublicId(req->publicId, actor);
			if( fill )
				fill(req);
			req->onComplete = dispatchToJobQueue;
		});

	if( !pushed && onComplete )
	{
		SConvOpResult failed;
		failed.result = EConvResult::DbError;
		onComplete(failed);
	}
}