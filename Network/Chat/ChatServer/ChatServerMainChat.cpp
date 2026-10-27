
//***************************************************************************
// ChatServerMainChat.cpp : CChatServerMain 구현 — 채팅 메시지/대화 기록 관련
//
//***************************************************************************

#include "pch.h"
#include <Redis/RedisResultSet.h>
#include "ChatServerMain.h"
#include "ChatSession.h"

#include <algorithm>
#include <chrono>
#include <thread>

//***************************************************************************
// @brief 새로 브로드캐스트할 채팅 메시지에 부여할 다음 고유 ID를 반환하고,
//        그 발신자/방 정보를 삭제 검증용으로 기억해둔다(kMaxTrackedMessages개
//        초과 시 가장 오래된 항목부터 자동으로 밀어낸다).
//***************************************************************************
int64 CChatServerMain::RegisterOutgoingMessage(const std::array<BYTE, kPublicIdBytes>& senderPublicId, int32 roomId, const std::string& fileRelativePath)
{
	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	// 발급과 큐 삽입을 같은 락 안에서 해서 _messageOwnerOrder가 항상 id 오름차순을 유지하게 한다.
	const int64 messageId = _nextMessageId.fetch_add(1);

	_messageOwners[messageId] = SMessageOwnerRecord{ senderPublicId, roomId, fileRelativePath };
	_messageOwnerOrder.push_back(messageId);

	while( _messageOwnerOrder.size() > kMaxTrackedMessages )
	{
		const int64 oldestId = _messageOwnerOrder.front();
		_messageOwnerOrder.pop_front();
		_messageOwners.erase(oldestId);
	}

	return messageId;
}

//***************************************************************************
// @brief messageId 삭제를 시도한다 — 추적 저장소에 남아있고 발신자가
//        일치할 때만 성공, 성공 시 저장소에서 제거하고 outRoomId를 채운다.
// @details [수정 — 순서 큐 정리] 삭제에 성공한 messageId를 _messageOwners에서만
//          지우고 _messageOwnerOrder(삽입 순서 큐)에 그대로 남겨두면,
//          나중에 RegisterOutgoingMessage()의 밀어내기 루프가 이미 없는
//          ID를 큐에서 뽑아 erase()를 또 호출하는 낭비가 쌓인다.
//          messageId는 단조 증가로 발급되므로 큐는 항상 정렬돼 있고,
//          이분 탐색으로 위치를 찾는다.
//***************************************************************************
EDeleteMessageResult CChatServerMain::TryDeleteMessage(int64 messageId, const std::array<BYTE, kPublicIdBytes>& requesterPublicId, int32& outRoomId, std::string& outFileRelativePath)
{
	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	auto it = _messageOwners.find(messageId);
	if( it == _messageOwners.end() )
		return EDeleteMessageResult::NotFound;

	if( it->second.senderPublicId != requesterPublicId )
		return EDeleteMessageResult::NotOwner;

	outRoomId = it->second.roomId;
	outFileRelativePath = std::move(it->second.fileRelativePath);
	_messageOwners.erase(it);

	auto orderIt = std::lower_bound(_messageOwnerOrder.begin(), _messageOwnerOrder.end(), messageId);
	if( orderIt != _messageOwnerOrder.end() && *orderIt == messageId )
		_messageOwnerOrder.erase(orderIt);

	return EDeleteMessageResult::Ok;
}

//***************************************************************************
// @brief 파일 첨부 메시지의 파일 상대 경로를 발신자 소유일 때만 추출한다.
//***************************************************************************
std::string CChatServerMain::ExtractOwnFileAttachmentPath(const std::array<BYTE, kPublicIdBytes>& senderPublicId, const std::string& message) const
{
	static const std::string kPrefix = "[FILE:";
	if( _fileServerUrl.empty() || message.compare(0, kPrefix.size(), kPrefix) != 0 )
		return std::string();

	const size_t close = message.find(']', kPrefix.size());
	if( close == std::string::npos )
		return std::string();

	const std::string relativePath = ImageRef::ExtractOwnedRelativePath(_fileServerUrl, message.substr(close + 1));

	// 파일 서버 저장소와 같은 경로 규칙을 만족하고(FileServerKeys), owner가 발신자 본인이어야 한다.
	const std::string ownerPrefix = PublicIdToHex(senderPublicId) + "/";
	if( !FileServerKeys::IsValidRelativePath(relativePath) || relativePath.compare(0, ownerPrefix.size(), ownerPrefix) != 0 )
		return std::string();

	return relativePath;
}

