
//***************************************************************************
// ChatServerMainFriend.cpp : CChatServerMain의 친구 기능 구현
//   - 접속 중 세션 레지스트리 (publicId -> 세션)
//   - 친구 DB 연산 요청(RequestFriendOp)
//   - 접속/종료 시 접속 중인 친구에게 상태 알림
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"
#include "DBFriendRequest.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

//***************************************************************************
// @brief 로그인 완료된 세션을 친구 알림/조회용 레지스트리에 등록하고, 접속 중인 친구에게 접속을 알립니다.
// @details 같은 계정이 중복 접속하면 가장 마지막 세션이 레지스트리를 차지한다.
//***************************************************************************
void CChatServerMain::OnFriendLogin(const std::shared_ptr<CChatSession>& session)
{
	if( session == nullptr )
		return;

	const std::array<BYTE, kPublicIdBytes> publicId = session->GetPublicId();
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		_onlineSessions[PublicIdToHex(publicId)] = session;
	}

	NotifyFriendPresence(publicId, true);

	// 이 계정이 차단한 사용자 목록을 세션에 올려둔다 — 채팅 수신 필터(BroadcastChatToRoom)가 쓴다.
	// 로드가 끝나기 전의 극히 짧은 구간에는 필터가 비어 있어 차단한 사용자의 메시지가 한두 개 전달될 수 있다.
	std::weak_ptr<CChatSession> sessionWeak = session;
	RequestFriendOp(publicId, EFriendOp::ListBlocks, nullptr, std::string(),
		[sessionWeak](const SFriendOpResult& res)
		{
			auto s = sessionWeak.lock();
			if( s == nullptr || res.result != EFriendResult::Ok )
				return;

			std::unordered_set<std::string> blocked;
			for( const SFriendEntry& entry : res.entries )
				blocked.insert(PublicIdToHex(entry.publicId));
			s->SetBlockedUsers(std::move(blocked));
		});
}

//***************************************************************************
// @brief 세션을 레지스트리에서 제거하고, 같은 계정의 다른 세션이 남아있지 않으면 친구들에게 종료를 알립니다.
// @param session 종료되는 세션 (비교 용도로만 쓰고 소유하지 않음 — OnDisconnected 안에서 호출됨)
//***************************************************************************
void CChatServerMain::OnFriendLogout(const CChatSession* session)
{
	if( session == nullptr )
		return;

	const std::array<BYTE, kPublicIdBytes> publicId = session->GetPublicId();
	const std::string key = PublicIdToHex(publicId);

	bool stillOnline = false;
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		auto it = _onlineSessions.find(key);
		if( it != _onlineSessions.end() )
		{
			auto current = it->second.lock();
			if( current == nullptr || current.get() == session )
				_onlineSessions.erase(it);
			else
				stillOnline = true;	// 중복 로그인으로 다른 세션이 이 계정을 차지하고 있음
		}
	}

	if( !stillOnline )
		NotifyFriendPresence(publicId, false);
}

//***************************************************************************
// @brief 접속 중인 세션을 publicId로 찾습니다. 없거나 끊겼으면 nullptr.
//***************************************************************************
std::shared_ptr<CChatSession> CChatServerMain::FindOnlineSession(const std::array<BYTE, kPublicIdBytes>& publicId) const
{
	std::lock_guard<std::mutex> lock(_onlineMutex);
	auto it = _onlineSessions.find(PublicIdToHex(publicId));
	return (it != _onlineSessions.end()) ? it->second.lock() : nullptr;
}

//***************************************************************************
// @brief 친구 목록을 조회해 접속 중인 친구에게 FriendStatusNotify를 보냅니다.
// @details 세션 수명과 무관하게 publicId만으로 동작한다(로그아웃 시점에는 세션이 사라지는 중).
//***************************************************************************
void CChatServerMain::NotifyFriendPresence(const std::array<BYTE, kPublicIdBytes>& publicId, bool online)
{
	RequestFriendOp(publicId, EFriendOp::ListFriends, nullptr, std::string(),
		[this, publicId, online](const SFriendOpResult& res)
		{
			if( res.result != EFriendResult::Ok )
				return;

			FriendStatusNotifyPacket notify{};
			InitPacket(notify, EChatPacketType::FriendStatusNotify);
			CopyPublicId(notify.publicId, publicId);
			notify.online = online ? 1 : 0;

			for( const SFriendEntry& friendEntry : res.entries )
			{
				if( auto friendSession = FindOnlineSession(friendEntry.publicId) )
					SendPacket(friendSession, notify);
			}
		});
}

