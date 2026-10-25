
//***************************************************************************
// ConvDBHandler.cpp : kDbCallIdent_Conv 핸들러 (대화방 1:1/그룹 — 생성/초대/나가기/목록/메시지/읽음)
//
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncHandler.h>
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include "../ChatCommon/Protocol/ChatPacketUtil.h"
#include "DbServiceManager.h"
#include "DbHandlerUtil.h"
#include "DBConvRequest.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	using PublicIdArr = std::array<BYTE, kPublicIdBytes>;
	using Row = DbUtil::Row;
	using DbUtil::ToI64;
	using CConvSql = DbUtil::CSql;	// 공용 문자열 파라미터 구문 도우미(DbHandlerUtil.h)

	// 정규화된 쌍 (작은 쪽, 큰 쪽) — friendships/direct_key의 저장 규칙. 소문자 16진 32자라 문자열 비교로 충분.
	void MakePair(const std::string& x, const std::string& y, std::string& lo, std::string& hi)
	{
		if( x < y ) { lo = x; hi = y; }
		else { lo = y; hi = x; }
	}

	// ── SQL ───────────────────────────────────────────────────────────────
	const TCHAR* const kSqlUserById =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, ''), 0 FROM users u ")
		_T("LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("WHERE u.public_id = ?");

	const TCHAR* const kSqlMembership =
		_T("SELECT m.is_active, m.visible_from_msg_id, m.last_read_msg_id, c.conv_type, c.last_msg_id ")
		_T("FROM conversation_members m JOIN conversations c ON c.conv_id = m.conv_id ")
		_T("WHERE m.conv_id = ? AND m.user_public_id = ?");

	// 요청자 기준 대화방 요약. 컬럼 순서는 ParseSummary()와 짝이다.
	//  0 conv_id, 1 type, 2 name, 3 memberCount, 4 unread, 5 lastMsgId, 6 lastTime, 7 lastKind,
	//  8 lastBody, 9 myLastRead, 10 peerId, 11 peerNickname, 12 peerImage
	// 안 읽은 수는 내가 차단한 사람의 메시지와 시스템 메시지를 제외한 텍스트 메시지만 센다.
	const TCHAR* const kSqlSummaryBase =
		_T("SELECT c.conv_id, c.conv_type, c.name, ")
		_T("(SELECT COUNT(*) FROM conversation_members x WHERE x.conv_id = c.conv_id), ")
		_T("(SELECT COUNT(*) FROM conversation_messages g WHERE g.conv_id = c.conv_id AND g.kind IN (0, 2) ")
		_T("  AND g.msg_id > GREATEST(m.last_read_msg_id, m.visible_from_msg_id) ")
		_T("  AND NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = m.user_public_id AND b.blocked_id = g.sender_public_id)), ")
		_T("CASE WHEN c.last_msg_id > m.visible_from_msg_id THEN c.last_msg_id ELSE 0 END, ")
		_T("COALESCE(UNIX_TIMESTAMP(l.created_at), 0), COALESCE(l.kind, 0), COALESCE(l.body, ''), ")
		_T("m.last_read_msg_id, COALESCE(pm.user_public_id, ''), COALESCE(pu.nickname, ''), COALESCE(pi.image_ref, '') ")
		_T("FROM conversation_members m JOIN conversations c ON c.conv_id = m.conv_id ")
		_T("LEFT JOIN conversation_messages l ON l.msg_id = c.last_msg_id AND l.msg_id > m.visible_from_msg_id ")
		_T("LEFT JOIN conversation_members pm ON pm.conv_id = c.conv_id AND c.conv_type = 1 AND pm.user_public_id <> m.user_public_id ")
		_T("LEFT JOIN users pu ON pu.public_id = pm.user_public_id ")
		_T("LEFT JOIN user_profile_images pi ON pi.user_public_id = pu.public_id AND pi.status = 1 ")
		_T("WHERE m.user_public_id = ? AND m.is_active = 1 ");
	const TCHAR* const kSqlSummaryOneTail = _T("AND c.conv_id = ?");
	const TCHAR* const kSqlSummaryListTail = _T("ORDER BY COALESCE(l.created_at, c.created_at) DESC, c.conv_id DESC LIMIT ");	// 뒤에 정수를 붙인다

	const TCHAR* const kSqlMembers =
		_T("SELECT m.user_public_id, COALESCE(u.nickname, ''), COALESCE(pi.image_ref, ''), m.last_read_msg_id ")
		_T("FROM conversation_members m LEFT JOIN users u ON u.public_id = m.user_public_id ")
		_T("LEFT JOIN user_profile_images pi ON pi.user_public_id = m.user_public_id AND pi.status = 1 ")
		_T("WHERE m.conv_id = ? ORDER BY m.joined_at, m.user_public_id");
	const TCHAR* const kSqlActiveMembers =
		_T("SELECT user_public_id FROM conversation_members WHERE conv_id = ? AND is_active = 1");
	// 1:1 상대와 양방향 차단 여부를 한 번에 조회한다. 파라미터: me, me, convId, me.
	// 컬럼: 상대 publicId, 내가 상대를 차단했는가(0/1), 상대가 나를 차단했는가(0/1).
	const TCHAR* const kSqlOtherMemberBlocks =
		_T("SELECT m.user_public_id, ")
		_T("EXISTS(SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_id = m.user_public_id), ")
		_T("EXISTS(SELECT 1 FROM user_blocks b WHERE b.blocker_id = m.user_public_id AND b.blocked_id = ?) ")
		_T("FROM conversation_members m WHERE m.conv_id = ? AND m.user_public_id <> ?");

	// 초대 대상 검증을 한 번에 조회한다. 뒤에 "?, ?, ...)"(대상 publicId들)를 이어 붙인다.
	// 파라미터: me, me, me, me, 대상들. 컬럼: publicId, 닉네임, 프로필 이미지, 친구(0/1), 내가 차단(0/1), 상대가 차단(0/1).
	// 친구 쌍은 (작은 ID, 큰 ID)로 저장되므로 LEAST/GREATEST로 맞춘다(소문자 16진 문자열이라 C++ 비교와 순서가 같다).
	const TCHAR* const kSqlValidateTargets =
		_T("SELECT u.public_id, u.nickname, COALESCE(p.image_ref, ''), ")
		_T("EXISTS(SELECT 1 FROM friendships f WHERE f.user_a = LEAST(?, u.public_id) AND f.user_b = GREATEST(?, u.public_id)), ")
		_T("EXISTS(SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_id = u.public_id), ")
		_T("EXISTS(SELECT 1 FROM user_blocks b WHERE b.blocker_id = u.public_id AND b.blocked_id = ?) ")
		_T("FROM users u LEFT JOIN user_profile_images p ON p.user_public_id = u.public_id AND p.status = 1 ")
		_T("WHERE u.public_id IN (");

	// 방금 INSERT한 메시지의 id와 시각을 한 번에 읽는다(같은 연결의 LAST_INSERT_ID()).
	const TCHAR* const kSqlLastInsertedMessage =
		_T("SELECT msg_id, UNIX_TIMESTAMP(created_at) FROM conversation_messages WHERE msg_id = LAST_INSERT_ID()");

	// 컬럼 7~10은 답장 정보. 답장 대상이 내가 들어오기 전 메시지(보이지 않음)이거나 삭제됐으면 Deleted(3)로 내린다.
	// 첫 파라미터는 visible_from_msg_id (답장 대상 가시성 판단).
	const TCHAR* const kSqlHistory =
		_T("SELECT g.msg_id, g.kind, COALESCE(g.sender_public_id, ''), COALESCE(u.nickname, ''), COALESCE(pi.image_ref, ''), ")
		_T("UNIX_TIMESTAMP(g.created_at), g.body, ")
		_T("COALESCE(g.reply_to_msg_id, 0), ")
		_T("CASE WHEN g.reply_to_msg_id IS NULL THEN 0 WHEN r.msg_id IS NULL THEN 3 ELSE r.kind END, ")
		_T("COALESCE(ru.nickname, ''), COALESCE(r.body, '') ")
		_T("FROM conversation_messages g LEFT JOIN users u ON u.public_id = g.sender_public_id ")
		_T("LEFT JOIN user_profile_images pi ON pi.user_public_id = u.public_id AND pi.status = 1 ")
		_T("LEFT JOIN conversation_messages r ON r.msg_id = g.reply_to_msg_id AND r.msg_id > ? ")
		_T("LEFT JOIN users ru ON ru.public_id = r.sender_public_id ")
		_T("WHERE g.conv_id = ? AND g.msg_id > ? AND (? = 0 OR g.msg_id < ?) ")
		_T("AND NOT EXISTS (SELECT 1 FROM user_blocks b WHERE b.blocker_id = ? AND b.blocked_id = g.sender_public_id) ")
		_T("ORDER BY g.msg_id DESC LIMIT ");	// 뒤에 정수를 붙인다

	const TCHAR* const kSqlInsertDirectConv = _T("INSERT IGNORE INTO conversations (conv_type, direct_key) VALUES (1, ?)");
	const TCHAR* const kSqlConvByDirectKey = _T("SELECT conv_id FROM conversations WHERE direct_key = ?");
	const TCHAR* const kSqlInsertGroupConv = _T("INSERT INTO conversations (conv_type, name) VALUES (2, ?)");
	const TCHAR* const kSqlDeleteConv = _T("DELETE FROM conversations WHERE conv_id = ?");

	// 멤버 추가(이미 있으면 무시). 읽음/가시 위치를 지금의 last_msg_id로 맞춰, 들어오기 전 대화는 보이지 않게 한다.
	const TCHAR* const kSqlInsertMember =
		_T("INSERT IGNORE INTO conversation_members (conv_id, user_public_id, last_read_msg_id, visible_from_msg_id) ")
		_T("SELECT c.conv_id, ?, c.last_msg_id, c.last_msg_id FROM conversations c WHERE c.conv_id = ?");
	const TCHAR* const kSqlActivateMember =
		_T("UPDATE conversation_members SET is_active = 1 WHERE conv_id = ? AND user_public_id = ?");
	const TCHAR* const kSqlReviveOthers =
		_T("UPDATE conversation_members SET is_active = 1 WHERE conv_id = ? AND user_public_id <> ? AND is_active = 0");
	const TCHAR* const kSqlHideDirectMember =
		_T("UPDATE conversation_members m JOIN conversations c ON c.conv_id = m.conv_id ")
		_T("SET m.is_active = 0, m.visible_from_msg_id = c.last_msg_id, m.last_read_msg_id = c.last_msg_id ")
		_T("WHERE m.conv_id = ? AND m.user_public_id = ?");
	const TCHAR* const kSqlDeleteMember = _T("DELETE FROM conversation_members WHERE conv_id = ? AND user_public_id = ?");
	const TCHAR* const kSqlMemberCount = _T("SELECT COUNT(*) FROM conversation_members WHERE conv_id = ?");

	const TCHAR* const kSqlInsertMessage =
		_T("INSERT INTO conversation_messages (conv_id, sender_public_id, kind, body) VALUES (?, ?, ?, ?)");
	const TCHAR* const kSqlInsertReplyMessage =
		_T("INSERT INTO conversation_messages (conv_id, sender_public_id, kind, body, reply_to_msg_id) VALUES (?, ?, ?, ?, ?)");
	// 답장 대상 검증: 이 대화방의 메시지이고 내게 보이며 일반(0)/사진(2)인 것만.
	const TCHAR* const kSqlReplyTarget =
		_T("SELECT kind FROM conversation_messages WHERE msg_id = ? AND conv_id = ? AND msg_id > ?");
	const TCHAR* const kSqlReplyInfo =
		_T("SELECT r.kind, COALESCE(u.nickname, ''), r.body FROM conversation_messages r ")
		_T("LEFT JOIN users u ON u.public_id = r.sender_public_id WHERE r.msg_id = ?");
	const TCHAR* const kSqlMessageOwner =
		_T("SELECT COALESCE(sender_public_id, ''), kind FROM conversation_messages WHERE msg_id = ? AND conv_id = ?");
	const TCHAR* const kSqlDeleteMessage =
		_T("UPDATE conversation_messages SET kind = 3, body = '', reply_to_msg_id = NULL ")
		_T("WHERE msg_id = ? AND conv_id = ? AND sender_public_id = ? AND kind IN (0, 2)");
	const TCHAR* const kSqlRenameConv = _T("UPDATE conversations SET name = ? WHERE conv_id = ? AND conv_type = 2");
	const TCHAR* const kSqlInsertSystemMessage =
		_T("INSERT INTO conversation_messages (conv_id, sender_public_id, kind, body) VALUES (?, NULL, 1, ?)");
	const TCHAR* const kSqlSetLastMsg = _T("UPDATE conversations SET last_msg_id = GREATEST(last_msg_id, ?) WHERE conv_id = ?");
	const TCHAR* const kSqlSetRead =
		_T("UPDATE conversation_members SET last_read_msg_id = GREATEST(last_read_msg_id, ?) WHERE conv_id = ? AND user_public_id = ?");

	//***************************************************************************
	// @brief 대화방 연산 하나를 수행한다. 결과는 SConvOpResult로, 반환값은 최종 EConvResult다.
	// @details 여러 구문을 하나의 트랜잭션으로 묶지 않는다(친구/프로필 이미지와 같은 실용적 타협).
	//          쓰기를 INSERT IGNORE / 조건부 UPDATE로 멱등하게 구성했고, "멤버십 -> 메시지 ->
	//          마지막 메시지 갱신" 순서라 중간에 실패해도 재시도하면 수렴한다.
	//***************************************************************************
	class CConvWork
	{
	public:
		CConvWork(ST_CONV_REQ* req, OdbcConnGuard& guard, SConvOpResult& out)
			: _req(req), _sql(guard), _out(out), _me(PublicIdToHex(req->publicId))
		{
		}

		EConvResult Run()
		{
			switch( _req->op )
			{
			case EConvOp::OpenDirect:	return OpenDirect();
			case EConvOp::CreateGroup:	return CreateGroup();
			case EConvOp::Invite:		return Invite();
			case EConvOp::Leave:		return Leave();
			case EConvOp::List:			return List();
			case EConvOp::Send:			return Send();
			case EConvOp::History:		return History();
			case EConvOp::MarkRead:		return MarkRead();
			case EConvOp::Members:		return Members();
			case EConvOp::Delete:		return Delete();
			case EConvOp::Rename:		return Rename();
			}
			return EConvResult::DbError;
		}

	private:
		struct SMembership
		{
			bool	found = false;
			bool	active = false;
			int64	visibleFrom = 0;
			int64	lastRead = 0;
			uint8	type = 0;
			int64	lastMsgId = 0;
		};

		static bool ParseMember(const Row& row, SConvMember& member)
		{
			return HexToPublicId(row[0], member.publicId);
		}

		bool LoadUser(const std::string& hex, SConvMember& member, bool& found)
		{
			std::vector<Row> rows;
			if( !_sql.Query(kSqlUserById, { hex }, 4, rows) )
				return false;
			found = !rows.empty() && ParseMember(rows.front(), member);
			if( found )
			{
				member.nickname = rows.front()[1];
				member.profileImageRef = rows.front()[2];
			}
			return true;
		}

		bool LoadMembership(int64 convId, SMembership& membership)
		{
			std::vector<Row> rows;
			if( !_sql.Query(kSqlMembership, { std::to_string(convId), _me }, 5, rows) )
				return false;
			if( rows.empty() )
				return true;	// found=false

			const Row& row = rows.front();
			membership.found = true;
			membership.active = (ToI64(row[0]) != 0);
			membership.visibleFrom = ToI64(row[1]);
			membership.lastRead = ToI64(row[2]);
			membership.type = static_cast<uint8>(ToI64(row[3]));
			membership.lastMsgId = ToI64(row[4]);
			return true;
		}

		static void ParseSummary(const Row& row, SConvSummary& summary)
		{
			summary.convId = ToI64(row[0]);
			summary.type = static_cast<uint8>(ToI64(row[1]));
			summary.name = row[2];
			summary.memberCount = static_cast<int32>(ToI64(row[3]));
			summary.unreadCount = static_cast<int32>(ToI64(row[4]));
			summary.lastMsgId = ToI64(row[5]);
			summary.lastMsgTime = ToI64(row[6]);
			summary.lastKind = static_cast<uint8>(ToI64(row[7]));
			summary.lastMessage = row[8];
			summary.myLastReadMsgId = ToI64(row[9]);
			if( !row[10].empty() )
			{
				HexToPublicId(row[10], summary.peer.publicId);
				summary.peer.nickname = row[11];
				summary.peer.profileImageRef = row[12];
			}
		}

		bool LoadSummary(int64 convId, SConvSummary& summary, bool& found)
		{
			_tstring query = _tstring(kSqlSummaryBase) + kSqlSummaryOneTail;
			std::vector<Row> rows;
			if( !_sql.Query(query.c_str(), { _me, std::to_string(convId) }, 13, rows) )
				return false;
			found = !rows.empty();
			if( found )
				ParseSummary(rows.front(), summary);
			return true;
		}

		bool LoadActiveMembers(int64 convId, std::vector<PublicIdArr>& ids)
		{
			ids.clear();
			std::vector<Row> rows;
			if( !_sql.Query(kSqlActiveMembers, { std::to_string(convId) }, 1, rows) )
				return false;
			for( const Row& row : rows )
			{
				PublicIdArr id{};
				if( HexToPublicId(row[0], id) )
					ids.push_back(id);
			}
			return true;
		}

		// 메시지 저장 + 방의 마지막 메시지 갱신. sender가 nullptr이면 시스템 메시지.
		bool InsertMessage(int64 convId, const SConvMember* sender, const std::string& body, SConvMessage& msg,
			uint8 kind = static_cast<uint8>(EConvMessageKind::Text), int64 replyToMsgId = 0)
		{
			bool ok;
			if( sender != nullptr && replyToMsgId > 0 )
				ok = _sql.Exec(kSqlInsertReplyMessage, { std::to_string(convId), PublicIdToHex(sender->publicId), std::to_string(kind), body, std::to_string(replyToMsgId) });
			else if( sender != nullptr )
				ok = _sql.Exec(kSqlInsertMessage, { std::to_string(convId), PublicIdToHex(sender->publicId), std::to_string(kind), body });
			else
				ok = _sql.Exec(kSqlInsertSystemMessage, { std::to_string(convId), body });
			if( !ok )
				return false;

			// LAST_INSERT_ID()는 연결 단위라 UPDATE보다 먼저 읽는다. id와 시각은 한 번에 조회한다.
			std::vector<Row> rows;
			if( !_sql.Query(kSqlLastInsertedMessage, {}, 2, rows) || rows.empty() )
				return false;
			const int64 msgId = ToI64(rows.front()[0]);
			const int64 timestamp = ToI64(rows.front()[1]);
			if( msgId <= 0 )
				return false;
			if( !_sql.Exec(kSqlSetLastMsg, { std::to_string(msgId), std::to_string(convId) }) )
				return false;

			msg = SConvMessage();
			msg.convId = convId;
			msg.msgId = msgId;
			msg.kind = (sender != nullptr) ? kind : static_cast<uint8>(EConvMessageKind::System);
			msg.body = body;
			msg.timestamp = timestamp;
			if( sender != nullptr && replyToMsgId > 0 )
			{
				msg.replyToMsgId = replyToMsgId;
				msg.replyKind = static_cast<uint8>(EConvMessageKind::Deleted);
				std::vector<Row> replyRows;
				if( _sql.Query(kSqlReplyInfo, { std::to_string(replyToMsgId) }, 3, replyRows) && !replyRows.empty() )
				{
					msg.replyKind = static_cast<uint8>(ToI64(replyRows.front()[0]));
					msg.replySenderNickname = replyRows.front()[1];
					msg.replyBody = replyRows.front()[2];
				}
			}
			if( sender != nullptr )
			{
				msg.hasSender = true;
				msg.sender = sender->publicId;
				msg.senderNickname = sender->nickname;
				msg.senderImageRef = sender->profileImageRef;
			}
			return true;
		}

		bool AddMember(int64 convId, const std::string& hex)
		{
			return _sql.Exec(kSqlInsertMember, { hex, std::to_string(convId) });
		}

		// 요청자 자신의 정보(닉네임/이미지) — 메시지 발신자/안내 문구용.
		bool LoadMe(SConvMember& me)
		{
			bool found = false;
			if( !LoadUser(_me, me, found) )
				return false;
			if( !found )
				me.publicId = ToPublicId(_req->publicId);
			return true;
		}

		//***************************************************************************
		// @brief 초대/생성 대상을 검증한다. 자기 자신/중복은 건너뛰고, 존재하지 않거나 친구가
		//        아니거나 차단 관계인 대상도 건너뛴다.
		// @param valid 통과한 대상
		// @param firstFail 하나도 통과하지 못했을 때 보고할 첫 실패 사유
		// @return DB 오류 없이 검증을 마쳤는지
		//***************************************************************************
		bool ValidateTargets(std::vector<SConvMember>& valid, EConvResult& firstFail, const std::vector<std::string>& skipHex)
		{
			valid.clear();
			firstFail = EConvResult::InvalidTarget;
			bool failSet = false;
			std::vector<std::string> seen = skipHex;
			seen.push_back(_me);

			// 건너뛸 대상(자기 자신/중복/제외 대상)을 거르고 검증할 대상 목록을 만든다.
			std::vector<std::string> hexes;
			const int32 count = (std::max)(0, (std::min)(_req->targetCount, kMaxConvInvite));
			for( int32 i = 0; i < count; ++i )
			{
				const std::string hex = PublicIdToHex(_req->targets[i]);
				if( std::find(seen.begin(), seen.end(), hex) != seen.end() )
					continue;
				seen.push_back(hex);
				hexes.push_back(hex);
			}
			if( hexes.empty() )
				return true;

			// 대상 전체를 쿼리 한 번으로 조회한다(존재/닉네임/친구/양방향 차단).
			_tstring query = kSqlValidateTargets;
			std::vector<std::string> params = { _me, _me, _me, _me };
			for( size_t i = 0; i < hexes.size(); ++i )
			{
				query += (i == 0) ? _T("?") : _T(", ?");
				params.push_back(hexes[i]);
			}
			query += _T(")");

			std::vector<Row> rows;
			if( !_sql.Query(query.c_str(), params, 6, rows) )
				return false;

			// 이후 판정은 입력 순서대로 해서 "첫 실패 사유"가 이전과 같게 유지한다.
			for( const std::string& hex : hexes )
			{
				auto fail = [&](EConvResult reason)
					{
						if( !failSet ) { firstFail = reason; failSet = true; }
					};

				const Row* found = nullptr;
				for( const Row& row : rows )
				{
					if( row[0] == hex ) { found = &row; break; }
				}

				SConvMember member;
				if( found == nullptr || !ParseMember(*found, member) ) { fail(EConvResult::InvalidTarget); continue; }
				member.nickname = (*found)[1];
				member.profileImageRef = (*found)[2];

				if( (*found)[3] != "1" ) { fail(EConvResult::NotFriends); continue; }
				if( (*found)[4] == "1" || (*found)[5] == "1" ) { fail(EConvResult::Blocked); continue; }

				valid.push_back(std::move(member));
			}
			return true;
		}

		static std::string JoinNicknames(const std::vector<SConvMember>& members)
		{
			std::string joined;
			for( size_t i = 0; i < members.size(); ++i )
			{
				if( i > 0 )
					joined += ", ";
				joined += members[i].nickname;
			}
			return joined;
		}

		// ── 1:1 열기 ────────────────────────────────────────────────────────
		EConvResult OpenDirect()
		{
			if( _req->targetCount < 1 )
				return EConvResult::InvalidTarget;

			std::vector<SConvMember> valid;
			EConvResult fail;
			if( !ValidateTargets(valid, fail, {}) )
				return EConvResult::DbError;
			if( valid.empty() )
				return fail;

			const std::string peerHex = PublicIdToHex(valid.front().publicId);
			std::string lo, hi;
			MakePair(_me, peerHex, lo, hi);
			const std::string key = lo + ":" + hi;

			int64 affected = 0;
			if( !_sql.Exec(kSqlInsertDirectConv, { key }, &affected) )
				return EConvResult::DbError;
			_out.isNew = (affected > 0);

			std::vector<Row> rows;
			if( !_sql.Query(kSqlConvByDirectKey, { key }, 1, rows) || rows.empty() )
				return EConvResult::DbError;
			const int64 convId = ToI64(rows.front()[0]);

			if( !AddMember(convId, _me) || !AddMember(convId, peerHex) )
				return EConvResult::DbError;
			// 내가 예전에 나갔다면(숨김) 되살린다. 상대의 숨김 상태는 건드리지 않는다.
			if( !_sql.Exec(kSqlActivateMember, { std::to_string(convId), _me }) )
				return EConvResult::DbError;

			bool found = false;
			if( !LoadSummary(convId, _out.summary, found) || !found )
				return EConvResult::DbError;
			_out.convId = convId;
			return EConvResult::Ok;
		}

		// ── 그룹 생성 ───────────────────────────────────────────────────────
		EConvResult CreateGroup()
		{
			std::vector<SConvMember> valid;
			EConvResult fail;
			if( !ValidateTargets(valid, fail, {}) )
				return EConvResult::DbError;
			if( valid.empty() )
				return fail;

			char safeName[sizeof(_req->name) + 1] = {};
			::memcpy(safeName, _req->name, sizeof(_req->name));
			const std::string name(safeName);

			if( !_sql.Exec(kSqlInsertGroupConv, { name }) )
				return EConvResult::DbError;
			int64 convId = 0;
			if( !_sql.LastInsertId(convId) || convId <= 0 )
				return EConvResult::DbError;

			// 이후 단계가 실패하면 반쯤 만들어진 방이 남지 않도록 방을 지운다(멤버/메시지는 CASCADE).
			auto rollback = [&]() -> EConvResult
				{
					_sql.Exec(kSqlDeleteConv, { std::to_string(convId) });
					return EConvResult::DbError;
				};

			if( !AddMember(convId, _me) )
				return rollback();
			for( const SConvMember& member : valid )
			{
				if( !AddMember(convId, PublicIdToHex(member.publicId)) )
					return rollback();
			}

			SConvMember me;
			if( !LoadMe(me) )
				return rollback();
			const std::string notice = me.nickname + "님이 " + JoinNicknames(valid) + "님을 초대했습니다.";
			if( !InsertMessage(convId, nullptr, notice, _out.newMsg) )
				return rollback();
			_out.hasNewMsg = true;

			for( const SConvMember& member : valid )
				_out.addedMembers.push_back(member.publicId);
			if( !LoadActiveMembers(convId, _out.recipients) )
				return EConvResult::DbError;

			bool found = false;
			if( !LoadSummary(convId, _out.summary, found) || !found )
				return EConvResult::DbError;
			_out.convId = convId;
			_out.isNew = true;
			return EConvResult::Ok;
		}

		// ── 초대 ────────────────────────────────────────────────────────────
		EConvResult Invite()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			if( membership.type != static_cast<uint8>(EConvType::Group) )
				return EConvResult::NotGroup;

			// 이미 멤버인 대상은 건너뛴다.
			std::vector<Row> memberRows;
			if( !_sql.Query(kSqlMembers, { std::to_string(_req->convId) }, 4, memberRows) )
				return EConvResult::DbError;
			std::vector<std::string> existing;
			for( const Row& row : memberRows )
				existing.push_back(row[0]);

			std::vector<SConvMember> valid;
			EConvResult fail;
			if( !ValidateTargets(valid, fail, existing) )
				return EConvResult::DbError;
			if( valid.empty() )
				return fail;
			if( static_cast<int32>(existing.size() + valid.size()) > kMaxConvMembers )
				return EConvResult::MemberLimit;

			for( const SConvMember& member : valid )
			{
				if( !AddMember(_req->convId, PublicIdToHex(member.publicId)) )
					return EConvResult::DbError;
			}

			SConvMember me;
			if( !LoadMe(me) )
				return EConvResult::DbError;
			const std::string notice = me.nickname + "님이 " + JoinNicknames(valid) + "님을 초대했습니다.";
			if( !InsertMessage(_req->convId, nullptr, notice, _out.newMsg) )
				return EConvResult::DbError;
			_out.hasNewMsg = true;

			for( const SConvMember& member : valid )
				_out.addedMembers.push_back(member.publicId);
			if( !LoadActiveMembers(_req->convId, _out.recipients) )
				return EConvResult::DbError;

			bool found = false;
			if( !LoadSummary(_req->convId, _out.summary, found) || !found )
				return EConvResult::DbError;
			_out.convId = _req->convId;
			return EConvResult::Ok;
		}

		// ── 나가기 ──────────────────────────────────────────────────────────
		EConvResult Leave()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			if( membership.type == static_cast<uint8>(EConvType::Direct) )
			{
				// 1:1은 내 목록에서만 숨긴다. 상대가 보내거나 내가 다시 열면 돌아온다.
				return _sql.Exec(kSqlHideDirectMember, { std::to_string(_req->convId), _me }) ? EConvResult::Ok : EConvResult::DbError;
			}

			SConvMember me;
			if( !LoadMe(me) )
				return EConvResult::DbError;
			if( !_sql.Exec(kSqlDeleteMember, { std::to_string(_req->convId), _me }) )
				return EConvResult::DbError;

			std::vector<Row> rows;
			if( !_sql.Query(kSqlMemberCount, { std::to_string(_req->convId) }, 1, rows) || rows.empty() )
				return EConvResult::DbError;
			if( ToI64(rows.front()[0]) <= 0 )
			{
				// 마지막 멤버가 나가면 방 자체를 지운다(메시지는 CASCADE).
				return _sql.Exec(kSqlDeleteConv, { std::to_string(_req->convId) }) ? EConvResult::Ok : EConvResult::DbError;
			}

			if( !InsertMessage(_req->convId, nullptr, me.nickname + "님이 나갔습니다.", _out.newMsg) )
				return EConvResult::DbError;
			_out.hasNewMsg = true;
			if( !LoadActiveMembers(_req->convId, _out.recipients) )
				return EConvResult::DbError;
			return EConvResult::Ok;
		}

		// ── 목록 ────────────────────────────────────────────────────────────
		EConvResult List()
		{
			_tstring query = _tstring(kSqlSummaryBase) + kSqlSummaryListTail + Utf8ToTString(std::to_string(kMaxConvList));
			std::vector<Row> rows;
			if( !_sql.Query(query.c_str(), { _me }, 13, rows) )
				return EConvResult::DbError;

			for( const Row& row : rows )
			{
				SConvSummary summary;
				ParseSummary(row, summary);
				_out.convs.push_back(std::move(summary));
			}
			return EConvResult::Ok;
		}

		// ── 메시지 전송 ─────────────────────────────────────────────────────
		EConvResult Send()
		{
			char safeBody[sizeof(_req->message) + 1] = {};
			::memcpy(safeBody, _req->message, sizeof(_req->message));
			const std::string body(safeBody);
			if( body.empty() )
				return EConvResult::InvalidMessage;
			if( _req->msgKind != static_cast<uint8>(EConvMessageKind::Text) && _req->msgKind != static_cast<uint8>(EConvMessageKind::Image) )
				return EConvResult::InvalidMessage;

			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			const bool isDirect = (membership.type == static_cast<uint8>(EConvType::Direct));
			if( isDirect )
			{
				std::vector<Row> rows;
				if( !_sql.Query(kSqlOtherMemberBlocks, { _me, _me, std::to_string(_req->convId), _me }, 3, rows) )
					return EConvResult::DbError;
				if( !rows.empty() )
				{
					const bool iBlock = (rows.front()[1] == "1");
					const bool theyBlock = (rows.front()[2] == "1");
					if( iBlock )
						return EConvResult::Blocked;	// 내가 차단한 상대에게는 보낼 수 없다
					_out.silent = theyBlock;			// 상대가 나를 차단함 — 저장만 하고 푸시/되살리기는 하지 않는다
				}
			}

			if( _req->replyToMsgId > 0 )
			{
				std::vector<Row> target;
				if( !_sql.Query(kSqlReplyTarget, { std::to_string(_req->replyToMsgId), std::to_string(_req->convId), std::to_string(membership.visibleFrom) }, 1, target) )
					return EConvResult::DbError;
				if( target.empty() )
					return EConvResult::InvalidReply;
				const int64 targetKind = ToI64(target.front()[0]);
				if( targetKind != static_cast<int64>(EConvMessageKind::Text) && targetKind != static_cast<int64>(EConvMessageKind::Image) )
					return EConvResult::InvalidReply;
			}

			SConvMember me;
			if( !LoadMe(me) )
				return EConvResult::DbError;
			if( !InsertMessage(_req->convId, &me, body, _out.newMsg, _req->msgKind, _req->replyToMsgId) )
				return EConvResult::DbError;
			_out.hasNewMsg = true;

			// 내가 보낸 메시지까지는 읽은 것으로 본다.
			if( !_sql.Exec(kSqlSetRead, { std::to_string(_out.newMsg.msgId), std::to_string(_req->convId), _me }) )
				return EConvResult::DbError;
			if( isDirect && !_out.silent && !_sql.Exec(kSqlReviveOthers, { std::to_string(_req->convId), _me }) )
				return EConvResult::DbError;

			if( !LoadActiveMembers(_req->convId, _out.recipients) )
				return EConvResult::DbError;
			return EConvResult::Ok;
		}

		// ── 기록 조회 ───────────────────────────────────────────────────────
		EConvResult History()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			const int32 limit = (std::max)(1, (std::min)(_req->count, kMaxConvHistoryPage));
			const std::string before = std::to_string((std::max<int64>)(0, _req->beforeMsgId));

			// 한 건 더 읽어서 더 오래된 메시지가 있는지 판단한다.
			_tstring query = _tstring(kSqlHistory) + Utf8ToTString(std::to_string(limit + 1));
			std::vector<Row> rows;
			if( !_sql.Query(query.c_str(), { std::to_string(membership.visibleFrom), std::to_string(_req->convId), std::to_string(membership.visibleFrom), before, before, _me }, 11, rows) )
				return EConvResult::DbError;

			_out.hasMore = (static_cast<int32>(rows.size()) > limit);
			if( _out.hasMore )
				rows.resize(limit);

			for( auto it = rows.rbegin(); it != rows.rend(); ++it )	// 최신순으로 읽었으니 뒤집어 오래된 것부터
			{
				SConvMessage msg;
				msg.convId = _req->convId;
				msg.msgId = ToI64((*it)[0]);
				msg.kind = static_cast<uint8>(ToI64((*it)[1]));
				if( !(*it)[2].empty() && HexToPublicId((*it)[2], msg.sender) )
					msg.hasSender = true;
				msg.senderNickname = (*it)[3];
				msg.senderImageRef = (*it)[4];
				msg.timestamp = ToI64((*it)[5]);
				msg.body = (*it)[6];
				msg.replyToMsgId = ToI64((*it)[7]);
				msg.replyKind = static_cast<uint8>(ToI64((*it)[8]));
				msg.replySenderNickname = (*it)[9];
				msg.replyBody = (*it)[10];
				_out.messages.push_back(std::move(msg));
			}
			return EConvResult::Ok;
		}

		// ── 그룹 이름 변경 ──────────────────────────────────────────────────
		// 멤버 누구나 바꿀 수 있고, 바뀐 사실은 시스템 안내 메시지로 남는다. 빈 이름은 이름 해제.
		EConvResult Rename()
		{
			char safeName[sizeof(_req->name) + 1] = {};
			::memcpy(safeName, _req->name, sizeof(_req->name));
			std::string name(safeName);
			const size_t first = name.find_first_not_of(" \t");
			const size_t last = name.find_last_not_of(" \t");
			name = (first == std::string::npos) ? std::string() : name.substr(first, last - first + 1);
			if( name.find_first_of("\r\n") != std::string::npos )
				return EConvResult::InvalidName;

			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			if( membership.type != static_cast<uint8>(EConvType::Group) )
				return EConvResult::NotGroup;
			_out.convId = _req->convId;

			SConvMember me;
			if( !LoadMe(me) )
				return EConvResult::DbError;
			if( !_sql.Exec(kSqlRenameConv, { name, std::to_string(_req->convId) }) )
				return EConvResult::DbError;
			_out.renamedTo = name;

			const std::string notice = name.empty()
				? me.nickname + "님이 대화방 이름을 해제했습니다."
				: me.nickname + "님이 대화방 이름을 '" + name + "'(으)로 변경했습니다.";
			if( !InsertMessage(_req->convId, nullptr, notice, _out.newMsg) )
				return EConvResult::DbError;
			_out.hasNewMsg = true;
			if( !LoadActiveMembers(_req->convId, _out.recipients) )
				return EConvResult::DbError;
			return EConvResult::Ok;
		}

		// ── 메시지 삭제(소프트) ──────────────────────────────────────────────
		// 내가 보낸 일반/사진 메시지만. kind를 Deleted로 바꾸고 본문을 비워 모두에게서 지운다.
		EConvResult Delete()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			std::vector<Row> rows;
			if( !_sql.Query(kSqlMessageOwner, { std::to_string(_req->msgId), std::to_string(_req->convId) }, 2, rows) )
				return EConvResult::DbError;
			if( rows.empty() || ToI64(rows.front()[1]) == static_cast<int64>(EConvMessageKind::Deleted) )
				return EConvResult::NotFound;
			if( rows.front()[0] != _me || ToI64(rows.front()[1]) == static_cast<int64>(EConvMessageKind::System) )
				return EConvResult::NotSender;

			int64 affected = 0;
			if( !_sql.Exec(kSqlDeleteMessage, { std::to_string(_req->msgId), std::to_string(_req->convId), _me }, &affected) )
				return EConvResult::DbError;
			if( affected <= 0 )
				return EConvResult::NotFound;

			_out.deletedMsgId = _req->msgId;
			if( !LoadActiveMembers(_req->convId, _out.recipients) )
				return EConvResult::DbError;
			return EConvResult::Ok;
		}

		// ── 읽음 ────────────────────────────────────────────────────────────
		EConvResult MarkRead()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			// 마지막 메시지를 넘어서는 값은 마지막 메시지로 보정한다.
			const int64 upTo = (std::min)((std::max<int64>)(0, _req->upToMsgId), membership.lastMsgId);
			if( upTo > membership.lastRead )
			{
				if( !_sql.Exec(kSqlSetRead, { std::to_string(upTo), std::to_string(_req->convId), _me }) )
					return EConvResult::DbError;
				if( !LoadActiveMembers(_req->convId, _out.recipients) )
					return EConvResult::DbError;
			}
			_out.readUpTo = (std::max)(upTo, membership.lastRead);
			return EConvResult::Ok;
		}

		// ── 멤버 목록 ───────────────────────────────────────────────────────
		EConvResult Members()
		{
			SMembership membership;
			if( !LoadMembership(_req->convId, membership) )
				return EConvResult::DbError;
			if( !membership.found || !membership.active )
				return EConvResult::NotMember;
			_out.convId = _req->convId;

			std::vector<Row> rows;
			if( !_sql.Query(kSqlMembers, { std::to_string(_req->convId) }, 4, rows) )
				return EConvResult::DbError;
			for( const Row& row : rows )
			{
				SConvMember member;
				if( !ParseMember(row, member) )
					continue;
				member.nickname = row[1];
				member.profileImageRef = row[2];
				member.lastReadMsgId = ToI64(row[3]);
				_out.members.push_back(std::move(member));
			}
			return EConvResult::Ok;
		}

	private:
		ST_CONV_REQ* _req;
		CConvSql		_sql;
		SConvOpResult& _out;
		std::string		_me;
	};
}

//***************************************************************************
// @brief 대화방 연산 하나를 처리합니다. 어떤 경로에서도 onComplete가 정확히 한 번 호출됩니다.
//***************************************************************************
DECLARE_DBASYNC_HANDLER_EX(MEMBER_DB_ASYNC, kDbCallIdent_Conv)
{
	ST_CONV_REQ* req = static_cast<ST_CONV_REQ*>(pStAsync);

	SConvOpResult out;
	auto finish = [&](EConvResult result) -> EDBReturnType
		{
			out.result = result;
			if( req->onComplete )
				req->onComplete(out);
			return (result == EConvResult::DbError) ? EDBReturnType::INVALID : EDBReturnType::OK;
		};

	OdbcConnGuard guard(MEMBER_DB_ASYNC.GetOdbcConnPool());
	if( guard == nullptr )
	{
		LOG_ERROR(_T("kDbCallIdent_Conv: No available ODBC connection in pool."));
		return finish(EConvResult::DbError);
	}

	CConvWork work(req, guard, out);
	const EConvResult result = work.Run();
	if( result == EConvResult::DbError )
		LOG_ERROR(_T("kDbCallIdent_Conv: DB operation failed."));
	return finish(result);
}