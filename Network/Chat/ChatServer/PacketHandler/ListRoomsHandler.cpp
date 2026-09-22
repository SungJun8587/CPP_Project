
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
	// @brief 방 목록 조회 요청 처리. ListProfileImagesHandler.cpp와 동일한
	//        패턴 — 항목마다 ListRoomsItemResPacket을 하나씩 보내고, 끝나면
	//        ListRoomsEndResPacket으로 마무리한다.
	//***************************************************************************
	void HandleListRoomsReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		if( !session.IsLoggedIn() )
			return;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestListRooms(sessionRef,
			[server, sessionWeak](ELoginResult result, const std::vector<SRoomListEntry>& rooms)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				// [설계] 조회 자체가 실패해도(DB 오류 등) 별도 실패 응답
				// 패킷을 두지 않았다 — ListProfileImagesReq와 동일하게,
				// 이 경우 그냥 빈 목록(totalCount=0)으로 마무리한다. 클라
				// 이언트 입장에서 "방이 하나도 없다"와 "조회 자체가 실패
				// 했다"를 굳이 구분하지 않아도 되는 목록형 요청의 관례를
				// 따랐다.
				int32 sentCount = 0;
				if( result == ELoginResult::Ok )
				{
					for( const SRoomListEntry& entry : rooms )
					{
						ListRoomsItemResPacket itemRes{};
						itemRes.size = sizeof(itemRes);
						itemRes.type = static_cast<uint16>(EChatPacketType::ListRoomsItemRes);
						itemRes.roomId = entry.roomId;

						const size_t nameCopyLen = (std::min)(entry.name.size(), sizeof(itemRes.name) - 1);
						::memcpy(itemRes.name, entry.name.data(), nameCopyLen);

						const size_t nicknameCopyLen = (std::min)(entry.ownerNickname.size(), sizeof(itemRes.ownerNickname) - 1);
						::memcpy(itemRes.ownerNickname, entry.ownerNickname.data(), nicknameCopyLen);

						itemRes.userCount = server->GetRoomUserCount(entry.roomId);

						const size_t imageUrlCopyLen = (std::min)(entry.imageRef.size(), sizeof(itemRes.imageUrl) - 1);
						::memcpy(itemRes.imageUrl, entry.imageRef.data(), imageUrlCopyLen);

						session->Send(&itemRes, sizeof(itemRes));
						++sentCount;
					}
				}

				ListRoomsEndResPacket endRes{};
				endRes.size = sizeof(endRes);
				endRes.type = static_cast<uint16>(EChatPacketType::ListRoomsEndRes);
				endRes.totalCount = sentCount;
				session->Send(&endRes, sizeof(endRes));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ListRoomsReq, ListRoomsReqPacket, HandleListRoomsReq);