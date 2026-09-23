
//***************************************************************************
// ChatServerMainRoom.cpp : CChatServerMain 구현 — 방(로비 포함) 관련
//
// [분리] ChatServerMain.h 상단의 "구현 파일 분리" 설명 참고. 여기엔 방
// 이동(로비 포함)/생성/삭제/이름변경/이미지 설정/방장 자동 이양/서버
// 시작 시 DB 초기 로딩이 모여있다.
//***************************************************************************

#include "pch.h"
#include <DB/DBAsyncPushHelper.h>
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "DbServiceManager.h"
#include "DBCreateRoomRequest.h"
#include "DBDeleteRoomRequest.h"
#include "DBRenameRoomRequest.h"
#include "DBListRoomsRequest.h"
#include "DBTransferRoomOwnerRequest.h"
#include "DBSetRoomImageRequest.h"
#include "DBGetRoomInfoRequest.h"

#include <algorithm>

//***************************************************************************
// @brief 세션을 지정한 위치(로비 또는 특정 룸)로 옮깁니다.
//***************************************************************************
void CChatServerMain::MoveToRoom(std::shared_ptr<CChatSession> session, int32 newRoomId, int32& outNewRoomUserCount)
{
	outNewRoomUserCount = 0;
	if( session == nullptr )
		return;

	int32 oldRoomId = -1;
	int32 oldRoomRemainingCount = 0;
	bool hadOldRoom = false;
	int32 newRoomCountAfter = 0;
	// [추가] 방장 이양 판단(HandleRoomOwnershipOnLeave)에 쓸 스냅샷 — 이
	// 함수는 _roomMutex를 해제한 뒤에 호출해야 하므로(그 안에서
	// BroadcastToRoom()/MoveToRoom() 재귀 호출이 다시 _roomMutex를 잠금),
	// 락이 걸려있는 동안 필요한 정보만 미리 복사해둔다.
	std::vector<std::shared_ptr<CChatSession>> oldRoomRemainingMembers;
	const std::array<BYTE, kPublicIdBytes> leavingPublicId = session->GetPublicId();

	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		oldRoomId = session->GetRoomId();

		// 기존 방/로비에서 제거 (oldRoomId == -1이면 "아직 어디에도 배정된
		// 적 없음"이라 제거할 대상 자체가 없다 — 로그인 직후 최초 배정 시나리오)
		if( oldRoomId >= 0 )
		{
			auto oldIt = _roomMembers.find(oldRoomId);
			if( oldIt != _roomMembers.end() )
			{
				auto& members = oldIt->second;
				members.erase(std::remove_if(members.begin(), members.end(),
					[&session](const std::weak_ptr<CChatSession>& w)
					{
						auto s = w.lock();
						return !s || s == session;
					}), members.end());

				oldRoomRemainingCount = static_cast<int32>(members.size());
				hadOldRoom = true;

				oldRoomRemainingMembers.reserve(members.size());
				for( auto& w : members )
				{
					if( auto s = w.lock() )
						oldRoomRemainingMembers.push_back(s);
				}
			}
		}

		// 새 위치에 추가
		session->SetRoomId(newRoomId);
		auto& newMembers = _roomMembers[newRoomId];
		newMembers.push_back(session);

		// 죽은 weak_ptr 청소 겸 실제 인원수 계산
		int32 count = 0;
		for( auto it = newMembers.begin(); it != newMembers.end(); )
		{
			if( it->lock() )
			{
				++count;
				++it;
			}
			else
			{
				it = newMembers.erase(it);
			}
		}
		newRoomCountAfter = count;
	}

	outNewRoomUserCount = newRoomCountAfter;

	// 인원수가 바뀐 두 위치(원래 있던 곳/새로 들어간 곳)에 갱신된 인원수를
	// 알린다 — 이미 그 방에 있던 다른 사람들의 화면도 실시간으로 갱신되게.
	if( hadOldRoom && oldRoomId != newRoomId )
	{
		NotifyRoomUserCount(oldRoomId, oldRoomRemainingCount);

		// [추가] 방금 나간 사람이 그 방의 방장이었는지 확인하고, 맞으면
		// 이양/삭제를 처리한다 — 로비(kLobbyRoomId)나 레지스트리에 없는
		// 위치면 이 함수가 즉시 반환하므로 항상 호출해도 안전하다.
		HandleRoomOwnershipOnLeave(oldRoomId, leavingPublicId, oldRoomRemainingMembers);
	}
	NotifyRoomUserCount(newRoomId, newRoomCountAfter);
}