namespace
{
	// RecordRoomChatMessage()가 저장하는 필드 구분자 — 이 이름 없는
	// 네임스페이스 안에 둬서 이 파일 안의 RemoveRoomChatMessage()/
	// RequestRoomChatHistory()와만 공유한다. 일반 텍스트/닉네임/URL에
	// 나올 일이 없는 제어문자라 이스케이프가 필요 없다.
	constexpr char kChatHistoryFieldDelim = '\x01';

	// 방당 Redis에 보관하는 최대 대화 기록 수(조회 상한 1000과 같다).
	constexpr int32 kMaxChatHistoryPerRoom = 1000;

	std::string BuildRoomChatOrderKey(int32 roomId) { return "RoomChatOrder:" + std::to_string(roomId); }
	std::string BuildRoomChatMsgKey(int32 roomId) { return "RoomChatMsg:" + std::to_string(roomId); }

	//***************************************************************************
	// @brief Redis 명령을 보내고, 응답이 연결 오류(CRedisClient가 연결 끊김/
	//        전송 실패 등을 알리려고 직접 만들어 콜백에 넘기는 ERedisType::Error
	//        값)이면 지정한 횟수만큼 지연을 두고 재시도한다.
	// @details 커넥션 풀은 끊어진 커넥션을 백그라운드에서 재연결하지만, 끊기는
	//          순간 그 커넥션에 이미 올라타 있던 요청은 재연결을 기다리지 않고
	//          에러로 끝난다. 이런 일시적인 연결 오류를 호출부가 빈 결과로
	//          오인하지 않도록 이 헬퍼가 흡수한다. Redis 서버가 돌려준 진짜
	//          에러 응답이나 정상적인 빈 결과는 재시도 대상이 아니다.
	//          [주의] 재시도는 연결 상태가 일시적으로 나쁜 경우를 위한
	//          안전망이다. 반복적으로 재시도가 발생한다면 원인(연결 끊김인지,
	//          파싱 실패인지)을 에러 문자열(res.strVal)로 구분해서 조사해야
	//          한다.
	// @details 지연은 jobQueue->DoTimer()가 아니라 서버의 유지보수 스레드 지연 큐
	//          (CChatServerMain::ScheduleDelayed)에 예약한 뒤, 기한이 되면
	//          jobQueue->DoAsync()로 되돌아오는 방식으로 건다. DoTimer()는
	//          CJobTimer::Distribute()를 주기적으로 호출하는 루프가 있어야만
	//          예약된 작업이 실행되는데 이 서버는 그 루프를 두지 않기 때문이다.
	// @param redisService 명령을 보낼 서비스
	// @param scheduleDelayed 지연 실행 예약 함수(delayMs, task)
	// @param jobQueue 재시도 결과를 원래 스레드 문맥으로 되돌리는 데 쓸 큐
	//        (nRetryDelayMs > 0일 때만 사용됨)
	// @param args Redis 명령어 및 인자
	// @param nMaxRetry 최대 재시도 횟수(0이면 재시도 없이 한 번만 시도)
	// @param nRetryDelayMs 재시도 사이에 둘 지연(밀리초). 0이면 지연 없이
	//        즉시 재시도.
	// @param onResult 최종 결과 콜백 — 재시도를 다 써도 계속 에러면 그
	//        마지막 에러 값 그대로 전달된다(호출부가 기존과 동일하게
	//        "에러/빈 응답"으로 처리하면 됨).
	//***************************************************************************
	// 재시도 간에 공유되는 불변 상태. 콜백이 이 구조체를 shared_ptr로 붙들 뿐
	// 자기 자신을 가리키지 않으므로 순환 참조가 없고, 마지막 콜백이 끝나면 해제된다.
	struct SRetryState
	{
		CRedisService* redisService = nullptr;
		CJobQueueRef jobQueue;
		std::function<void(int32, std::function<void()>)> scheduleDelayed;
		CVector<std::string> args;
		int32 nRetryDelayMs = 0;
		std::function<void(const RedisValue&)> onResult;
	};

