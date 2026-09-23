
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
	// @details [수정 — 버그 수정] 로비가 아니면 성공 응답을 곧바로 안 보내고
	//          CChatServerMain::RequestGetRoomInfo()로 그 방의 지금 이름/
	//          방장/이미지를 조회한 뒤, 그 결과를 RoomEnterResPacket에 실어서
	//          응답한다 — 예전엔 응답에 방 정보가 없어서 클라이언트가 별도로
	//          방 목록 캐시(RoomListPanel)를 참조해야 했는데, 로그인 직후
	//          재접속하면서 곧바로 마지막 방에 자동 재입장하는 것처럼 그
	//          캐시가 아직 로딩되기 전에 입장하는 타이밍이면 방 이름/방장/
	//          이미지가 기본값으로 보이는 문제가 있었다. 이제 이 응답 하나로
	//          방 정보까지 전부 받으므로 그런 타이밍 의존이 없어진다.
	//***************************************************************************
	void HandleRoomEnterReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시 — 다른 핸들러들과 동일한 정책

		const RoomEnterReqPacket* packet = reinterpret_cast<const RoomEnterReqPacket*>(header);
		const int32 roomId = packet->roomId;

		// [추가 — 진단용] 같은 세션에서 RoomEnterReq가 중복으로 오는지
		// 서버 쪽에서도 교차 확인 — 클라이언트가 정말 두 번 보내는 건지,
		// 아니면 클라이언트는 한 번만 보냈는데 응답 처리 쪽에서 문제가
		// 나는 건지 구분하기 위함.
		LOG_INFO(_T("HandleRoomEnterReq: roomId=%d 요청 수신"), roomId);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		if( roomId < 1 || !server->RoomExists(roomId) )
		{
			RoomEnterResPacket res{};
			res.size = sizeof(res);
			res.type = static_cast<uint16>(EChatPacketType::RoomEnterRes);
			res.roomId = roomId;
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidRoomId);
			res.roomUserCount = 0;
			session.Send(&res, sizeof(res));
			return;
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		int32 newUserCount = 0;
		server->MoveToRoom(sessionRef, roomId, newUserCount);

		// 로비는 이름/방장/이미지 개념이 없으니 조회 없이 바로 응답한다.
		if( roomId == kLobbyRoomId )
		{
			RoomEnterResPacket res{};
			res.size = sizeof(res);
			res.type = static_cast<uint16>(EChatPacketType::RoomEnterRes);
			res.roomId = roomId;
			res.success = 1;
			res.reason = static_cast<uint8>(ERoomResult::Ok);
			res.roomUserCount = newUserCount;
			session.Send(&res, sizeof(res));
			return;
		}

		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestGetRoomInfo(sessionRef, roomId,
			[server, sessionWeak, roomId, newUserCount](bool found, const SRoomListEntry& info)
			{
				auto session = sessionWeak.lock();
				if( session == nullptr )
					return;

				RoomEnterResPacket res{};
				res.size = sizeof(res);
				res.type = static_cast<uint16>(EChatPacketType::RoomEnterRes);
				res.roomId = roomId;
				res.success = 1;
				res.reason = static_cast<uint8>(ERoomResult::Ok);
				res.roomUserCount = newUserCount;

				// found==false는 입장 직후 그 방이 삭제된 것 같은 극히 좁은
				// 경합뿐이다 — 그 경우 roomName 등은 {} 초기화 덕분에 그냥
				// 빈 문자열로 남는다(클라이언트는 이미 방 삭제 알림도 곧
				// 받을 것이므로 문제없음).
				if( found )
				{
					const size_t nameCopyLen = (std::min)(info.name.size(), sizeof(res.roomName) - 1);
					::memcpy(res.roomName, info.name.data(), nameCopyLen);

					const size_t nicknameCopyLen = (std::min)(info.ownerNickname.size(), sizeof(res.roomOwnerNickname) - 1);
					::memcpy(res.roomOwnerNickname, info.ownerNickname.data(), nicknameCopyLen);

					const size_t urlCopyLen = (std::min)(info.imageRef.size(), sizeof(res.roomImageUrl) - 1);
					::memcpy(res.roomImageUrl, info.imageRef.data(), urlCopyLen);
				}

				session->Send(&res, sizeof(res));

				// [추가] 입장에 성공했으니(여기 도달한 시점엔 이미 확정) 그
				// 방의 최근 대화 기록을 이 세션에게만(브로드캐스트 아님)
				// 자동으로 스트리밍해준다 — 카카오톡/디스코드처럼 방에
				// 들어가자마자 과거 대화가 보이게.
				server->RequestRoomChatHistory(session, roomId,
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
							itemRes.roomId = roomId;
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
			});
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomEnterReq, RoomEnterReqPacket, HandleRoomEnterReq);