//***************************************************************************
// @brief 세션이 지금 있는 방(로비 포함)에서만 빠집니다. 연결 종료 전용.
//***************************************************************************
void CChatServerMain::LeaveCurrentRoom(CChatSession* session)
{
	if( session == nullptr )
		return;

	int32 roomId = -1;
	int32 remainingCount = 0;
	bool hadRoom = false;

	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		roomId = session->GetRoomId();
		if( roomId < 0 )
			return; // 로그인 직후 방 배정 전에 끊긴 극히 드문 경우 — 정리할 게 없음

		auto it = _roomMembers.find(roomId);
		if( it == _roomMembers.end() )
			return;

		auto& members = it->second;
		members.erase(std::remove_if(members.begin(), members.end(),
			[session](const std::weak_ptr<CChatSession>& w)
			{
				auto s = w.lock();
				return !s || s.get() == session;
			}), members.end());

		remainingCount = static_cast<int32>(members.size());
		hadRoom = true;
	}

	// [수정 — 버그] 예전엔 여기서도 HandleRoomOwnershipOnLeave()를 불러서,
	// 방장이 "방 나가기"/"방 삭제"를 명시적으로 하지 않고 그냥 접속만
	// 끊어도(네트워크 끊김, 앱 재시작 등) 방장이 자동 이양되거나 — 아무도
	// 안 남았으면 방 자체가 DB에서 삭제됐다. 그런데 방 대화 기록을
	// "방이 없어질 때까지 유지"하기로 한 이상, 단순 접속 끊김만으로 방과
	// 그 기록이 통째로 사라지는 건 사용자가 기대하는 동작이 아니다 —
	// 방장은 오프라인 상태에서도 여전히 방장이고, 재접속하면 그대로 방을
	// 관리할 수 있어야 한다. 그래서 이 함수(연결 종료 전용)에서는 인원수
	// 알림만 보내고, 방장 이양/방 삭제는 명시적 행동(RoomLeaveReq ->
	// MoveToRoom(), DeleteRoomReq -> RequestDeleteRoom())에서만 일어나게
	// 한다 — MoveToRoom()은 여전히 HandleRoomOwnershipOnLeave()를 호출한다.
	if( hadRoom )
		NotifyRoomUserCount(roomId, remainingCount);
}

//***************************************************************************
// @brief 방(로비 포함)의 현재 인원수를 조회합니다.
//***************************************************************************
int32 CChatServerMain::GetRoomUserCount(int32 roomId) const
{
	std::lock_guard<std::mutex> lock(_roomMutex);

	auto it = _roomMembers.find(roomId);
	if( it == _roomMembers.end() )
		return 0;

	int32 count = 0;
	for( auto& w : it->second )
	{
		if( !w.expired() )
			++count;
	}
	return count;
}

//***************************************************************************
// @brief 지정한 방(로비 포함)에 있는 세션들에게만 브로드캐스트합니다.
//***************************************************************************
void CChatServerMain::BroadcastToRoom(int32 roomId, const void* data, uint16 size)
{
	if( data == nullptr || size == 0 )
		return;

	std::vector<std::shared_ptr<CChatSession>> targets;
	{
		std::lock_guard<std::mutex> lock(_roomMutex);

		auto it = _roomMembers.find(roomId);
		if( it == _roomMembers.end() )
			return;

		targets.reserve(it->second.size());
		for( auto& w : it->second )
		{
			if( auto s = w.lock() )
				targets.push_back(s);
		}
	}

	for( auto& s : targets )
	{
		if( s->IsConnected() )
			s->Send(data, size);
	}
}

