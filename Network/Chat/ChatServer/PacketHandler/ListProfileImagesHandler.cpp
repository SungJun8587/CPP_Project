
//***************************************************************************
// ListProfileImagesHandler.cpp : ListProfileImagesReq 패킷 핸들러 (자체 등록)
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
	// @brief 갤러리 목록 조회. 개수가 가변이라 항목마다 ListProfileImagesItemRes를
	//        하나씩 순차 전송하고, 마지막에 ListProfileImagesEndRes로 마무리한다.
	//***************************************************************************
	void HandleListProfileImagesReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestListProfileImages(sessionRef, publicId,
			[sessionWeak](ELoginResult result, const std::vector<SProfileImageEntry>& images)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return; // 응답이 오기 전에 연결이 끊김

				// [참고] result가 DbError여도 그냥 "항목 0개"로 응답한다 —
				// 별도 실패 응답 타입을 안 만든 단순화. 클라이언트 입장에서
				// "갤러리가 비어 있다"와 "조회에 실패했다"를 구분 못 하는 게
				// 이상적이진 않지만, 데모 범위에서 감수했다.
				if( result == ELoginResult::Ok )
				{
					for( const auto& entry : images )
					{
						ListProfileImagesItemResPacket itemRes{};
						InitPacket(itemRes, EChatPacketType::ListProfileImagesItemRes);
						itemRes.imageId = entry.imageId;
						itemRes.isActive = entry.isActive ? 1 : 0;

						FixedBuf::CopyStr(itemRes.imageRef, entry.imageRef);

						session->Send(&itemRes, sizeof(itemRes));
					}
				}

				ListProfileImagesEndResPacket endRes{};
				InitPacket(endRes, EChatPacketType::ListProfileImagesEndRes);
				endRes.totalCount = (result == ELoginResult::Ok) ? static_cast<int32>(images.size()) : 0;
				session->Send(&endRes, sizeof(endRes));
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(ListProfileImagesReq, ListProfileImagesReqPacket, HandleListProfileImagesReq);