	void AttemptSend(const std::shared_ptr<SRetryState>& state, int32 nRetryLeft)
	{
		const bool queued = state->redisService->SendCommand(state->args, [state, nRetryLeft](const RedisValue& res)
			{
				if( res.eType == ERedisType::Error && nRetryLeft > 0 )
				{
					LOG_ERROR(_T("SendCommandWithRetry: 일시적 연결 오류로 재시도 예약 (남은재시도=%d, 지연=%dms, err=%hs)"),
						nRetryLeft, state->nRetryDelayMs, res.strVal.c_str());

					if( state->jobQueue && state->scheduleDelayed && state->nRetryDelayMs > 0 )
					{
						// 유지보수 스레드의 지연 큐에 예약하고, 기한이 되면 jobQueue로 복귀해 재시도한다(위 설명 참고).
						state->scheduleDelayed(state->nRetryDelayMs, [state, nRetryLeft]()
							{
								state->jobQueue->DoAsync([state, nRetryLeft]()
									{
										AttemptSend(state, nRetryLeft - 1);
									});
							});
					}
					else
					{
						AttemptSend(state, nRetryLeft - 1);
					}
					return;
				}
				state->onResult(res);
			});

		// 큐 적재 실패(풀 없음/대기 큐 포화) 시 콜백이 오지 않으므로 에러 응답으로 마무리한다.
		if( !queued )
		{
			RedisValue err;
			err.eType = ERedisType::Error;
			err.strVal = "SendCommand rejected";
			state->onResult(err);
		}
	}

	void SendCommandWithRetry(CRedisService* redisService, CJobQueueRef jobQueue, std::function<void(int32, std::function<void()>)> scheduleDelayed, const CVector<std::string>& args, int32 nMaxRetry, int32 nRetryDelayMs, std::function<void(const RedisValue&)> onResult)
	{
		auto state = std::make_shared<SRetryState>();
		state->redisService = redisService;
		state->jobQueue = std::move(jobQueue);
		state->scheduleDelayed = std::move(scheduleDelayed);
		state->args = args;
		state->nRetryDelayMs = nRetryDelayMs;
		state->onResult = std::move(onResult);
		AttemptSend(state, nMaxRetry);
	}
}

