
//***************************************************************************
// RoomMemberListHandler.cpp : RoomMemberListReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <algorithm>
#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 항목 벡터를 requestId/scope 공통 포맷으로 클라이언트에 스트리밍하고
	//        완료 응답까지 보낸다. Present/History 두 경로가 이 함수로 모인다.
	//***************************************************************************
	void SendMemberListResult(
		CChatSession& session,
		int32 requestId,
		int32 roomId,
		ERoomMemberListScope scope,
		int32 totalCount,
		int32 page,
		int32 pageSize,
		const std::vector<SRoomMemberHistoryInfo>& members)
	{
		for( const SRoomMemberHistoryInfo& member : members )
		{
			RoomMemberListItemResPacket itemRes{};
			itemRes.size = sizeof(itemRes);
			itemRes.type = static_cast<uint16>(EChatPacketType::RoomMemberListItemRes);
			itemRes.requestId = requestId;
			::memcpy(itemRes.publicId, member.publicId.data(), member.publicId.size());

			const size_t nicknameCopyLen = (std::min)(member.nickname.size(), sizeof(itemRes.nickname) - 1);
			::memcpy(itemRes.nickname, member.nickname.data(), nicknameCopyLen);

			const size_t imageUrlCopyLen = (std::min)(member.profileImageUrl.size(), sizeof(itemRes.profileImageUrl) - 1);
			::memcpy(itemRes.profileImageUrl, member.profileImageUrl.data(), imageUrlCopyLen);

			itemRes.online = member.online ? 1 : 0;

			session.Send(&itemRes, sizeof(itemRes));
		}

		RoomMemberListEndResPacket endRes{};
		endRes.size = sizeof(endRes);
		endRes.type = static_cast<uint16>(EChatPacketType::RoomMemberListEndRes);
		endRes.requestId = requestId;
		endRes.roomId = roomId;
		endRes.scope = static_cast<uint8_t>(scope);
		endRes.totalCount = totalCount;
		endRes.page = page;
		endRes.pageSize = pageSize;
		session.Send(&endRes, sizeof(endRes));
	}

	//***************************************************************************
	// @brief scope==Present 처리. CChatServerMain::GetRoomMembers()(인메모리,
	//        DB 없음)로 지금 그 방에 접속해 있는 전체 목록을 받아온 뒤, 이
	//        핸들러에서 직접 한 페이지만 잘라 보낸다 — 목록 자체가 이미
	//        메모리에 있어 별도 비동기 DB 왕복이 필요 없다. 전원이 지금
	//        연결돼 있어야만 이 목록에 들어오므로 online은 항상 1이다.
	//***************************************************************************
	void HandlePresentScope(CChatSession& session, int32 requestId, int32 roomId, int32 page, int32 pageSize)
	{
		CChatServerMain* server = session.GetServer();

		std::vector<SRoomMemberInfo> allMembers = server->GetRoomMembers(roomId);
		const int32 totalCount = static_cast<int32>(allMembers.size());

		const int32 lastPage = (totalCount <= 0) ? 0 : (totalCount - 1) / pageSize;
		const int32 resultPage = (std::min)(page, lastPage);

		const size_t offset = static_cast<size_t>(resultPage) * static_cast<size_t>(pageSize);
		const size_t end = (std::min)(offset + static_cast<size_t>(pageSize), allMembers.size());

		std::vector<SRoomMemberHistoryInfo> pageMembers;
		if( offset < end )
		{
			pageMembers.reserve(end - offset);
			for( size_t i = offset; i < end; ++i )
			{
				SRoomMemberHistoryInfo info;
				info.publicId = allMembers[i].publicId;
				info.nickname = allMembers[i].nickname;
				info.profileImageUrl = allMembers[i].profileImageUrl;
				info.online = true; // Present 범위는 정의상 전원 접속 중
				pageMembers.push_back(std::move(info));
			}
		}

		SendMemberListResult(session, requestId, roomId, ERoomMemberListScope::Present, totalCount, resultPage, pageSize, pageMembers);
	}

	//***************************************************************************
	// @brief scope==History 처리. DB의 room_members(입장 이력)를 페이지
	//        단위로 조회하고, 각 유저의 접속 상태(서버 어딘가에 로그인해
	//        있는지)를 붙여서 보낸다 — CChatServerMain::RequestRoomMemberHistoryPage()
	//        참고. DB 비동기 콜백이라 세션을 weak_ptr로 붙잡는다.
	//***************************************************************************
	void HandleHistoryScope(CChatSession& session, int32 requestId, int32 roomId, int32 page, int32 pageSize)
	{
		CChatServerMain* server = session.GetServer();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestRoomMemberHistoryPage(roomId, page, pageSize,
			[sessionWeak, requestId, roomId, pageSize](ELoginResult result, int32 totalCount, int32 resultPage, const std::vector<SRoomMemberHistoryInfo>& members)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				if( result == ELoginResult::Ok )
				{
					SendMemberListResult(*session, requestId, roomId, ERoomMemberListScope::History, totalCount, resultPage, pageSize, members);
				}
				else
				{
					SendMemberListResult(*session, requestId, roomId, ERoomMemberListScope::History, 0, 0, pageSize, std::vector<SRoomMemberHistoryInfo>());
				}
			});
	}

	//***************************************************************************
	// @brief 방 멤버 목록 조회 요청 처리. scope(지금 접속 중인 사람만 /
	//        입장 이력 전체)에 따라 Present/History 두 경로로 나뉜다.
	// @details [수정 — 접근 범위 확장] roomId는 로그인한 사용자라면 지금
	//          자신이 그 방에 있는지와 무관하게 조회할 수 있다 — 처음엔
	//          "요청자가 실제로 지금 그 방에 있는지" 재확인해서 불일치하면
	//          거부했지만(채팅 화면 안에서 "지금 보고 있는 방"만 조회하는
	//          용도였을 때의 방어), 방 목록 화면(RoomListPanel)에서 "내가
	//          예전에 들어갔던(지금은 다른 곳에 있는) 방"의 멤버도 미리
	//          볼 수 있게 확장하면서 이 제약이 오히려 기능을 막게 됐다.
	//          방의 존재 자체는 이미 전체 방 목록(ListRoomsReq)으로 로그인한
	//          누구나 볼 수 있으므로, 그 방의 멤버 목록까지 여는 것으로
	//          새로 노출되는 정보는 없다고 판단해 이 검증을 제거했다.
	//          roomId가 존재하지 않는 방이어도 GetRoomMembers()/
	//          RequestRoomMemberHistoryPage() 양쪽 다 빈 결과를 돌려줄
	//          뿐이라 별도 유효성 검사가 필요 없다.
	//***************************************************************************
	void HandleRoomMemberListReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		if( session.GetServer() == nullptr )
			return;

		const RoomMemberListReqPacket* packet = reinterpret_cast<const RoomMemberListReqPacket*>(header);
		const int32 requestId = packet->requestId;
		const int32 roomId = packet->roomId;

		const ERoomMemberListScope scope = (packet->scope == static_cast<uint8_t>(ERoomMemberListScope::History))
			? ERoomMemberListScope::History
			: ERoomMemberListScope::Present;

		int32 pageSize = packet->pageSize;
		if( pageSize < 1 )
			pageSize = 1;
		if( pageSize > kMaxRoomPageSize )
			pageSize = kMaxRoomPageSize;

		const int32 page = (packet->page < 0) ? 0 : packet->page;

		if( scope == ERoomMemberListScope::Present )
			HandlePresentScope(session, requestId, roomId, page, pageSize);
		else
			HandleHistoryScope(session, requestId, roomId, page, pageSize);
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomMemberListReq, RoomMemberListReqPacket, HandleRoomMemberListReq);