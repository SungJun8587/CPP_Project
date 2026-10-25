
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
	bool firstSession = false;
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		auto& sessions = _onlineSessions[PublicIdToHex(publicId)];
		sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
			[](const std::weak_ptr<CChatSession>& w) { return w.expired(); }), sessions.end());
		firstSession = sessions.empty();
		sessions.push_back(session);
	}

	if( firstSession )
		NotifyFriendPresence(publicId, true);	// 계정의 첫 세션일 때만 알린다

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
// @return 같은 계정의 다른 세션이 아직 접속 중이면 true
//***************************************************************************
bool CChatServerMain::OnFriendLogout(const CChatSession* session)
{
	if( session == nullptr )
		return false;

	const std::array<BYTE, kPublicIdBytes> publicId = session->GetPublicId();
	const std::string key = PublicIdToHex(publicId);

	bool stillOnline = false;
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		auto it = _onlineSessions.find(key);
		if( it != _onlineSessions.end() )
		{
			auto& sessions = it->second;
			sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
				[session](const std::weak_ptr<CChatSession>& w)
				{
					auto s = w.lock();
					return s == nullptr || s.get() == session;
				}), sessions.end());

			stillOnline = !sessions.empty();	// 같은 계정의 다른 세션이 남아 있음(다중 접속)
			if( !stillOnline )
				_onlineSessions.erase(it);
		}
	}

	if( !stillOnline )
	{
		NotifyFriendPresence(publicId, false);	// 캐시된 친구 목록을 쓰므로 항목을 지우기 전에 호출한다
		InvalidateFriendCache(publicId);
	}

	return stillOnline;
}

//***************************************************************************
// @brief 접속 중인 세션을 publicId로 찾습니다. 없거나 끊겼으면 nullptr.
//***************************************************************************
std::shared_ptr<CChatSession> CChatServerMain::FindOnlineSession(const std::array<BYTE, kPublicIdBytes>& publicId) const
{
	std::lock_guard<std::mutex> lock(_onlineMutex);
	auto it = _onlineSessions.find(PublicIdToHex(publicId));
	if( it == _onlineSessions.end() )
		return nullptr;

	// 가장 최근에 로그인한 살아있는 세션을 돌려준다.
	for( auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit )
	{
		if( auto s = rit->lock() )
			return s;
	}
	return nullptr;
}

//***************************************************************************
// @brief 여러 publicId의 접속 세션을 락 한 번으로 찾습니다.
// @details 키(16진 문자열) 생성은 락 밖에서 끝내서, 락을 잡는 시간은 해시 조회에만 쓴다.
//***************************************************************************
std::vector<std::shared_ptr<CChatSession>> CChatServerMain::FindOnlineSessions(const std::vector<std::array<BYTE, kPublicIdBytes>>& ids) const
{
	std::vector<std::string> keys;
	keys.reserve(ids.size());
	for( const auto& id : ids )
		keys.push_back(PublicIdToHex(id));

	std::vector<std::shared_ptr<CChatSession>> sessions(ids.size());
	std::lock_guard<std::mutex> lock(_onlineMutex);
	for( size_t i = 0; i < keys.size(); ++i )
	{
		auto it = _onlineSessions.find(keys[i]);
		if( it == _onlineSessions.end() )
			continue;

		// 가장 최근에 로그인한 살아있는 세션.
		for( auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit )
		{
			if( auto s = rit->lock() )
			{
				sessions[i] = std::move(s);
				break;
			}
		}
	}
	return sessions;
}

//***************************************************************************
// @brief 친구 목록을 조회해 접속 중인 친구에게 FriendStatusNotify를 보냅니다.
// @details 세션 수명과 무관하게 publicId만으로 동작한다(로그아웃 시점에는 세션이 사라지는 중).
//***************************************************************************
void CChatServerMain::NotifyFriendPresence(const std::array<BYTE, kPublicIdBytes>& publicId, bool online)
{
	GetFriendIds(publicId,
		[this, publicId, online](const FriendIdList& friendIds)
		{
			FriendStatusNotifyPacket notify{};
			InitPacket(notify, EChatPacketType::FriendStatusNotify);
			CopyPublicId(notify.publicId, publicId);
			notify.online = online ? 1 : 0;

			for( const auto& friendSession : FindOnlineSessions(friendIds) )
			{
				if( friendSession != nullptr )
					SendPacket(friendSession, notify);
			}
		});
}

