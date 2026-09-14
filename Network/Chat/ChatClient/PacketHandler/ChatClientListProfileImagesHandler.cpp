
//***************************************************************************
// ChatClientListProfileImagesHandler.cpp : ListProfileImagesItemRes/
//                                          ListProfileImagesEndRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 갤러리 목록 항목 하나. 가변 개수라 항목마다 이 핸들러가 한 번씩
	//        호출된다 — 개수 자체는 뒤이어 오는 ListProfileImagesEndRes로 통지.
	//***************************************************************************
	void HandleListProfileImagesItemRes(CChatClientSession& session, const PacketHeader* header)
	{
		const ListProfileImagesItemResPacket* packet = reinterpret_cast<const ListProfileImagesItemResPacket*>(header);

		char safeRefBuf[kProfileImageUrlBytes + 1] = {};
		::memcpy(safeRefBuf, packet->imageRef, sizeof(packet->imageRef));
		const std::string imageRef(safeRefBuf);

		if( CChatClientMain* client = session.GetClient() )
			client->OnProfileImageListItem(packet->imageId, imageRef, packet->isActive != 0);
	}

	//***************************************************************************
	// @brief 갤러리 목록 전송 완료(총 개수 통지).
	//***************************************************************************
	void HandleListProfileImagesEndRes(CChatClientSession& session, const PacketHeader* header)
	{
		const ListProfileImagesEndResPacket* packet = reinterpret_cast<const ListProfileImagesEndResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnProfileImageListEnd(packet->totalCount);
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(ListProfileImagesItemRes, ListProfileImagesItemResPacket, HandleListProfileImagesItemRes);
REGISTER_CHAT_CLIENT_PACKET_HANDLER(ListProfileImagesEndRes, ListProfileImagesEndResPacket, HandleListProfileImagesEndRes);