//***************************************************************************
// @brief roomId의 갱신된 인원수를 그 방의 멤버 전원에게 알립니다.
//***************************************************************************
void CChatServerMain::NotifyRoomUserCount(int32 roomId, int32 userCount)
{
	RoomUserCountNotifyPacket notify{};
	notify.size = sizeof(notify);
	notify.type = static_cast<uint16>(EChatPacketType::RoomUserCountNotify);
	notify.roomId = roomId;
	notify.userCount = userCount;

	BroadcastToRoom(roomId, &notify, notify.size);
}

//***************************************************************************
// @brief 방을 새로 만듭니다. RequestChangeNickname()과 동일한 구조를
//        따르되, 성공 시 인메모리 레지스트리 등록이라는 부수효과가 하나
//        더 있다 — 그 등록은 순수 데이터 작업(세션/IOCP 객체를 안 건드림)
//        이라 DB 워커 스레드에서 바로 해도 안전하다(RequestDeleteProfileImage()의
//        ScheduleFileDeletionIfOwned() 호출과 동일한 선례).
//***************************************************************************
void CChatServerMain::RequestCreateRoom(
	std::shared_ptr<CChatSession> session,
	const std::array<BYTE, kPublicIdBytes>& ownerPublicId,
	const std::string& roomName,
	std::function<void(ERoomResult result, int32 newRoomId)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_CREATE_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_CreateRoom,
		[this, ownerPublicId, roomName, jobQueue, onComplete](ST_CREATE_ROOM_REQ* req)
		{
			::memcpy(req->ownerPublicId, ownerPublicId.data(), ownerPublicId.size());
			req->roomName = roomName;
			req->maxRoomsPerOwner = _maxRoomsPerOwner;

			req->onComplete = [this, ownerPublicId, roomName, jobQueue, onComplete](ERoomResult result, int32 newRoomId)
				{
					if( result == ERoomResult::Ok )
					{
						std::lock_guard<std::mutex> lock(_roomRegistryMutex);
						_roomRegistry[newRoomId] = SRoomInfo{ roomName, ownerPublicId };
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result, newRoomId]()
						{
							if( onComplete )
								onComplete(result, newRoomId);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError, 0);
	}
}

//***************************************************************************
// @brief 방을 삭제합니다.
// @details [설계] 성공하면 그 방에 남아있던 멤버 전원에게
//          DeleteRoomNotifyPacket을 브로드캐스트하고 로비로 옮긴 뒤,
//          레지스트리에서도 제거한다 — 패킷 핸들러가 멤버 목록에 접근할
//          방법이 없어서(그 정보는 이 클래스만 갖고 있음) 여기서 전담한다.
//          이 부수효과들은 DB 워커 스레드에서 실행되지만, CChatSession::Send()가
//          스레드 세이프(헤더 문서 참고)하고 MoveToRoom()/BroadcastToRoom()도
//          자체 락(_roomMutex)으로 보호되므로 안전하다.
//***************************************************************************
void CChatServerMain::RequestDeleteRoom(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_DeleteRoom,
		[this, roomId, requesterPublicId, jobQueue, onComplete](ST_DELETE_ROOM_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());

			req->onComplete = [this, roomId, jobQueue, onComplete](ERoomResult result)
				{
					if( result == ERoomResult::Ok )
					{
						std::vector<std::shared_ptr<CChatSession>> members;
						{
							std::lock_guard<std::mutex> lock(_roomMutex);
							auto it = _roomMembers.find(roomId);
							if( it != _roomMembers.end() )
							{
								members.reserve(it->second.size());
								for( auto& w : it->second )
									if( auto s = w.lock() )
										members.push_back(s);
							}
						}

						DeleteRoomNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::DeleteRoomNotify);
						notify.roomId = roomId;
						BroadcastToRoom(roomId, &notify, notify.size);

						// 남아있던 멤버 전원을 로비로 이동. MoveToRoom() 내부가
						// 이 방(oldRoomId==roomId)의 방장 이양 로직도 같이 타게
						// 되는데, 방 자체가 곧 레지스트리에서 삭제될 것이므로
						// (바로 아래) 그 로직은 "레지스트리에 이미 없음" 경로로
						// 빠져 아무 일도 안 한다 — 순서가 중요하다(레지스트리
						// 삭제를 먼저 하면 이 멤버 이동들이 다른 코드 경로를
						// 타서 예상과 달라질 수 있음).
						for( auto& memberSession : members )
						{
							int32 lobbyCount = 0;
							MoveToRoom(memberSession, kLobbyRoomId, lobbyCount);
						}

						{
							std::lock_guard<std::mutex> lock(_roomRegistryMutex);
							_roomRegistry.erase(roomId);
						}

						// [추가] 방 자체가 삭제됐으니 그 방의 대화 기록도
						// 같이 지운다 — "방이 없어질 때까지 유지"라는 요구를
						// 방이 사라지는 이 시점에 자연스럽게 만족시킨다.
						ClearRoomChatHistory(roomId);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief 방 이름을 바꿉니다. 성공 시 그 방 멤버 전원에게
//        RenameRoomNotifyPacket을 브로드캐스트한다.
//***************************************************************************
void CChatServerMain::RequestRenameRoom(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	const std::string& newName,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_RENAME_ROOM_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_RenameRoom,
		[this, roomId, requesterPublicId, newName, jobQueue, onComplete](ST_RENAME_ROOM_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());
			req->newName = newName;

			req->onComplete = [this, roomId, newName, jobQueue, onComplete](ERoomResult result)
				{
					if( result == ERoomResult::Ok )
					{
						{
							std::lock_guard<std::mutex> lock(_roomRegistryMutex);
							auto it = _roomRegistry.find(roomId);
							if( it != _roomRegistry.end() )
								it->second.name = newName;
						}

						RenameRoomNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::RenameRoomNotify);
						notify.roomId = roomId;
						const size_t nameCopyLen = (std::min)(newName.size(), sizeof(notify.newName) - 1);
						::memcpy(notify.newName, newName.data(), nameCopyLen);
						BroadcastToRoom(roomId, &notify, notify.size);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief 존재하는 모든 방을 조회합니다. RequestListProfileImages()와
//        완전히 동일한 구조(부수효과 없음 — 순수 조회).
//***************************************************************************
void CChatServerMain::RequestListRooms(
	std::shared_ptr<CChatSession> session,
	std::function<void(ELoginResult result, const std::vector<SRoomListEntry>& rooms)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	auto dispatchToJobQueue = [this, jobQueue, onComplete](ELoginResult result, const std::vector<SRoomListEntry>& rooms)
		{
			if( jobQueue == nullptr )
				return;

			// [추가] 항목마다 image_ref가 상대 경로로 저장돼 있으면 완전한
			// URL로 복원한다 — RequestListProfileImages()와 동일한 이유
			// (ToDisplayImageUrl() 참고).
			std::vector<SRoomListEntry> displayRooms = rooms;
			for( SRoomListEntry& entry : displayRooms )
				entry.imageRef = ToDisplayImageUrl(entry.imageRef);

			jobQueue->DoAsync([onComplete, result, displayRooms]()
				{
					if( onComplete )
						onComplete(result, displayRooms);
				});
		};

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_ROOMS_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListRooms,
		[dispatchToJobQueue](ST_LIST_ROOMS_REQ* req)
		{
			req->onComplete = dispatchToJobQueue;
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ELoginResult::DbError, std::vector<SRoomListEntry>());
	}
}

//***************************************************************************
// @brief 방 프로필 이미지를 설정/교체/해제합니다.
// @details [설계] RequestDeleteRoom()과 동일한 구조 — 성공 시 부수효과
//          (이전 파일 삭제 예약 + 방 멤버 브로드캐스트)를 DB 콜백 안에서
//          바로 처리한다(RequestDeleteProfileImage()의
//          ScheduleFileDeletionIfOwned() 호출과 같은 선례).
//***************************************************************************
void CChatServerMain::RequestSetRoomImage(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& requesterPublicId,
	const std::string& newImageUrl,
	std::function<void(ERoomResult result)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	// DB엔 상대경로로 저장 — RequestSetProfileImageUrl()과 동일한 이유
	// (ToStorableImageRef() 참고). 외부 URL이면 그대로 통과된다.
	const std::string storableUrl = ToStorableImageRef(newImageUrl);

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_SET_ROOM_IMAGE_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_SetRoomImage,
		[this, roomId, requesterPublicId, storableUrl, jobQueue, onComplete](ST_SET_ROOM_IMAGE_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->requesterPublicId, requesterPublicId.data(), requesterPublicId.size());
			req->imageRef = storableUrl;

			req->onComplete = [this, roomId, storableUrl, jobQueue, onComplete](ERoomResult result, const std::string& oldImageRef)
				{
					if( result == ERoomResult::Ok )
					{
						// 1) 교체되기 전 이미지가 우리 파일 서버 소유였으면
						// 실제 파일 삭제를 예약한다. oldImageRef는 DB에
						// 저장돼 있던 그대로(상대경로 형태)라, 완전한 URL
						// 형태를 기대하는 ScheduleFileDeletionIfOwned()에
						// 넘기기 전에 먼저 ToDisplayImageUrl()로 복원한다.
						if( !oldImageRef.empty() )
							ScheduleFileDeletionIfOwned(ToDisplayImageUrl(oldImageRef));

						// 2) 그 방 멤버 전원에게 브로드캐스트.
						RoomImageChangedNotifyPacket notify{};
						notify.size = sizeof(notify);
						notify.type = static_cast<uint16>(EChatPacketType::RoomImageChangedNotify);
						notify.roomId = roomId;

						const std::string displayUrl = ToDisplayImageUrl(storableUrl);
						const size_t urlCopyLen = (std::min)(displayUrl.size(), sizeof(notify.imageUrl) - 1);
						::memcpy(notify.imageUrl, displayUrl.data(), urlCopyLen);

						BroadcastToRoom(roomId, &notify, notify.size);
					}

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, result]()
						{
							if( onComplete )
								onComplete(result);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(ERoomResult::DbError);
	}
}

//***************************************************************************
// @brief 방 하나(roomId)의 이름/방장 닉네임/이미지를 조회합니다.
// @details RequestListRooms()와 동일한 구조(부수효과 없음 — 순수 조회)지만
//          방 하나만 대상이라 DB 요청이 다르다. imageUrl은 완전한 URL로
//          복원해서(ToDisplayImageUrl()) 콜백에 넘긴다 — RequestListRooms()와
//          동일한 이유.
//***************************************************************************
void CChatServerMain::RequestGetRoomInfo(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	std::function<void(bool found, const SRoomListEntry& info)> onComplete)
{
	if( session == nullptr )
		return;

	CJobQueueRef jobQueue = _jobQueue;

	const bool pushed = PushDBAsyncRequest<COdbcAsyncSrv, ST_GET_ROOM_INFO_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_GetRoomInfo,
		[this, roomId, jobQueue, onComplete](ST_GET_ROOM_INFO_REQ* req)
		{
			req->roomId = roomId;

			req->onComplete = [this, jobQueue, onComplete](ELoginResult result, bool found, const SRoomListEntry& info)
				{
					SRoomListEntry displayInfo = info;
					if( result == ELoginResult::Ok && found )
						displayInfo.imageRef = ToDisplayImageUrl(info.imageRef);

					const bool actuallyFound = (result == ELoginResult::Ok) && found;

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, actuallyFound, displayInfo]()
						{
							if( onComplete )
								onComplete(actuallyFound, displayInfo);
						});
				};
		},
		kMaxDbQueueCapacity);

	if( !pushed )
	{
		if( onComplete )
			onComplete(false, SRoomListEntry());
	}
}

//***************************************************************************
// @brief roomId가 실제로 존재하는 방인지(로비 포함) 확인합니다.
//***************************************************************************
bool CChatServerMain::RoomExists(int32 roomId) const
{
	if( roomId == kLobbyRoomId )
		return true;

	std::lock_guard<std::mutex> lock(_roomRegistryMutex);
	return _roomRegistry.find(roomId) != _roomRegistry.end();
}

//***************************************************************************
// @brief roomId를 떠난 사람이 그 방의 방장이었는지 확인하고, 맞으면
//        이양(남은 멤버가 있을 때) 또는 삭제(아무도 안 남았을 때)를 한다.
// @details [주의] 호출부(MoveToRoom()/LeaveCurrentRoom())가 이미 _roomMutex를
//          해제한 뒤에 불러야 한다 — 이 함수가 내부적으로
//          BroadcastToRoom()/MoveToRoom()(빈 방 정리 경로는 아니지만, 삭제
//          쪽은 RequestDeleteRoom()의 onComplete가 그걸 하므로 여기서는
//          아님)을 호출하지 않으므로 실제로는 재진입 위험이 없지만,
//          _roomRegistryMutex와 _roomMutex의 락 순서를 항상 "먼저 걸린 락을
//          풀고 다음 락을 건다"로 유지하기 위한 방어적 설계다.
//***************************************************************************
void CChatServerMain::HandleRoomOwnershipOnLeave(int32 roomId, const std::array<BYTE, kPublicIdBytes>& leavingPublicId,
	const std::vector<std::shared_ptr<CChatSession>>& remainingMembers)
{
	std::array<BYTE, kPublicIdBytes> currentOwner{};

	{
		std::lock_guard<std::mutex> lock(_roomRegistryMutex);
		auto it = _roomRegistry.find(roomId);
		if( it == _roomRegistry.end() )
			return; // 로비 등 방장 개념이 없는 위치

		currentOwner = it->second.ownerPublicId;
	}

	if( currentOwner != leavingPublicId )
		return; // 방장이 나간 게 아니면 할 일 없음

	if( remainingMembers.empty() )
	{
		// 아무도 안 남음 — 방 자체를 지운다. 레지스트리는 즉시(동기) 반영해
		// 그 사이 들어오는 RoomEnterReq가 이미 없는 방으로 정확히 처리되게
		// 하고, DB 삭제는 비동기로 뒤따라간다(실패해도 인메모리 관점에서는
		// 이미 없는 방이라 사용자 체감 문제는 없음 — 다만 로그는 남긴다).
		{
			std::lock_guard<std::mutex> lock(_roomRegistryMutex);
			_roomRegistry.erase(roomId);
		}

		// [추가] RequestDeleteRoom()과 동일한 이유 — 방이 없어지는 이
		// 경로(빈 방 자동 삭제)에서도 대화 기록을 같이 지운다.
		ClearRoomChatHistory(roomId);

		PushDBAsyncRequest<COdbcAsyncSrv, ST_DELETE_ROOM_REQ>(
			MEMBER_DB_ASYNC,
			kDbCallIdent_DeleteRoom,
			[roomId, leavingPublicId](ST_DELETE_ROOM_REQ* req)
			{
				req->roomId = roomId;
				::memcpy(req->requesterPublicId, leavingPublicId.data(), leavingPublicId.size());
				req->onComplete = [roomId](ERoomResult result)
					{
						if( result != ERoomResult::Ok )
							LOG_ERROR(_T("HandleRoomOwnershipOnLeave: 빈 방(roomId=%d) 자동 삭제 DB 반영 실패(reason=%d) — 인메모리에서는 이미 삭제됨"),
								roomId, static_cast<int32>(result));
					};
			},
			kMaxDbQueueCapacity);

		return;
	}

	// 가장 오래 있었던 멤버(벡터 맨 앞 — _roomMembers가 erase-remove로
	// 삽입 순서를 유지하므로)에게 이양한다.
	std::shared_ptr<CChatSession> newOwnerSession = remainingMembers.front();
	const std::array<BYTE, kPublicIdBytes> newOwnerPublicId = newOwnerSession->GetPublicId();
	const std::string newOwnerNickname = newOwnerSession->GetNickname();

	{
		std::lock_guard<std::mutex> lock(_roomRegistryMutex);
		auto it = _roomRegistry.find(roomId);
		if( it != _roomRegistry.end() )
			it->second.ownerPublicId = newOwnerPublicId;
	}

	PushDBAsyncRequest<COdbcAsyncSrv, ST_TRANSFER_ROOM_OWNER_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_TransferRoomOwner,
		[roomId, newOwnerPublicId](ST_TRANSFER_ROOM_OWNER_REQ* req)
		{
			req->roomId = roomId;
			::memcpy(req->newOwnerPublicId, newOwnerPublicId.data(), newOwnerPublicId.size());
			req->onComplete = [roomId](bool success)
				{
					if( !success )
						LOG_ERROR(_T("HandleRoomOwnershipOnLeave: 방장 이양(roomId=%d) DB 반영 실패 — 인메모리는 이미 반영됨"), roomId);
				};
		},
		kMaxDbQueueCapacity);

	RoomOwnerChangedNotifyPacket notify{};
	notify.size = sizeof(notify);
	notify.type = static_cast<uint16>(EChatPacketType::RoomOwnerChangedNotify);
	notify.roomId = roomId;
	const size_t nicknameCopyLen = (std::min)(newOwnerNickname.size(), sizeof(notify.newOwnerNickname) - 1);
	::memcpy(notify.newOwnerNickname, newOwnerNickname.data(), nicknameCopyLen);

	BroadcastToRoom(roomId, &notify, notify.size);
}

