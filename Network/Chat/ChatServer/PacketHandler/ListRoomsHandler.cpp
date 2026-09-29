
//***************************************************************************
// ListRoomsHandler.cpp : ListRoomsReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 방 목록 조회 요청 처리. 범위(전체/내가 참여한 방), 페이지, 이름
	//        검색어로 한 페이지를 조회해서, 항목마다 ListRoomsItemResPacket을
	//        하나씩 보내고 ListRoomsEndResPacket으로 마무리한다.
	// @details 모든 응답 패킷은 요청의 requestId를 그대로 되돌려준다 —
	//          클라이언트가 이미 다른 요청으로 넘어간 뒤 늦게 도착한 응답을
	//          걸러내는 데 쓴다.
	//***************************************************************************
	void HandleListRoomsReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const ListRoomsReqPacket* packet = reinterpret_cast<const ListRoomsReqPacket*>(header);

		const int32 requestId = packet->requestId;

		// 알 수 없는 scope 값은 전체 방 조회로 취급한다(프로토콜 위반이지만
		// 조회 전용 요청이라 무해하다).
		const ERoomListScope scope = (packet->scope == static_cast<uint8_t>(ERoomListScope::Joined))
			? ERoomListScope::Joined
			: ERoomListScope::All;

		int32 pageSize = packet->pageSize;
		if( pageSize < 1 )
			pageSize = 1;
		if( pageSize > kMaxRoomPageSize )
			pageSize = kMaxRoomPageSize;

		const int32 page = (packet->page < 0) ? 0 : packet->page;

		// keyword는 고정 길이 버퍼라 널 종료가 보장되지 않는다 — 버퍼 안에서
		// 문자열 끝을 찾아 그 길이만큼만 복사한다.
		size_t keywordLen = 0;
		while( keywordLen < sizeof(packet->keyword) && packet->keyword[keywordLen] != '\0' )
			++keywordLen;
		const std::string keyword(packet->keyword, keywordLen);

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestListRooms(sessionRef, scope, session.GetPublicId(), keyword, page, pageSize,
			[server, sessionWeak, requestId, scope, pageSize](ELoginResult result, int32 totalCount, int32 resultPage, const std::vector<SRoomListEntry>& rooms)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				// [설계] 조회 자체가 실패해도(DB 오류 등) 별도 실패 응답 패킷을
				// 두지 않았다 — 이 경우 그냥 빈 목록(totalCount=0)으로
				// 마무리한다. 클라이언트 입장에서 "방이 하나도 없다"와 "조회
				// 자체가 실패했다"를 굳이 구분하지 않아도 되는 목록형 요청의
				// 관례를 따랐다.
				if( result == ELoginResult::Ok )
				{
					for( const SRoomListEntry& entry : rooms )
					{
						ListRoomsItemResPacket itemRes{};
						itemRes.size = sizeof(itemRes);
						itemRes.type = static_cast<uint16>(EChatPacketType::ListRoomsItemRes);
						itemRes.requestId = requestId;
						itemRes.roomId = entry.roomId;

						const size_t nameCopyLen = (std::min)(entry.name.size(), sizeof(itemRes.name) - 1);
						::memcpy(itemRes.name, entry.name.data(), nameCopyLen);

						const size_t nicknameCopyLen = (std::min)(entry.ownerNickname.size(), sizeof(itemRes.ownerNickname) - 1);
						::memcpy(itemRes.ownerNickname, entry.ownerNickname.data(), nicknameCopyLen);

						itemRes.userCount = server->GetRoomUserCount(entry.roomId);
						itemRes.totalMemberCount = entry.totalMemberCount;

						const size_t imageUrlCopyLen = (std::min)(entry.imageRef.size(), sizeof(itemRes.imageUrl) - 1);
						::memcpy(itemRes.imageUrl, entry.imageRef.data(), imageUrlCopyLen);

						session->Send(&itemRes, sizeof(itemRes));
					}
				}

				ListRoomsEndResPacket endRes{};
				endRes.size = sizeof(endRes);
				endRes.type = static_cast<uint16>(EChatPacketType::ListRoomsEndRes);
				endRes.requestId = requestId;
				endRes.scope = static_cast<uint8_t>(scope);
				endRes.totalCount = (result == ELoginResult::Ok) ? totalCount : 0;
				endRes.page = (result == ELoginResult::Ok) ? resultPage : 0;
				endRes.pageSize = pageSize;
				session->Send(&endRes, sizeof(endRes));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ListRoomsReq, ListRoomsReqPacket, HandleListRoomsReq);