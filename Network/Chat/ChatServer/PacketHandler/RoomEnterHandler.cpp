
//***************************************************************************
// RoomEnterHandler.cpp : RoomEnterReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"

namespace
{
	//***************************************************************************
	// @brief 방 입장 요청 처리. 로그인 상태여야 하고, roomId가 실제로
	//        존재하는(생성된) 방이어야 한다(로비로는 이 요청으로 못
	//        들어간다 — 그건 RoomLeaveReq). [수정] 예전엔 "1~kMaxRoomId
	//        범위 안"이면 무조건 유효한 방으로 봤다(고정 슬롯 방식) — 이제
	//        방이 동적으로 생성/삭제되므로, CChatServerMain::RoomExists()로
	//        실제 존재 여부를 확인한다.
	//***************************************************************************
	void HandleRoomEnterReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시 — 다른 핸들러들과 동일한 정책

		const RoomEnterReqPacket* packet = reinterpret_cast<const RoomEnterReqPacket*>(header);
		const int32 roomId = packet->roomId;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		RoomEnterResPacket res{};
		res.size = sizeof(res);
		res.type = static_cast<uint16>(EChatPacketType::RoomEnterRes);
		res.roomId = roomId;

		if( roomId < 1 || !server->RoomExists(roomId) )
		{
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidRoomId);
			res.roomUserCount = 0;
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		int32 newUserCount = 0;
		server->MoveToRoom(sessionRef, roomId, newUserCount);

		res.success = 1;
		res.reason = static_cast<uint8>(ERoomResult::Ok);
		res.roomUserCount = newUserCount;
		session.Send(&res, sizeof(res));

		// [추가] 입장에 성공했고 로비가 아니면, 그 방의 최근 대화 기록을
		// 이 세션에게만(브로드캐스트 아님) 자동으로 스트리밍해준다 —
		// 카카오톡/디스코드처럼 방에 들어가자마자 과거 대화가 보이게.
		// RequestRoomChatHistory()가 로비면 내부에서 즉시 빈 목록으로
		// 완료하므로, 여기서 굳이 kLobbyRoomId를 먼저 걸러낼 필요는
		// 없지만 완료 콜백 등록 자체를 아예 안 하도록 미리 걸러둔다.
		if( roomId != kLobbyRoomId )
		{
			std::weak_ptr<CChatSession> sessionWeak = sessionRef;

			server->RequestRoomChatHistory(sessionRef, roomId,
				[sessionWeak, roomId](const std::vector<CChatServerMain::SChatHistoryEntry>& history)
				{
					auto session = sessionWeak.lock();
					if( session == nullptr )
						return;

					for( const CChatServerMain::SChatHistoryEntry& entry : history )
					{
						ChatHistoryItemResPacket itemRes{};
						itemRes.size = sizeof(itemRes);
						itemRes.type = static_cast<uint16>(EChatPacketType::ChatHistoryItemRes);
						itemRes.messageId = entry.messageId;
						itemRes.timestampMs = entry.timestampMs;

						const size_t nicknameCopyLen = (std::min)(entry.nickname.size(), sizeof(itemRes.nickname) - 1);
						::memcpy(itemRes.nickname, entry.nickname.data(), nicknameCopyLen);

						const size_t urlCopyLen = (std::min)(entry.profileImageUrl.size(), sizeof(itemRes.profileImageUrl) - 1);
						::memcpy(itemRes.profileImageUrl, entry.profileImageUrl.data(), urlCopyLen);

						const size_t messageCopyLen = (std::min)(entry.message.size(), sizeof(itemRes.message) - 1);
						::memcpy(itemRes.message, entry.message.data(), messageCopyLen);

						session->Send(&itemRes, sizeof(itemRes));
					}

					ChatHistoryEndResPacket endRes{};
					endRes.size = sizeof(endRes);
					endRes.type = static_cast<uint16>(EChatPacketType::ChatHistoryEndRes);
					endRes.roomId = roomId;
					endRes.totalCount = static_cast<int32>(history.size());
					session->Send(&endRes, sizeof(endRes));
				});
		}
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomEnterReq, RoomEnterReqPacket, HandleRoomEnterReq);