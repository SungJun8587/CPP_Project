
//***************************************************************************
// ChatConvHandler.cpp : 대화방(1:1/그룹) 패킷 핸들러 (자체 등록)
//
//   서버는 각 연산을 DB에서 처리한 뒤(RequestConvOp), 결과를 요청자에게 응답하고
//   접속 중인 다른 멤버에게 푸시한다. 모든 메시지는 DB에 저장되므로 오프라인 멤버는
//   다음 접속 때 목록(안 읽은 수)과 기록 조회로 받는다.
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"
#include "DBConvRequest.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace
{
	// 요청자 정보 스냅샷 — 요청자가 응답 전에 끊겨도 다른 멤버 푸시에 쓸 수 있도록 값으로 보관한다.
	struct SActor
	{
		std::weak_ptr<CChatSession>	session;
		PublicId					publicId{};
	};

	bool BeginOp(CChatSession& session, CChatServerMain*& server, SActor& actor)
	{
		server = GetLoggedInServer(session);
		if( server == nullptr )
			return false;

		actor.session = GetSessionRef(session);
		actor.publicId = session.GetPublicId();
		return true;
	}

	//***************************************************************************
	// @brief UTF-8 문자 경계를 지켜 고정폭 char 배열에 복사한다(잘려도 깨진 문자가 남지 않음).
	//***************************************************************************
	template<size_t N>
	void CopyUtf8Safe(char(&dst)[N], const std::string& src)
	{
		size_t len = (std::min)(src.size(), N - 1);
		if( len < src.size() )
		{
			// 잘리는 지점이 문자 중간(연속 바이트 0b10xxxxxx)이면 그 문자의 시작까지 물린다.
			while( len > 0 && (static_cast<unsigned char>(src[len]) & 0xC0) == 0x80 )
				--len;
		}
		::memset(dst, 0, N);
		::memcpy(dst, src.data(), len);
	}

	void FillSummary(ConvSummaryFields& out, const SConvSummary& s)
	{
		::memset(&out, 0, sizeof(out));
		out.convId = s.convId;
		out.type = s.type;
		FixedBuf::CopyStr(out.name, s.name);
		out.memberCount = s.memberCount;
		out.unreadCount = s.unreadCount;
		out.lastMsgId = s.lastMsgId;
		out.lastMsgTime = s.lastMsgTime;
		out.lastKind = s.lastKind;
		CopyUtf8Safe(out.lastMessage, s.lastMessage);
		out.myLastReadMsgId = s.myLastReadMsgId;
		CopyPublicId(out.peerPublicId, s.peer.publicId);
		FixedBuf::CopyStr(out.peerNickname, s.peer.nickname);
		FixedBuf::CopyStr(out.peerImageUrl, s.peer.profileImageRef);
	}

	void FillMessage(ConvMessageNotifyPacket& out, const SConvMessage& m)
	{
		out.convId = m.convId;
		out.msgId = m.msgId;
		out.kind = m.kind;
		if( m.hasSender )
			CopyPublicId(out.senderPublicId, m.sender);
		FixedBuf::CopyStr(out.senderNickname, m.senderNickname);
		FixedBuf::CopyStr(out.senderImageUrl, m.senderImageRef);
		out.timestamp = m.timestamp;
		CopyUtf8Safe(out.message, m.body);

		out.replyToMsgId = m.replyToMsgId;
		if( m.replyToMsgId > 0 )
		{
			out.replyKind = m.replyKind;
			FixedBuf::CopyStr(out.replySenderNickname, m.replySenderNickname);
			// 미리보기는 텍스트 대상의 첫 줄만(사진은 클라이언트가 "(사진)"으로 표시).
			if( m.replyKind == static_cast<uint8>(EConvMessageKind::Text) )
			{
				const size_t cut = m.replyBody.find_first_of("\r\n");
				CopyUtf8Safe(out.replyPreview, cut == std::string::npos ? m.replyBody : m.replyBody.substr(0, cut));
			}
		}
	}

	void SendToOnline(CChatServerMain* server, const PublicId& target, const void* data, uint16 size)
	{
		if( auto s = server->FindOnlineSession(target) )
			s->Send(data, size);
	}

	//***************************************************************************
	// @brief 새 메시지를 접속 중인 멤버에게 푸시한다(발신자 본인 포함).
	// @details 발신자를 차단한 멤버에게는 보내지 않는다(DB의 기록/안 읽은 수 계산과 같은 규칙).
	//          silent(1:1에서 상대가 나를 차단)이면 발신자에게만 돌려준다.
	//***************************************************************************
	void PushMessage(CChatServerMain* server, const SConvOpResult& res, const PublicId& actor)
	{
		if( !res.hasNewMsg )
			return;

		ConvMessageNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::ConvMessageNotify);
		FillMessage(notify, res.newMsg);

		const std::string senderKey = res.newMsg.hasSender ? PublicIdToHex(res.newMsg.sender) : std::string();
		for( const PublicId& recipient : res.recipients )
		{
			if( res.silent && recipient != actor )
				continue;

			auto session = server->FindOnlineSession(recipient);
			if( session == nullptr || !session->IsConnected() )
				continue;
			if( res.newMsg.hasSender && recipient != res.newMsg.sender && session->IsBlockingHex(senderKey) )
				continue;
			session->Send(&notify, sizeof(notify));
		}
	}

	//***************************************************************************
	// @brief 새로 들어온 멤버에게 대화방 요약을 알린다(안 읽은 수 0, 읽음 위치 0으로 초기화).
	//***************************************************************************
	void PushAddedMembers(CChatServerMain* server, const SConvOpResult& res)
	{
		if( res.addedMembers.empty() )
			return;

		SConvSummary forInvitee = res.summary;
		forInvitee.unreadCount = 0;
		forInvitee.myLastReadMsgId = 0;

		ConvUpdatedNotifyPacket notify{};
		InitPacket(notify, EChatPacketType::ConvUpdatedNotify);
		FillSummary(notify.summary, forInvitee);

		for( const PublicId& added : res.addedMembers )
			SendToOnline(server, added, &notify, sizeof(notify));
	}

	// ── 1:1 열기 ─────────────────────────────────────────────────────────
	void HandleConvOpenDirectReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvOpenDirectReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const PublicId target = ToPublicId(packet->publicId);

		server->RequestConvOp(actor.publicId,
			[target](ST_CONV_REQ* req)
			{
				req->op = EConvOp::OpenDirect;
				req->targetCount = 1;
				CopyPublicId(req->targets[0], target);
			},
			[actor](const SConvOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				ConvOpenResPacket out{};
				InitPacket(out, EChatPacketType::ConvOpenRes);
				out.success = (res.result == EConvResult::Ok) ? 1 : 0;
				out.reason = static_cast<uint8>(res.result);
				out.isNew = res.isNew ? 1 : 0;
				if( res.result == EConvResult::Ok )
					FillSummary(out.summary, res.summary);
				SendPacket(me, out);
			});
	}

	// ── 그룹 생성 ────────────────────────────────────────────────────────
	void HandleConvCreateGroupReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvCreateGroupReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int32 count = packet->count;
		if( count < 1 || count > kMaxConvInvite )
		{
			ConvOpenResPacket out{};
			InitPacket(out, EChatPacketType::ConvOpenRes);
			out.reason = static_cast<uint8>(EConvResult::InvalidTarget);
			SendPacket(session, out);
			return;
		}

		const std::string name = FixedBuf::ReadStr(packet->name);

		// 요청 패킷은 핸들러가 끝나면 사라지므로 값으로 복사해 둔다.
		auto members = std::make_shared<std::array<std::array<BYTE, kPublicIdBytes>, kMaxConvInvite>>();
		for( int32 i = 0; i < count; ++i )
			::memcpy((*members)[i].data(), packet->members[i], kPublicIdBytes);

		server->RequestConvOp(actor.publicId,
			[name, members, count](ST_CONV_REQ* req)
			{
				req->op = EConvOp::CreateGroup;
				FixedBuf::CopyStr(req->name, name);
				req->targetCount = count;
				for( int32 i = 0; i < count; ++i )
					::memcpy(req->targets[i], (*members)[i].data(), kPublicIdBytes);
			},
			[server, actor](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvOpenResPacket out{};
					InitPacket(out, EChatPacketType::ConvOpenRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.isNew = res.isNew ? 1 : 0;
					if( ok )
						FillSummary(out.summary, res.summary);
					SendPacket(me, out);
				}

				if( !ok )
					return;
				PushAddedMembers(server, res);		// 초대받은 멤버가 방을 먼저 알게 한 뒤
				PushMessage(server, res, actor.publicId);	// 안내 메시지를 보낸다
			});
	}

	// ── 초대 ─────────────────────────────────────────────────────────────
	void HandleConvInviteReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvInviteReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const int32 count = packet->count;
		if( count < 1 || count > kMaxConvInvite )
		{
			ConvInviteResPacket out{};
			InitPacket(out, EChatPacketType::ConvInviteRes);
			out.reason = static_cast<uint8>(EConvResult::InvalidTarget);
			out.convId = convId;
			SendPacket(session, out);
			return;
		}

		auto members = std::make_shared<std::array<std::array<BYTE, kPublicIdBytes>, kMaxConvInvite>>();
		for( int32 i = 0; i < count; ++i )
			::memcpy((*members)[i].data(), packet->members[i], kPublicIdBytes);

		server->RequestConvOp(actor.publicId,
			[convId, members, count](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Invite;
				req->convId = convId;
				req->targetCount = count;
				for( int32 i = 0; i < count; ++i )
					::memcpy(req->targets[i], (*members)[i].data(), kPublicIdBytes);
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvInviteResPacket out{};
					InitPacket(out, EChatPacketType::ConvInviteRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.convId = convId;
					SendPacket(me, out);
				}

				if( !ok )
					return;
				PushAddedMembers(server, res);
				PushMessage(server, res, actor.publicId);
			});
	}

	// ── 나가기 ───────────────────────────────────────────────────────────
	void HandleConvLeaveReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvLeaveReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;

		server->RequestConvOp(actor.publicId,
			[convId](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Leave;
				req->convId = convId;
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvLeaveResPacket out{};
					InitPacket(out, EChatPacketType::ConvLeaveRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.convId = convId;
					SendPacket(me, out);
				}

				if( ok )
					PushMessage(server, res, actor.publicId);	// 남은 멤버에게 "나갔습니다" 안내
			});
	}

	// ── 목록 ─────────────────────────────────────────────────────────────
	void HandleConvListReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		server->RequestConvOp(actor.publicId,
			[](ST_CONV_REQ* req) { req->op = EConvOp::List; },
			[actor](const SConvOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EConvResult::Ok);
				int32 sent = 0;
				if( ok )
				{
					for( const SConvSummary& summary : res.convs )
					{
						ConvListItemResPacket item{};
						InitPacket(item, EChatPacketType::ConvListItemRes);
						FillSummary(item.summary, summary);
						SendPacket(me, item);
						++sent;
					}
				}

				ConvListEndResPacket end{};
				InitPacket(end, EChatPacketType::ConvListEndRes);
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.totalCount = sent;
				SendPacket(me, end);
			});
	}

	// ── 메시지 전송 ──────────────────────────────────────────────────────
	void HandleConvSendReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvSendReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const int64 replyToMsgId = (std::max<int64>)(0, packet->replyToMsgId);
		const uint8 kind = packet->kind;
		std::string message = FixedBuf::ReadStr(packet->message);

		const bool isImage = (kind == static_cast<uint8>(EConvMessageKind::Image));
		const bool isText = (kind == static_cast<uint8>(EConvMessageKind::Text));
		const bool blank = std::all_of(message.begin(), message.end(),
			[](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; });
		// 사진은 우리 파일 서버에 올라간 이미지 URL만 허용한다(외부 URL로 다른 사용자의 IP를 수집하는 등의 악용 방지).
		if( (!isText && !isImage) || blank || (isImage && !server->IsOwnedImageUrl(message)) )
		{
			ConvSendResPacket out{};
			InitPacket(out, EChatPacketType::ConvSendRes);
			out.reason = static_cast<uint8>(EConvResult::InvalidMessage);
			out.convId = convId;
			SendPacket(session, out);
			return;
		}

		if( isImage )
			message = server->ToStorableImageRef(message);	// DB에는 호스트를 뺀 참조로 저장

		server->RequestConvOp(actor.publicId,
			[convId, message, kind, replyToMsgId](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Send;
				req->convId = convId;
				req->msgKind = kind;
				req->replyToMsgId = replyToMsgId;
				FixedBuf::CopyStr(req->message, message);
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvSendResPacket out{};
					InitPacket(out, EChatPacketType::ConvSendRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.convId = convId;
					out.msgId = ok ? res.newMsg.msgId : 0;
					SendPacket(me, out);
				}

				if( ok )
					PushMessage(server, res, actor.publicId);
			});
	}

	// ── 그룹 이름 변경 ───────────────────────────────────────────────────
	void HandleConvRenameReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvRenameReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const std::string name = FixedBuf::ReadStr(packet->name);

		server->RequestConvOp(actor.publicId,
			[convId, name](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Rename;
				req->convId = convId;
				FixedBuf::CopyStr(req->name, name);
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvRenameResPacket out{};
					InitPacket(out, EChatPacketType::ConvRenameRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.convId = convId;
					SendPacket(me, out);
				}

				if( !ok )
					return;

				ConvRenamedNotifyPacket notify{};
				InitPacket(notify, EChatPacketType::ConvRenamedNotify);
				notify.convId = convId;
				FixedBuf::CopyStr(notify.name, res.renamedTo);
				for( const PublicId& recipient : res.recipients )
					SendToOnline(server, recipient, &notify, sizeof(notify));

				PushMessage(server, res, actor.publicId);	// "OO님이 이름을 변경했습니다" 안내
			});
	}

	// ── 메시지 삭제 ──────────────────────────────────────────────────────
	void HandleConvDeleteReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvDeleteReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const int64 msgId = packet->msgId;

		server->RequestConvOp(actor.publicId,
			[convId, msgId](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Delete;
				req->convId = convId;
				req->msgId = msgId;
			},
			[server, actor, convId, msgId](const SConvOpResult& res)
			{
				const bool ok = (res.result == EConvResult::Ok);

				if( auto me = actor.session.lock() )
				{
					ConvDeleteResPacket out{};
					InitPacket(out, EChatPacketType::ConvDeleteRes);
					out.success = ok ? 1 : 0;
					out.reason = static_cast<uint8>(res.result);
					out.convId = convId;
					out.msgId = msgId;
					SendPacket(me, out);
				}

				if( !ok )
					return;

				ConvDeletedNotifyPacket notify{};
				InitPacket(notify, EChatPacketType::ConvDeletedNotify);
				notify.convId = convId;
				notify.msgId = res.deletedMsgId;
				for( const PublicId& recipient : res.recipients )
					SendToOnline(server, recipient, &notify, sizeof(notify));
			});
	}

	// ── 기록 조회 ────────────────────────────────────────────────────────
	void HandleConvHistoryReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvHistoryReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const int64 beforeMsgId = packet->beforeMsgId;
		const int32 count = packet->count;
		const int32 requestId = packet->requestId;

		server->RequestConvOp(actor.publicId,
			[convId, beforeMsgId, count](ST_CONV_REQ* req)
			{
				req->op = EConvOp::History;
				req->convId = convId;
				req->beforeMsgId = beforeMsgId;
				req->count = count;
			},
			[actor, convId, requestId](const SConvOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EConvResult::Ok);
				int32 sent = 0;
				if( ok )
				{
					for( const SConvMessage& message : res.messages )
					{
						ConvHistoryItemResPacket item{};
						InitPacket(item, EChatPacketType::ConvHistoryItemRes);
						FillMessage(item, message);
						SendPacket(me, item);
						++sent;
					}
				}

				ConvHistoryEndResPacket end{};
				InitPacket(end, EChatPacketType::ConvHistoryEndRes);
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.hasMore = (ok && res.hasMore) ? 1 : 0;
				end.convId = convId;
				end.count = sent;
				end.requestId = requestId;
				SendPacket(me, end);
			});
	}

	// ── 읽음 ─────────────────────────────────────────────────────────────
	void HandleConvReadReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvReadReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;
		const int64 upToMsgId = packet->upToMsgId;

		server->RequestConvOp(actor.publicId,
			[convId, upToMsgId](ST_CONV_REQ* req)
			{
				req->op = EConvOp::MarkRead;
				req->convId = convId;
				req->upToMsgId = upToMsgId;
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				// 읽음 위치가 실제로 앞으로 갔을 때만 recipients가 채워진다.
				if( res.result != EConvResult::Ok || res.recipients.empty() )
					return;

				ConvReadNotifyPacket notify{};
				InitPacket(notify, EChatPacketType::ConvReadNotify);
				notify.convId = convId;
				CopyPublicId(notify.readerPublicId, actor.publicId);
				notify.upToMsgId = res.readUpTo;

				for( const PublicId& recipient : res.recipients )
				{
					if( recipient != actor.publicId )
						SendToOnline(server, recipient, &notify, sizeof(notify));
				}
			});
	}

	// ── 멤버 목록 ────────────────────────────────────────────────────────
	void HandleConvMembersReq(CChatSession& session, const PacketHeader* header)
	{
		const auto* packet = reinterpret_cast<const ConvMembersReqPacket*>(header);

		CChatServerMain* server = nullptr;
		SActor actor;
		if( !BeginOp(session, server, actor) )
			return;

		const int64 convId = packet->convId;

		server->RequestConvOp(actor.publicId,
			[convId](ST_CONV_REQ* req)
			{
				req->op = EConvOp::Members;
				req->convId = convId;
			},
			[server, actor, convId](const SConvOpResult& res)
			{
				auto me = actor.session.lock();
				if( me == nullptr )
					return;

				const bool ok = (res.result == EConvResult::Ok);
				int32 sent = 0;
				if( ok )
				{
					for( const SConvMember& member : res.members )
					{
						ConvMemberItemResPacket item{};
						InitPacket(item, EChatPacketType::ConvMemberItemRes);
						item.convId = convId;
						CopyPublicId(item.publicId, member.publicId);
						FixedBuf::CopyStr(item.nickname, member.nickname);
						FixedBuf::CopyStr(item.profileImageUrl, member.profileImageRef);
						item.lastReadMsgId = member.lastReadMsgId;
						item.online = (server->FindOnlineSession(member.publicId) != nullptr) ? 1 : 0;
						SendPacket(me, item);
						++sent;
					}
				}

				ConvMembersEndResPacket end{};
				InitPacket(end, EChatPacketType::ConvMembersEndRes);
				end.success = ok ? 1 : 0;
				end.reason = static_cast<uint8>(res.result);
				end.convId = convId;
				end.totalCount = sent;
				SendPacket(me, end);
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ConvOpenDirectReq, ConvOpenDirectReqPacket, HandleConvOpenDirectReq);
REGISTER_CHAT_PACKET_HANDLER(ConvCreateGroupReq, ConvCreateGroupReqPacket, HandleConvCreateGroupReq);
REGISTER_CHAT_PACKET_HANDLER(ConvInviteReq, ConvInviteReqPacket, HandleConvInviteReq);
REGISTER_CHAT_PACKET_HANDLER(ConvLeaveReq, ConvLeaveReqPacket, HandleConvLeaveReq);
REGISTER_CHAT_PACKET_HANDLER(ConvListReq, ConvListReqPacket, HandleConvListReq);
REGISTER_CHAT_PACKET_HANDLER(ConvSendReq, ConvSendReqPacket, HandleConvSendReq);
REGISTER_CHAT_PACKET_HANDLER(ConvHistoryReq, ConvHistoryReqPacket, HandleConvHistoryReq);
REGISTER_CHAT_PACKET_HANDLER(ConvReadReq, ConvReadReqPacket, HandleConvReadReq);
REGISTER_CHAT_PACKET_HANDLER(ConvMembersReq, ConvMembersReqPacket, HandleConvMembersReq);
REGISTER_CHAT_PACKET_HANDLER(ConvDeleteReq, ConvDeleteReqPacket, HandleConvDeleteReq);
REGISTER_CHAT_PACKET_HANDLER(ConvRenameReq, ConvRenameReqPacket, HandleConvRenameReq);