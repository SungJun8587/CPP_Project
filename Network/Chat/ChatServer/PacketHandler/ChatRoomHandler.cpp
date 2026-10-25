
//***************************************************************************
// ChatRoomHandler.cpp : 방 기능 패킷 핸들러 — 생성/삭제/이름 변경/이미지/목록/입장/퇴장/멤버 목록
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"
#include <cstring>
#include <algorithm>

namespace
{
	//***************************************************************************
	// @brief 방 생성 요청 처리. 로그인 상태여야 한다. 이름 형식이 잘못됐으면
	//        DB까지 안 가고 즉시 InvalidName으로 응답한다.
	//***************************************************************************
	void HandleCreateRoomReq(CChatSession& session, const PacketHeader* header)
	{
		const CreateRoomReqPacket* packet = reinterpret_cast<const CreateRoomReqPacket*>(header);

		// 고정폭 char 배열이 NUL로 안 끝났을 가능성(비정상 패킷)에 대비해
		// 최대 길이 안에서만 읽어 std::string으로 안전하게 변환한다.
		const std::string roomName = FixedBuf::ReadStr(packet->roomName);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		if( !RoomValidation::IsValidRoomName(roomName) )
		{
			CreateRoomResPacket res{};
			InitPacket(res, EChatPacketType::CreateRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			res.roomId = 0;
			SendPacket(session, res);
			return;
		}

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestCreateRoom(sessionRef, session.GetPublicId(), roomName,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ERoomResult result, int32 newRoomId)
				{
					CreateRoomResPacket res{};
					InitPacket(res, EChatPacketType::CreateRoomRes);
					res.success = (result == ERoomResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					res.roomId = newRoomId;
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(CreateRoomReq, CreateRoomReqPacket, HandleCreateRoomReq);

namespace
{
	//***************************************************************************
	// @brief 방 삭제 요청 처리. 성공 시 그 방에 있던 멤버 전원에게
	//        DeleteRoomNotifyPacket을 브로드캐스트하고 로비로 이동시키는
	//        작업은 CChatServerMain::RequestDeleteRoom() 내부에서 전담한다
	//        — 여기서는 요청자에게 보낼 직접 응답만 처리한다.
	//***************************************************************************
	void HandleDeleteRoomReq(CChatSession& session, const PacketHeader* header)
	{
		const DeleteRoomReqPacket* packet = reinterpret_cast<const DeleteRoomReqPacket*>(header);
		const int32 roomId = packet->roomId;

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestDeleteRoom(sessionRef, roomId, session.GetPublicId(),
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ERoomResult result)
				{
					DeleteRoomResPacket res{};
					InitPacket(res, EChatPacketType::DeleteRoomRes);
					res.success = (result == ERoomResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteRoomReq, DeleteRoomReqPacket, HandleDeleteRoomReq);

namespace
{
	//***************************************************************************
	// @brief 방 이름 변경 요청 처리. 성공 시 그 방 멤버 전원에게
	//        RenameRoomNotifyPacket을 브로드캐스트하는 것은
	//        CChatServerMain::RequestRenameRoom() 내부에서 전담한다.
	//***************************************************************************
	void HandleRenameRoomReq(CChatSession& session, const PacketHeader* header)
	{
		const RenameRoomReqPacket* packet = reinterpret_cast<const RenameRoomReqPacket*>(header);
		const int32 roomId = packet->roomId;

		const std::string newName = FixedBuf::ReadStr(packet->newName);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		if( !RoomValidation::IsValidRoomName(newName) )
		{
			RenameRoomResPacket res{};
			InitPacket(res, EChatPacketType::RenameRoomRes);
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidName);
			SendPacket(session, res);
			return;
		}

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestRenameRoom(sessionRef, roomId, session.GetPublicId(), newName,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ERoomResult result)
				{
					RenameRoomResPacket res{};
					InitPacket(res, EChatPacketType::RenameRoomRes);
					res.success = (result == ERoomResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(RenameRoomReq, RenameRoomReqPacket, HandleRenameRoomReq);

namespace
{
	//***************************************************************************
	// @brief 방 프로필 이미지 설정/교체/해제 요청 처리. 성공 시 그 방
	//        멤버 전원에게 RoomImageChangedNotifyPacket을 브로드캐스트하고,
	//        이전 이미지가 우리 파일 서버 소유였으면 실제 파일 삭제를
	//        예약하는 것은 CChatServerMain::RequestSetRoomImage() 내부에서
	//        전담한다 — 여기서는 요청자에게 보낼 직접 응답만 처리한다.
	//***************************************************************************
	void HandleSetRoomImageReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const SetRoomImageReqPacket* packet = reinterpret_cast<const SetRoomImageReqPacket*>(header);
		const int32 roomId = packet->roomId;

		// 고정폭 char 배열이 NUL로 안 끝났을 가능성(비정상 패킷)에 대비해
		// 최대 길이 안에서만 읽어 std::string으로 안전하게 변환한다.
		const size_t urlLen = ::strnlen(packet->imageUrl, sizeof(packet->imageUrl));
		const std::string imageUrl(packet->imageUrl, urlLen);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSetRoomImage(sessionRef, roomId, session.GetPublicId(), imageUrl,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ERoomResult result)
				{
					SetRoomImageResPacket res{};
					InitPacket(res, EChatPacketType::SetRoomImageRes);
					res.success = (result == ERoomResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(SetRoomImageReq, SetRoomImageReqPacket, HandleSetRoomImageReq);

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
		CChatServerMain* server = GetLoggedInServer(session);
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

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestListRooms(sessionRef, scope, session.GetPublicId(), keyword, page, pageSize,
			BindSession(sessionWeak, [server, requestId, scope, pageSize](const std::shared_ptr<CChatSession>& session, ELoginResult result, int32 totalCount, int32 resultPage, const std::vector<SRoomListEntry>& rooms)
				{
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
							InitPacket(itemRes, EChatPacketType::ListRoomsItemRes);
							itemRes.requestId = requestId;
							itemRes.roomId = entry.roomId;

							CopyStrUtf8(itemRes.name, entry.name);

							CopyStrUtf8(itemRes.ownerNickname, entry.ownerNickname);

							itemRes.userCount = server->GetRoomUserCount(entry.roomId);
							itemRes.totalMemberCount = entry.totalMemberCount;

							CopyStrUtf8(itemRes.imageUrl, entry.imageRef);

							SendPacket(session, itemRes);
						}
					}

					ListRoomsEndResPacket endRes{};
					InitPacket(endRes, EChatPacketType::ListRoomsEndRes);
					endRes.requestId = requestId;
					endRes.scope = static_cast<uint8_t>(scope);
					endRes.totalCount = (result == ELoginResult::Ok) ? totalCount : 0;
					endRes.page = (result == ELoginResult::Ok) ? resultPage : 0;
					endRes.pageSize = pageSize;
					SendPacket(session, endRes);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(ListRoomsReq, ListRoomsReqPacket, HandleListRoomsReq);

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

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		if( !session.AllowRoomEnter() )
		{
			RoomEnterResPacket res{};
			InitPacket(res, EChatPacketType::RoomEnterRes);
			res.roomId = roomId;
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::TooFast);
			SendPacket(session, res);
			return;
		}

		if( roomId < 1 || !server->RoomExists(roomId) )
		{
			RoomEnterResPacket res{};
			InitPacket(res, EChatPacketType::RoomEnterRes);
			res.roomId = roomId;
			res.success = 0;
			res.reason = static_cast<uint8>(ERoomResult::InvalidRoomId);
			res.roomUserCount = 0;
			SendPacket(session, res);
			return;
		}

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		int32 newUserCount = 0;
		server->MoveToRoom(sessionRef, roomId, newUserCount);

		// 방에 입장했다는 사실을 남긴다 — "내가 참여한 방" 목록의 근거가 된다.
		// 로비 입장은 기록하지 않고(함수 안에서 걸러짐), 결과를 기다리지도 않는다.
		server->RequestRecordRoomJoin(roomId, session.GetPublicId());

		// 로비는 이름/방장/이미지 개념이 없으니 조회 없이 바로 응답한다.
		if( roomId == kLobbyRoomId )
		{
			RoomEnterResPacket res{};
			InitPacket(res, EChatPacketType::RoomEnterRes);
			res.roomId = roomId;
			res.success = 1;
			res.reason = static_cast<uint8>(ERoomResult::Ok);
			res.roomUserCount = newUserCount;
			SendPacket(session, res);
			return;
		}

		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestGetRoomInfo(sessionRef, roomId,
			BindSession(sessionWeak, [server, sessionWeak, roomId, newUserCount](const std::shared_ptr<CChatSession>& session, bool found, const SRoomListEntry& info)
				{
					RoomEnterResPacket res{};
					InitPacket(res, EChatPacketType::RoomEnterRes);
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
						CopyStrUtf8(res.roomName, info.name);

						CopyStrUtf8(res.roomOwnerNickname, info.ownerNickname);

						CopyStrUtf8(res.roomImageUrl, info.imageRef);
					}

					SendPacket(session, res);

					// [추가] 입장에 성공했으니(여기 도달한 시점엔 이미 확정) 그
					// 방의 최근 대화 기록을 이 세션에게만(브로드캐스트 아님)
					// 자동으로 스트리밍해준다 — 카카오톡/디스코드처럼 방에
					// 들어가자마자 과거 대화가 보이게.
					server->RequestRoomChatHistory(session, roomId,
						BindSession(sessionWeak, [roomId](const std::shared_ptr<CChatSession>& session, const std::vector<CChatServerMain::SChatHistoryEntry>& history)
							{
								int32 sentCount = 0;
								for( const CChatServerMain::SChatHistoryEntry& entry : history )
								{
									// [추가] 내가 차단한 사용자의 과거 메시지는 보내지 않는다.
									if( session->IsBlockingHex(entry.senderPublicId) )
										continue;

									ChatHistoryItemResPacket itemRes{};
									InitPacket(itemRes, EChatPacketType::ChatHistoryItemRes);
									itemRes.roomId = roomId;
									itemRes.messageId = entry.messageId;
									itemRes.timestampMs = entry.timestampMs;

									CopyStrUtf8(itemRes.nickname, entry.nickname);

									CopyStrUtf8(itemRes.profileImageUrl, entry.profileImageUrl);

									CopyStrUtf8(itemRes.message, entry.message);

									SendPacket(session, itemRes);
									++sentCount;
								}

								ChatHistoryEndResPacket endRes{};
								InitPacket(endRes, EChatPacketType::ChatHistoryEndRes);
								endRes.roomId = roomId;
								endRes.totalCount = sentCount;	// 차단으로 건너뛴 항목을 뺀, 실제로 보낸 개수
								SendPacket(session, endRes);
							}));
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomEnterReq, RoomEnterReqPacket, HandleRoomEnterReq);

namespace
{
	//***************************************************************************
	// @brief 방 퇴장 요청 처리 — 지금 있는 방(로비 제외)에서 로비로 돌아간다.
	// @details 이미 로비에 있는 상태에서 이 요청이 와도 안전하다 —
	//          MoveToRoom(session, kLobbyRoomId, ...)이 "로비 -> 로비" 이동이
	//          되어 사실상 아무 일도 안 하는 것과 동일한 결과를 낸다(oldRoomId
	//          == newRoomId라 인원수 알림도 한 번만 나감). success는 항상 1로
	//          보낸다 — 로비에 있어도 "요청 자체는 처리됨"이라는 의미.
	//***************************************************************************
	void HandleRoomLeaveReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());

		const int32 oldRoomId = session.GetRoomId();

		int32 lobbyUserCount = 0;
		server->MoveToRoom(sessionRef, kLobbyRoomId, lobbyUserCount);

		RoomLeaveResPacket res{};
		InitPacket(res, EChatPacketType::RoomLeaveRes);
		res.success = 1;
		res.roomId = oldRoomId; // 방금까지 있었던 곳(로비였으면 kLobbyRoomId 그대로)
		res.roomUserCount = (oldRoomId == kLobbyRoomId) ? lobbyUserCount : server->GetRoomUserCount(oldRoomId);
		SendPacket(session, res);
	}
}

REGISTER_CHAT_PACKET_HANDLER(RoomLeaveReq, RoomLeaveReqPacket, HandleRoomLeaveReq);

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
			InitPacket(itemRes, EChatPacketType::RoomMemberListItemRes);
			itemRes.requestId = requestId;
			CopyPublicId(itemRes.publicId, member.publicId);

			CopyStrUtf8(itemRes.nickname, member.nickname);

			CopyStrUtf8(itemRes.profileImageUrl, member.profileImageUrl);

			itemRes.online = member.online ? 1 : 0;

			SendPacket(session, itemRes);
		}

		RoomMemberListEndResPacket endRes{};
		InitPacket(endRes, EChatPacketType::RoomMemberListEndRes);
		endRes.requestId = requestId;
		endRes.roomId = roomId;
		endRes.scope = static_cast<uint8_t>(scope);
		endRes.totalCount = totalCount;
		endRes.page = page;
		endRes.pageSize = pageSize;
		SendPacket(session, endRes);
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
			BindSession(sessionWeak, [requestId, roomId, pageSize](const std::shared_ptr<CChatSession>& session, ELoginResult result, int32 totalCount, int32 resultPage, const std::vector<SRoomMemberHistoryInfo>& members)
				{
					if( result == ELoginResult::Ok )
					{
						SendMemberListResult(*session, requestId, roomId, ERoomMemberListScope::History, totalCount, resultPage, pageSize, members);
					}
					else
					{
						SendMemberListResult(*session, requestId, roomId, ERoomMemberListScope::History, 0, 0, pageSize, std::vector<SRoomMemberHistoryInfo>());
					}
				}));
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