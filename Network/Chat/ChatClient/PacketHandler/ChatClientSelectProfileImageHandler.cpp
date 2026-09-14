
//***************************************************************************
// ChatClientSelectProfileImageHandler.cpp : SelectProfileImageRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

namespace
{
	void HandleSelectProfileImageRes(CChatClientSession& session, const PacketHeader* header)
	{
		const SelectProfileImageResPacket* packet = reinterpret_cast<const SelectProfileImageResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnSelectProfileImageResult(packet->success != 0, static_cast<ELoginResult>(packet->reason));
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(SelectProfileImageRes, SelectProfileImageResPacket, HandleSelectProfileImageRes);