//***************************************************************************
// @brief 방 대화 메시지 하나를 Redis에 기록한다.
//***************************************************************************
void CChatServerMain::RecordRoomChatMessage(
	int32 roomId,
	const std::array<BYTE, kPublicIdBytes>& senderPublicId,
	const std::string& nickname,
	const std::string& profileImageUrl,
	const std::string& message,
	int64 messageId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	const std::string senderPublicIdHex = PublicIdToHex(senderPublicId);
	const int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();

	std::string serialized;
	serialized.reserve(senderPublicIdHex.size() + nickname.size() + profileImageUrl.size() + message.size() + 32);
	serialized += senderPublicIdHex;
	serialized += kChatHistoryFieldDelim;
	serialized += nickname;
	serialized += kChatHistoryFieldDelim;
	serialized += profileImageUrl;
	serialized += kChatHistoryFieldDelim;
	serialized += message;
	serialized += kChatHistoryFieldDelim;
	serialized += std::to_string(nowMs);

	const std::string messageIdStr = std::to_string(messageId);

	// 한 번의 EVAL로 (1) ZADD RoomChatOrder:{roomId} {messageId} {messageId} — score와 member 둘 다
	// messageId(문자열): score는 정렬 기준(오름차순 발급이라 시간순과 일치), member는 조회 후 HMGET에
	// 그대로 쓸 키, (2) HSET RoomChatMsg:{roomId} {messageId} {serialized}, (3) 방당 kMaxChatHistoryPerRoom
	// 개를 넘으면 가장 오래된 항목을 ZSET·HASH에서 함께 제거한다. 원자적이고 왕복도 한 번이다.
	static const std::string kRecordScript =
		"redis.call('ZADD', KEYS[1], ARGV[1], ARGV[1])\n"
		"redis.call('HSET', KEYS[2], ARGV[1], ARGV[2])\n"
		"local limit = tonumber(ARGV[3])\n"
		"local n = redis.call('ZCARD', KEYS[1])\n"
		"if n > limit then\n"
		"  local old = redis.call('ZRANGE', KEYS[1], 0, n - limit - 1)\n"
		"  redis.call('ZREM', KEYS[1], unpack(old))\n"
		"  redis.call('HDEL', KEYS[2], unpack(old))\n"
		"end\n"
		"return 1";

	CVector<std::string> evalArgs;
	evalArgs.push_back("EVAL");
	evalArgs.push_back(kRecordScript);
	evalArgs.push_back("2");
	evalArgs.push_back(BuildRoomChatOrderKey(roomId));
	evalArgs.push_back(BuildRoomChatMsgKey(roomId));
	evalArgs.push_back(messageIdStr);
	evalArgs.push_back(serialized);
	evalArgs.push_back(std::to_string(kMaxChatHistoryPerRoom));
	_redisService->SendCommand(evalArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 메시지 하나를 대화 기록에서 제거한다.
//***************************************************************************
void CChatServerMain::RemoveRoomChatMessage(int32 roomId, int64 messageId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	const std::string messageIdStr = std::to_string(messageId);

	CVector<std::string> zremArgs;
	zremArgs.push_back("ZREM");
	zremArgs.push_back(BuildRoomChatOrderKey(roomId));
	zremArgs.push_back(messageIdStr);
	_redisService->SendCommand(zremArgs, [](const RedisValue& /*res*/) {});

	CVector<std::string> hdelArgs;
	hdelArgs.push_back("HDEL");
	hdelArgs.push_back(BuildRoomChatMsgKey(roomId));
	hdelArgs.push_back(messageIdStr);
	_redisService->SendCommand(hdelArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 방과 관련된 Redis 데이터를 전부 지운다(방 삭제 시 호출).
// @details [수정 — 리네이밍] 원래 이름은 ClearRoomChatHistory였다 — 대화
//          기록 두 키(RoomChatOrder/RoomChatMsg)만 지웠는데, 방 목록의
//          totalMemberCount를 위한 입장 이력 집계 Set(RoomMembers)도 방이
//          없어지면 함께 정리해야 해서 이름을 넓혔다. 지우지 않고 두면
//          같은 room_id가 재사용될 일은 없지만(AUTO_INCREMENT), 쓸모없는
//          키가 Redis에 계속 남는다.
//***************************************************************************
void CChatServerMain::ClearRoomRedisData(int32 roomId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	// DEL은 여러 키를 한 번에 받으므로 한 커맨드로 세 키를 같이 지운다.
	CVector<std::string> delArgs;
	delArgs.push_back("DEL");
	delArgs.push_back(BuildRoomChatOrderKey(roomId));
	delArgs.push_back(BuildRoomChatMsgKey(roomId));
	delArgs.push_back(BuildRoomMembersKey(roomId));
	_redisService->SendCommand(delArgs, [](const RedisValue& /*res*/) {});
}

//***************************************************************************
// @brief 방에 입장한 적이 있는 전체 유저 집합을 담는 Redis Set 키를 만든다.
//***************************************************************************
std::string CChatServerMain::BuildRoomMembersKey(int32 roomId) const
{
	return "RoomMembers:" + std::to_string(roomId);
}

//***************************************************************************
// @brief 방의 최근 대화 기록(최대 1000개)을 오래된 순서로 조회한다.
// @details ZREVRANGE로 최신 messageId 1000개(최신순)를 얻은 뒤, 그 목록으로
//          HMGET을 한 번 더 호출해 내용을 한꺼번에 가져온다 — 두 Redis
//          왕복을 콜백 체이닝으로 순차 처리한다. 최종적으로 오래된 순서로
//          뒤집어서 돌려준다(자연스러운 채팅 로그 순서).
//***************************************************************************
void CChatServerMain::RequestRoomChatHistory(
	std::shared_ptr<CChatSession> session,
	int32 roomId,
	std::function<void(const std::vector<SChatHistoryEntry>& history)> onComplete)
{
	if( session == nullptr )
		return;

	if( roomId == kLobbyRoomId || _redisService == nullptr )
	{
		if( onComplete )
			onComplete(std::vector<SChatHistoryEntry>());
		return;
	}

	CJobQueueRef jobQueue = _jobQueue;
	const std::string msgKey = BuildRoomChatMsgKey(roomId);
	const std::string orderKey = BuildRoomChatOrderKey(roomId);

	CVector<std::string> zrevrangeArgs;
	zrevrangeArgs.push_back("ZREVRANGE");
	zrevrangeArgs.push_back(orderKey);
	zrevrangeArgs.push_back("0");
	zrevrangeArgs.push_back("999"); // 최근 1000개(0~999, inclusive)

	// 일시적인 연결 오류는 SendCommandWithRetry()가 흡수한다. 재시도 횟수와
	// 지연은 풀의 재연결 주기(기본 3초)보다 짧게 두어, 방 입장 응답이
	// 최악의 경우에도 약 1초(2회 x 500ms) 안에 끝나도록 했다.
	auto scheduleDelayed = [this](int32 delayMs, std::function<void()> task) { ScheduleDelayed(delayMs, std::move(task)); };
	constexpr int32 kChatHistoryMaxRetry = 2;
	constexpr int32 kChatHistoryRetryDelayMs = 500;

	SendCommandWithRetry(_redisService.get(), jobQueue, scheduleDelayed, zrevrangeArgs, kChatHistoryMaxRetry, kChatHistoryRetryDelayMs, [this, jobQueue, scheduleDelayed, onComplete, msgKey](const RedisValue& orderRes)
		{
			CRedisResultSet orderSet(orderRes);

			if( orderSet.IsEmpty() )
			{
				PostToJobQueue(jobQueue, onComplete, std::vector<SChatHistoryEntry>());
				return;
			}

			std::vector<std::string> messageIds;
			messageIds.reserve(orderSet.GetSize());
			std::string idStr;
			while( orderSet.GetData(idStr) )
				messageIds.push_back(idStr);

			// HMGET RoomChatMsg:{roomId} {id1} {id2} ... — 여러 messageId의
			// 내용을 한 번에 조회한다. 존재하지 않는 필드(그 사이 삭제된
			// 메시지)는 CRedisResultSet이 빈 문자열로 채워준다.
			CVector<std::string> hmgetArgs;
			hmgetArgs.push_back("HMGET");
			hmgetArgs.push_back(msgKey);
			for( const std::string& id : messageIds )
				hmgetArgs.push_back(id);

			SendCommandWithRetry(_redisService.get(), jobQueue, scheduleDelayed, hmgetArgs, kChatHistoryMaxRetry, kChatHistoryRetryDelayMs, [jobQueue, onComplete, messageIds](const RedisValue& hmgetRes)
				{
					CRedisResultSet hmgetSet(hmgetRes);

					std::vector<SChatHistoryEntry> historyNewestFirst;
					historyNewestFirst.reserve(messageIds.size());

					for( size_t i = 0; i < messageIds.size(); ++i )
					{
						std::string serialized;
						if( !hmgetSet.GetData(serialized) || serialized.empty() )
							continue; // 그 사이 삭제된 메시지 — 건너뜀

						// "senderPublicId\x01nickname\x01profileImageUrl\x01message\x01timestampMs" 분해.
						std::vector<std::string> fields;
						size_t start = 0;
						for( size_t pos = 0; pos <= serialized.size(); ++pos )
						{
							if( pos == serialized.size() || serialized[pos] == kChatHistoryFieldDelim )
							{
								fields.push_back(serialized.substr(start, pos - start));
								start = pos + 1;
							}
						}

						if( fields.size() != 5 )
							continue; // 형식이 깨진 값 — 방어적으로 건너뜀

						SChatHistoryEntry entry;
						try
						{
							entry.messageId = std::stoll(messageIds[i]);
							entry.timestampMs = std::stoll(fields[4]);
						}
						catch( const std::exception& )
						{
							continue;
						}
						entry.senderPublicId = fields[0];
						entry.nickname = fields[1];
						entry.profileImageUrl = fields[2];
						entry.message = fields[3];

						historyNewestFirst.push_back(std::move(entry));
					}

					// 최신순으로 받았으니 자연스러운 채팅 로그 순서(오래된
					// 것부터)로 뒤집는다.
					std::vector<SChatHistoryEntry> historyOldestFirst(historyNewestFirst.rbegin(), historyNewestFirst.rend());

					PostToJobQueue(jobQueue, onComplete, historyOldestFirst);
				});
		});
}