//***************************************************************************
// @brief 친구 id 목록을 캐시에서 돌려주고, 없으면 DB에서 읽어 캐시에 채운 뒤 돌려줍니다.
//***************************************************************************
void CChatServerMain::GetFriendIds(const std::array<BYTE, kPublicIdBytes>& publicId, std::function<void(const FriendIdList& ids)> onReady)
{
	const std::string key = PublicIdToHex(publicId);

	uint64_t gen = 0;
	std::shared_ptr<const FriendIdList> cached;
	{
		std::lock_guard<std::mutex> lock(_friendCacheMutex);
		auto it = _friendCache.find(key);
		if( it != _friendCache.end() )
			cached = it->second;
		else
			gen = _friendCacheGen.load();
	}

	// 콜백은 락 밖에서 호출한다(목록은 공유 포인터로 붙잡고 있으므로 그 사이 무효화돼도 안전하다).
	if( cached != nullptr )
	{
		onReady(*cached);
		return;
	}

	RequestFriendOp(publicId, EFriendOp::ListFriends, nullptr, std::string(),
		[this, publicId, key, gen, onReady](const SFriendOpResult& res)
		{
			if( res.result != EFriendResult::Ok )
				return;

			auto ids = std::make_shared<FriendIdList>();
			ids->reserve(res.entries.size());
			for( const SFriendEntry& entry : res.entries )
				ids->push_back(entry.publicId);

			{
				std::lock_guard<std::mutex> lock(_friendCacheMutex);
				// 접속 중인 계정만, 그리고 조회를 시작한 뒤 무효화가 없었을 때만 캐시에 넣는다.
				if( gen == _friendCacheGen.load() && IsUserOnline(publicId) )
					_friendCache[key] = ids;
			}

			onReady(*ids);
		});
}

//***************************************************************************
// @brief 계정의 친구 id 캐시 항목을 지우고 세대를 올려 진행 중인 조회 결과가 캐시에 들어가지 못하게 합니다.
//***************************************************************************
void CChatServerMain::InvalidateFriendCache(const std::array<BYTE, kPublicIdBytes>& publicId)
{
	const std::string key = PublicIdToHex(publicId);

	std::lock_guard<std::mutex> lock(_friendCacheMutex);
	_friendCache.erase(key);
	_friendCacheGen.fetch_add(1);
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

	const bool hasTarget = (targetId != nullptr);
	const std::array<BYTE, kPublicIdBytes> requestedTarget = hasTarget ? *targetId : std::array<BYTE, kPublicIdBytes>{};
	const std::array<BYTE, kPublicIdBytes> actorId = actor;

	auto dispatchToJobQueue = [this, jobQueue, onComplete, op, hasTarget, requestedTarget, actorId](const SFriendOpResult& result)
		{
			if( jobQueue == nullptr )
				return;

			// 친구 관계가 바뀐 연산이 성공했으면 양쪽 계정의 친구 id 캐시를 무효화한다
			// (콜백보다 먼저 — 콜백 안의 후속 조회가 오래된 캐시를 보지 않도록).
			if( result.result == EFriendResult::Ok &&
				(op == EFriendOp::Accept || op == EFriendOp::Remove || op == EFriendOp::Block ||
					(op == EFriendOp::SendRequest && result.autoAccepted)) )
			{
				InvalidateFriendCache(actorId);
				if( hasTarget )
					InvalidateFriendCache(requestedTarget);
				InvalidateFriendCache(result.target.publicId);
			}

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
				CopyStrUtf8(req->targetNickname, targetNickname);
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
	CopyStrUtf8(notify.nickname, session->GetNickname());
	CopyStrUtf8(notify.profileImageUrl, session->GetProfileImageUrl());

	GetFriendIds(publicId,
		[this, notify](const FriendIdList& friendIds)
		{
			for( const auto& friendSession : FindOnlineSessions(friendIds) )
			{
				if( friendSession != nullptr )
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