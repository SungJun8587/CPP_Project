
//***************************************************************************
// ChatFriendHandler.cpp : 친구 기능 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"
#include "DBFriendRequest.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace
{
	// 요청자 정보 스냅샷 — 요청자가 응답 전에 끊겨도 상대 알림에 쓸 수 있도록 값으로 보관한다.
	struct SActor
	{
		std::weak_ptr<CChatSession>	session;
		PublicId					publicId{};
		std::string					nickname;
		std::string					profileImageUrl;
	};

	// 공통 사전 처리: 로그인된 세션만 허용하고 서버/요청자 스냅샷을 만든다.
	bool BeginOp(CChatSession& session, CChatServerMain*& server, SActor& actor)
	{
		server = GetLoggedInServer(session);
		if( server == nullptr )
			return false;

		actor.session = GetSessionRef(session);
		actor.publicId = session.GetPublicId();
		actor.nickname = session.GetNickname();
		actor.profileImageUrl = session.GetProfileImageUrl();
		return true;
	}

	void SendToOnline(CChatServerMain* server, const PublicId& target, const void* data, uint16 size)
	{
		if( auto s = server->FindOnlineSession(target) )
			s->Send(data, size);
	}

	void NotifyRemoved(CChatServerMain* server, const PublicId& to, const PublicId& removedFriend)
	{
		FriendRemovedNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::FriendRemovedNotify);
		CopyPublicId(notify.publicId, removedFriend);
		SendToOnline(server, to, &notify, sizeof(notify));
	}

	void NotifyAccepted(CChatServerMain* server, const PublicId& to, const PublicId& newFriend,
		const std::string& nickname, const std::string& profileImageUrl, bool online)
	{
		FriendAcceptedNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::FriendAcceptedNotify);
		CopyPublicId(notify.publicId, newFriend);
		FixedBuf::CopyStr(notify.nickname, nickname);
		FixedBuf::CopyStr(notify.profileImageUrl, profileImageUrl);
		notify.online = online ? 1 : 0;
		SendToOnline(server, to, &notify, sizeof(notify));
	}

	// ── 친구 요청 ────────────────────────────────────────────────────────
	void HandleFriendRequestReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const FriendRequestReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const std::string nickname = FixedBuf::ReadStr(packet->nickname);
		if( nickname.empty() )
			return;

		server->RequestFriendOp(actor.publicId, EFriendOp::SendRequest, nullptr, nickname,
			[server, actor](const SFriendOpResult& res)
			{
				const bool ok = (res.result == EFriendResult::Ok);

				if( ok && !res.silent )
				{
					if( res.autoAccepted )
					{
						// 서로 요청이 맞물려 즉시 친구 — 상대(먼저 요청한 쪽)에게 수락 통지
						NotifyAccepted(server, res.target.publicId, actor.publicId,
							actor.nickname, actor.profileImageUrl, true);
					}
					else
					{
						FriendRequestNotifyPacket notify{};
						InitPacket(notify, EChatPacketType::FriendRequestNotify);
						CopyPublicId(notify.publicId, actor.publicId);
						FixedBuf::CopyStr(notify.nickname, actor.nickname);
						FixedBuf::CopyStr(notify.profileImageUrl, actor.profileImageUrl);
						SendToOnline(server, res.target.publicId, &notify, sizeof(notify));
					}
				}

				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				FriendRequestResPacket out{};
				InitPacket(out, EChatPacketType::FriendRequestRes);
				out.success = ok ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				out.autoAccepted = res.autoAccepted ? 1 : 0;
				if( ok )
					CopyPublicId(out.publicId, res.target.publicId);
				SendPacket(me, out);

				if( ok && res.autoAccepted )
				{
					const bool online = (server->FindOnlineSession(res.target.publicId) != nullptr);
					NotifyAccepted(server, actor.publicId, res.target.publicId,
						res.target.nickname, res.target.profileImageRef, online);
				}
			});
	}

	// ── 수락 / 거절 ──────────────────────────────────────────────────────
	void HandleFriendRespondReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const FriendRespondReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const PublicId target = ToPublicId(packet->publicId);
		const bool accept = (packet->accept != 0);

		server->RequestFriendOp(actor.publicId, accept ? EFriendOp::Accept : EFriendOp::Reject, &target, std::string(),
			[server, actor, accept, target](const SFriendOpResult& res)
			{
				const bool ok = (res.result == EFriendResult::Ok);
				const bool targetOnline = ok && (server->FindOnlineSession(res.target.publicId) != nullptr);

				// 수락되면 요청을 보냈던 쪽에게 알린다 (거절은 알리지 않음)
				if( ok && accept )
				{
					NotifyAccepted(server, res.target.publicId, actor.publicId,
						actor.nickname, actor.profileImageUrl, true);
				}

				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				FriendRespondResPacket out{};
				InitPacket(out, EChatPacketType::FriendRespondRes);
				out.success = ok ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				out.accepted = accept ? 1 : 0;
				CopyPublicId(out.publicId, target);	// 요청에 실려온 대상을 그대로 되돌려준다(실패 시에도 클라이언트가 짝지을 수 있도록)
				out.online = targetOnline ? 1 : 0;
				SendPacket(me, out);
			});
	}

	// ── 친구 삭제 ────────────────────────────────────────────────────────
	void HandleFriendRemoveReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const FriendRemoveReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const PublicId target = ToPublicId(packet->publicId);

		server->RequestFriendOp(actor.publicId, EFriendOp::Remove, &target, std::string(),
			[server, actor, target](const SFriendOpResult& res)
			{
				const bool ok = (res.result == EFriendResult::Ok);
				if( ok )
					NotifyRemoved(server, target, actor.publicId);

				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				FriendRemoveResPacket out{};
				InitPacket(out, EChatPacketType::FriendRemoveRes);
				out.success = ok ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				CopyPublicId(out.publicId, target);
				SendPacket(me, out);
			});
	}

	// ── 목록 조회 (친구 / 받은 요청 / 차단) ─────────────────────────────
	void HandleFriendListReq(CChatSession& session, const PacketHeader*)
	{
		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		server->RequestFriendOp(actor.publicId, EFriendOp::ListFriends, nullptr, std::string(),
			[server, actor](const SFriendOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EFriendResult::Ok);
				int32 sent = 0;
				if( ok )
				{
					for( const SFriendEntry& e : res.entries )
					{
						FriendListItemResPacket item{};
						InitPacket(item, EChatPacketType::FriendListItemRes);
						CopyPublicId(item.publicId, e.publicId);
						FixedBuf::CopyStr(item.nickname, e.nickname);
						FixedBuf::CopyStr(item.profileImageUrl, e.profileImageRef);
						item.online = (server->FindOnlineSession(e.publicId) != nullptr) ? 1 : 0;
						SendPacket(me, item);
						++sent;
					}
				}

				FriendListEndResPacket end{};
				InitPacket(end, EChatPacketType::FriendListEndRes);
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.totalCount = sent;
				SendPacket(me, end);
			});
	}

	void HandleFriendPendingListReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const FriendPendingListReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int32 requestId = packet->requestId;

		server->RequestFriendOp(actor.publicId, EFriendOp::ListPending, nullptr, std::string(),
			[actor, requestId](const SFriendOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EFriendResult::Ok);
				if( ok )
				{
					for( const SFriendEntry& e : res.entries )
					{
						FriendPendingItemResPacket item{};
						InitPacket(item, EChatPacketType::FriendPendingItemRes);
						item.requestId = requestId;
						CopyPublicId(item.publicId, e.publicId);
						FixedBuf::CopyStr(item.nickname, e.nickname);
						FixedBuf::CopyStr(item.profileImageUrl, e.profileImageRef);
						SendPacket(me, item);
					}
				}

				FriendPendingEndResPacket end{};
				InitPacket(end, EChatPacketType::FriendPendingEndRes);
				end.requestId = requestId;
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.totalCount = res.totalCount;
				end.page = res.page;
				end.pageSize = res.pageSize;
				SendPacket(me, end);
			},
			packet->page, packet->pageSize);
	}

	void HandleBlockListReq(CChatSession& session, const PacketHeader*)
	{
		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		server->RequestFriendOp(actor.publicId, EFriendOp::ListBlocks, nullptr, std::string(),
			[actor](const SFriendOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EFriendResult::Ok);
				int32 sent = 0;
				if( ok )
				{
					for( const SFriendEntry& e : res.entries )
					{
						BlockListItemResPacket item{};
						InitPacket(item, EChatPacketType::BlockListItemRes);
						CopyPublicId(item.publicId, e.publicId);
						FixedBuf::CopyStr(item.nickname, e.nickname);
						SendPacket(me, item);
						++sent;
					}
				}

				BlockListEndResPacket end{};
				InitPacket(end, EChatPacketType::BlockListEndRes);
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.totalCount = sent;
				SendPacket(me, end);
			});
	}

	// ── 차단 / 차단 해제 ─────────────────────────────────────────────────
	void HandleBlockReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const BlockReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const std::string nickname = FixedBuf::ReadStr(packet->nickname);
		if( nickname.empty() )
			return;

		server->RequestFriendOp(actor.publicId, EFriendOp::Block, nullptr, nickname,
			[server, actor](const SFriendOpResult& res)
			{
				const bool ok = (res.result == EFriendResult::Ok);

				// 차단으로 끊긴 친구 관계는 상대에게 "삭제"로만 보인다(차단 사실은 노출하지 않음).
				if( ok && res.friendshipRemoved )
					NotifyRemoved(server, res.target.publicId, actor.publicId);

				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				// 채팅 수신 필터 갱신 — 이후 이 사용자의 메시지는 이 세션에 전달되지 않는다.
				if( ok || res.result == EFriendResult::AlreadyBlocked )
					me->AddBlockedUser(PublicIdToHex(res.target.publicId));

				BlockResPacket out{};
				InitPacket(out, EChatPacketType::BlockRes);
				out.success = ok ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				if( ok )
					CopyPublicId(out.publicId, res.target.publicId);
				SendPacket(me, out);
			});
	}

	void HandleUnblockReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const UnblockReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const PublicId target = ToPublicId(packet->publicId);

		server->RequestFriendOp(actor.publicId, EFriendOp::Unblock, &target, std::string(),
			[actor, target](const SFriendOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				if( res.result == EFriendResult::Ok || res.result == EFriendResult::NotBlocked )
					me->RemoveBlockedUser(PublicIdToHex(target));

				UnblockResPacket out{};
				InitPacket(out, EChatPacketType::UnblockRes);
				out.success = (res.result == EFriendResult::Ok) ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				CopyPublicId(out.publicId, target);
				SendPacket(me, out);
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(FriendRequestReq, FriendRequestReqPacket, HandleFriendRequestReq);
REGISTER_CHAT_PACKET_HANDLER(FriendRespondReq, FriendRespondReqPacket, HandleFriendRespondReq);
REGISTER_CHAT_PACKET_HANDLER(FriendRemoveReq, FriendRemoveReqPacket, HandleFriendRemoveReq);
REGISTER_CHAT_PACKET_HANDLER(FriendListReq, FriendListReqPacket, HandleFriendListReq);
REGISTER_CHAT_PACKET_HANDLER(FriendPendingListReq, FriendPendingListReqPacket, HandleFriendPendingListReq);
REGISTER_CHAT_PACKET_HANDLER(BlockListReq, BlockListReqPacket, HandleBlockListReq);
REGISTER_CHAT_PACKET_HANDLER(BlockReq, BlockReqPacket, HandleBlockReq);
REGISTER_CHAT_PACKET_HANDLER(UnblockReq, UnblockReqPacket, HandleUnblockReq);