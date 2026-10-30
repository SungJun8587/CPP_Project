
//***************************************************************************
// ChatServerKeys.h : 채팅 서버가 Redis에 쓰는 키 이름 규약 (header-only)
//
// @details
// 키 접두어와 키 조립을 한 곳에서만 정의한다. 접두어는 Lua 스크립트
// (ChatServerMain.cpp의 kLoginScript/kRefreshScript가 쓰는 "User:")에도 같은 값이
// 하드코딩되어 있으므로, 바꿀 때는 그 스크립트도 함께 확인해야 한다.
//   - User:{publicId 16진}      로그인 상태 해시 (TTL, 하트비트 주기로 갱신)
//   - RoomChatOrder:{roomId}    방 대화 기록의 순서(ZSET, score=messageId)
//   - RoomChatMsg:{roomId}      방 대화 기록의 내용(HASH, field=messageId)
//   - RoomMembers:{roomId}      방에 입장한 적이 있는 전체 유저 집합(SET, 원소=publicId 16진).
//                               SADD는 이미 있는 원소를 다시 넣어도 크기가 늘지 않으므로 SCARD가
//                               "서로 다른 유저 수"를 정확히 유지한다. 방 목록의 totalMemberCount만을
//                               위한 빠른 카운터이며, DB의 room_members(입장 시각이 필요한 참여 이력)와는 별개다.
//***************************************************************************

#ifndef CHATSERVERKEYS_H
#define CHATSERVERKEYS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace ChatServerKeys
{
	inline constexpr const char* kUserPrefix = "User:";
	inline constexpr const char* kUserFieldGroupId = "serverGroupId";		// User:{id} 해시의 필드 — 로그인한 서버(채널) 식별
	inline constexpr const char* kUserFieldChannelId = "serverChannelId";
	inline constexpr size_t kUserKeyBatch = 200;	// User:{id} 여러 개를 EVAL 한 번에 처리할 때의 최대 개수(로그인 키 갱신, 수신자 채널 조회)
	inline constexpr const char* kRoomChatOrderPrefix = "RoomChatOrder:";
	inline constexpr const char* kRoomChatMsgPrefix = "RoomChatMsg:";
	inline constexpr const char* kRoomMembersPrefix = "RoomMembers:";

	// 채널 간 전달 계층의 Pub/Sub 토픽(ChatServerMainRelay.cpp). 채널마다 토픽 하나를 구독한다.
	inline constexpr const char* kRelayTopicPrefix = "Chat:";
	inline constexpr const char* kRelayTopicPattern = "Chat:*";		// PUBSUB CHANNELS 패턴 — 접두사와 같은 값

	// 채널 식별 "group:channel" — 로그인 키의 필드 값(조회 스크립트가 같은 형식으로 돌려줌)이자 토픽 이름의 뒷부분.
	inline std::string BuildChannelKey(const std::string& groupId, const std::string& channelId)
	{
		return groupId + ":" + channelId;
	}

	inline std::string BuildRelayTopic(const std::string& channelKey)
	{
		return kRelayTopicPrefix + channelKey;
	}

	inline std::string BuildUserKey(const std::string& publicIdHex)
	{
		return kUserPrefix + publicIdHex;
	}

	inline std::string BuildRoomChatOrderKey(int32_t roomId)
	{
		return kRoomChatOrderPrefix + std::to_string(roomId);
	}

	inline std::string BuildRoomChatMsgKey(int32_t roomId)
	{
		return kRoomChatMsgPrefix + std::to_string(roomId);
	}

	inline std::string BuildRoomMembersKey(int32_t roomId)
	{
		return kRoomMembersPrefix + std::to_string(roomId);
	}
}

#endif // ndef CHATSERVERKEYS_H