//***************************************************************************
// @brief 친구 DB 연산을 비동기로 요청하고, 결과를 JobQueue로 옮겨 onComplete를 호출합니다.
// @param actor 요청자 publicId
// @param targetId 대상 publicId (닉네임으로 지정하는 연산이면 nullptr)
// @param targetNickname 대상 닉네임 (targetId가 있으면 무시)
// @param page/pageSize EFriendOp::ListPending 전용 페이지 지정 (서버가 보정, 보정값은 결과에 담김) (다른 연산은 무시)
// @details 다른 Request*()와 달리 세션 포인터를 받지 않는다 — 요청자가 응답 전에
//          끊겨도 DB 변경에 따른 상대방 알림은 여전히 전달돼야 하기 때문이다.
//          onComplete 안에서 요청자 세션이 필요하면 호출부가 weak_ptr로 재확인한다.
//***************************************************************************
void CChatServerMain::RequestFriendOp(
	const std::array<BYTE, kPublicIdBytes>& actor,
	EFriendOp op,
	const std::array<BYTE, kPublicIdBytes>* targetId,
	const std::string& targetNickname,
	std::function<void(const SFriendOpResult& result)> onComplete,
	int32 page,
	int32 pageSize)
{
	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](const SFriendOpResult& result)
		{
			if( jobQueue == nullptr )
				return;

			// image_ref가 상대 경로로 저장돼 있을 수 있으므로 표시용 URL로 복원한다
			// (RequestListProfileImages()와 동일한 이유).
			SFriendOpResult display = result;
			display.target.profileImageRef = ToDisplayImageUrl(display.target.profileImageRef);
			for( SFriendEntry& entry : display.entries )
				entry.profileImageRef = ToDisplayImageUrl(entry.profileImageRef);

			PostToJobQueue(jobQueue, onComplete, display);
		};

	const bool pushed = PushDb<ST_FRIEND_REQ>(kDbCallIdent_Friend,
		[&actor, op, targetId, &targetNickname, page, pageSize, dispatchToJobQueue](ST_FRIEND_REQ* req)
		{
			req->op = op;
			req->page = page;
			req->pageSize = pageSize;
			CopyPublicId(req->publicId, actor);
			if( targetId != nullptr )
			{
				req->hasTargetId = true;
				CopyPublicId(req->targetPublicId, *targetId);
			}
			else
			{
				FixedBuf::CopyStr(req->targetNickname, targetNickname);
			}
			req->onComplete = dispatchToJobQueue;
		});

	if( !pushed && onComplete )
	{
		SFriendOpResult failed;
		failed.result = EFriendResult::DbError;
		onComplete(failed);
	}
}

//***************************************************************************
// @brief 세션의 현재 닉네임/대표 프로필 이미지를 접속 중인 친구들에게 FriendProfileChangedNotify로 알립니다.
// @details 닉네임 변경, 프로필 이미지 설정/대표 변경/삭제 핸들러가 "세션의 값을 갱신한 직후"에 호출한다.
//          알림에는 세션이 들고 있는 최신 닉네임/프로필 URL 전체가 실린다. 친구 목록은 DB에서 조회한다.
//***************************************************************************
void CChatServerMain::NotifyFriendProfileChanged(const std::shared_ptr<CChatSession>& session)
{
	if( session == nullptr || !session->IsLoggedIn() )
		return;

	const std::array<BYTE, kPublicIdBytes> publicId = session->GetPublicId();

	FriendProfileChangedNotifyPacket notify{};
	InitPacket(notify, EChatPacketType::FriendProfileChangedNotify);
	CopyPublicId(notify.publicId, publicId);
	FixedBuf::CopyStr(notify.nickname, session->GetNickname());
	FixedBuf::CopyStr(notify.profileImageUrl, session->GetProfileImageUrl());

	RequestFriendOp(publicId, EFriendOp::ListFriends, nullptr, std::string(),
		[this, notify](const SFriendOpResult& res)
		{
			if( res.result != EFriendResult::Ok )
				return;

			for( const SFriendEntry& friendEntry : res.entries )
			{
				if( auto friendSession = FindOnlineSession(friendEntry.publicId) )
					SendPacket(friendSession, notify);
			}
		});
}

//***************************************************************************
// @brief 채팅 메시지를 방(로비 포함)에 브로드캐스트하되, 발신자를 차단한 수신자에게는 보내지 않습니다.
// @details BroadcastToRoom()과 동일한 구조(락 안에서 대상만 수집한 뒤 락 밖에서 전송). 차단은
//          수신자 기준 단방향이다 — 차단한 사람만 상대의 메시지를 못 보고, 차단당한 사람은
//          차단 여부를 알 수 없다. 발신자 본인에게는 그대로 에코된다(자기 자신은 차단할 수 없음).
//***************************************************************************
void CChatServerMain::BroadcastChatToRoom(int32 roomId, const std::array<BYTE, kPublicIdBytes>& senderPublicId,
	const void* data, uint16 size)
{
	if( data == nullptr || size == 0 )
		return;

	std::vector<std::shared_ptr<CChatSession>> targets;
	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		auto it = _roomMembers.find(roomId);
		if( it == _roomMembers.end() )
			return;

		targets.reserve(it->second.size());
		for( auto& w : it->second )
		{
			if( auto s = w.lock() )
				targets.push_back(s);
		}
	}

	const std::string senderKey = PublicIdToHex(senderPublicId);	// 수신자마다 다시 변환하지 않도록 한 번만
	for( auto& s : targets )
	{
		if( s->IsConnected() && !s->IsBlockingHex(senderKey) )
			s->Send(data, size);
	}
}