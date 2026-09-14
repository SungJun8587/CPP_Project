
//***************************************************************************
// ChatClientDeleteProfileImageHandler.cpp : DeleteProfileImageRes 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientSession.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

namespace
{
	void HandleDeleteProfileImageRes(CChatClientSession& session, const PacketHeader* header)
	{
		const DeleteProfileImageResPacket* packet = reinterpret_cast<const DeleteProfileImageResPacket*>(header);

		if( CChatClientMain* client = session.GetClient() )
			client->OnDeleteProfileImageResult(packet->success != 0, static_cast<ELoginResult>(packet->reason));
	}
}

REGISTER_CHAT_CLIENT_PACKET_HANDLER(DeleteProfileImageRes, DeleteProfileImageResPacket, HandleDeleteProfileImageRes);