//***************************************************************************
// @brief 서버 시작 시 DB에 저장된 모든 방을 인메모리 레지스트리로 읽어들인다.
// @details Crypto::CCryptoUtil::FromHex()(CryptoUtil.h 확인 완료 —
//          ToHex()의 정확한 역방향, 16진 문자열 -> 원본 바이트, 실패 시
//          false 반환)로 DB의 16진 owner_public_id 문자열을 다시 바이트로
//          복원한다. 형식이 깨진 값(있어서는 안 되지만 방어적으로)이면
//          그 방 하나만 건너뛰고 나머지는 계속 로딩한다.
//***************************************************************************
void CChatServerMain::LoadRoomRegistryFromDb()
{
	PushDBAsyncRequest<COdbcAsyncSrv, ST_LIST_ROOMS_REQ>(
		MEMBER_DB_ASYNC,
		kDbCallIdent_ListRooms,
		[this](ST_LIST_ROOMS_REQ* req)
		{
			req->onComplete = [this](ELoginResult result, const std::vector<SRoomListEntry>& rooms)
				{
					if( result != ELoginResult::Ok )
					{
						LOG_ERROR(_T("LoadRoomRegistryFromDb: 방 목록 초기 로딩 실패"));
						return;
					}

					std::lock_guard<std::mutex> lock(_roomRegistryMutex);
					int32 loadedCount = 0;
					for( const SRoomListEntry& entry : rooms )
					{
						std::array<BYTE, kPublicIdBytes> ownerPublicId{};
						if( !Crypto::CCryptoUtil::FromHex(entry.ownerPublicId, ownerPublicId.data(), ownerPublicId.size()) )
						{
							LOG_ERROR(_T("LoadRoomRegistryFromDb: roomId=%d의 owner_public_id 16진 디코딩 실패 — 이 방은 건너뜀"), entry.roomId);
							continue;
						}

						_roomRegistry[entry.roomId] = SRoomInfo{ entry.name, ownerPublicId };
						++loadedCount;
					}

					LOG_INFO(_T("LoadRoomRegistryFromDb: 방 %d개 로딩 완료"), loadedCount);
				};
		},
		kMaxDbQueueCapacity);
}