
//***************************************************************************
// ChatServerMainChat.cpp : CChatServerMain 구현 — 채팅 메시지/대화 기록 관련
//
// [분리] ChatServerMain.h 상단의 "구현 파일 분리" 설명 참고. 여기엔 채팅
// 메시지 ID 발급/삭제 검증(RegisterOutgoingMessage/TryDeleteMessage —
// 인메모리, 최근 N개만 추적)과 방 대화 기록(Redis 저장/삭제/조회)이
// 모여있다.
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
int64 CChatServerMain::RegisterOutgoingMessage(const std::array<BYTE, kPublicIdBytes>& senderPublicId, int32 roomId)
{
	const int64 messageId = _nextMessageId.fetch_add(1);

	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	_messageOwners[messageId] = SMessageOwnerRecord{ senderPublicId, roomId };
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
//          ID를 큐에서 뽑아 erase()를 또 호출하는 낭비(해는 없지만 불필요한
//          작업)가 쌓인다 — std::deque에서 특정 원소 하나를 지우는 건
//          O(n)이라 매 삭제마다 하기엔 아깝지만, 이 큐는 애초에
//          kMaxTrackedMessages(500)로 크기가 짧게 묶여있어 실질적인
//          비용은 무시할 수준이다.
//***************************************************************************
EDeleteMessageResult CChatServerMain::TryDeleteMessage(int64 messageId, const std::array<BYTE, kPublicIdBytes>& requesterPublicId, int32& outRoomId)
{
	std::lock_guard<std::mutex> lock(_messageOwnerMutex);

	auto it = _messageOwners.find(messageId);
	if( it == _messageOwners.end() )
		return EDeleteMessageResult::NotFound;

	if( it->second.senderPublicId != requesterPublicId )
		return EDeleteMessageResult::NotOwner;

	outRoomId = it->second.roomId;
	_messageOwners.erase(it);

	auto orderIt = std::find(_messageOwnerOrder.begin(), _messageOwnerOrder.end(), messageId);
	if( orderIt != _messageOwnerOrder.end() )
		_messageOwnerOrder.erase(orderIt);

	return EDeleteMessageResult::Ok;
}

namespace
{
	// RecordRoomChatMessage()가 저장하는 필드 구분자 — 이 이름 없는
	// 네임스페이스 안에 둬서 이 파일 안의 RemoveRoomChatMessage()/
	// RequestRoomChatHistory()와만 공유한다. 일반 텍스트/닉네임/URL에
	// 나올 일이 없는 제어문자라 이스케이프가 필요 없다.
	constexpr char kChatHistoryFieldDelim = '\x01';

	std::string BuildRoomChatOrderKey(int32 roomId) { return "RoomChatOrder:" + std::to_string(roomId); }
	std::string BuildRoomChatMsgKey(int32 roomId) { return "RoomChatMsg:" + std::to_string(roomId); }

	//***************************************************************************
	// @brief [추가 — 버그 수정] Redis 명령을 보내고, 응답이 "일시적 연결
	//        오류"(CRedisClient::SendCommand()가 자체적으로 만들어 보내는
	//        에러 값 — 예: "ERR connection closed")면 지정한 횟수만큼
	//        자동으로 재시도한다.
	// @details 진단 결과, 대화 기록이 간헐적으로 안 불러와지던 현상의
	//          실제 원인은 서버 코드 버그가 아니라 "그 순간 HMGET을 맡은
	//          커넥션이 마침 끊겨 있었던" 것이었다(Docker Desktop 환경의
	//          네트워크 계층이 Redis의 idle 타임아웃 설정과 무관하게 조용히
	//          연결을 끊는 것으로 추정). 커넥션 풀 자체는 이미 자동
	//          재연결을 처리하지만, 끊기는 바로 그 순간 이미 그 커넥션에
	//          올라타 있던 요청 하나는 재연결을 기다려주지 않고 그 자리에서
	//          실패로 끝난다.
	// @details [수정 — 버그 수정: DoTimer 미동작] 처음엔 jobQueue->DoTimer()로
	//          지연을 걸었는데, 실제 재현에서 예약 로그("재시도 예약")만
	//          찍히고 그 뒤로 영원히 아무 일도 안 일어나는 게 확인됐다.
	//          CJobQueue::DoTimer()는 CJobTimer::Reserve()로 우선순위 큐에
	//          작업을 넣기만 할 뿐, 그걸 실제로 꺼내서 실행하는
	//          CJobTimer::Distribute()를 어딘가(전용 스레드)에서 주기적으로
	//          불러줘야 한다 — 채팅 서버 쪽에 그 배선이 실제로 돌고 있는지
	//          확인이 안 됐고(gpJobTimer 자체는 BaseGlobal::Init()에서
	//          생성되지만, Distribute() 호출 루프가 없으면 예약된 작업이
	//          큐에 영원히 갇혀있게 된다), 검증 안 된 전역 배선에 재시도
	//          기능 전체가 기대게 만드는 건 위험하다고 판단했다. 대신 짧게
	//          sleep하는 detached 스레드를 하나 띄운 뒤, 그 안에서
	//          jobQueue->DoAsync()로 되돌아오는 방식으로 바꿨다 —
	//          DoAsync()는 이미 이 함수의 정상 흐름(HMGET 응답 처리 등)에서
	//          검증된 경로라 안전하게 재사용할 수 있다. 재시도는 에러가 난
	//          경우에만(드물게) 일어나므로 스레드 하나 띄우는 비용은
	//          무시할 수준이다.
	// @param redisService 명령을 보낼 서비스
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
	void SendCommandWithRetry(CRedisService* redisService, CJobQueueRef jobQueue, const CVector<std::string>& args, int32 nMaxRetry, int32 nRetryDelayMs, std::function<void(const RedisValue&)> onResult)
	{
		auto fnSend = std::make_shared<std::function<void(int32)>>();
		*fnSend = [redisService, jobQueue, args, onResult, fnSend, nRetryDelayMs](int32 nRetryLeft)
			{
				redisService->SendCommand(args, [redisService, jobQueue, args, onResult, nRetryLeft, fnSend, nRetryDelayMs](const RedisValue& res)
					{
						if( res.eType == ERedisType::Error && nRetryLeft > 0 )
						{
							LOG_ERROR(_T("SendCommandWithRetry: 일시적 연결 오류로 재시도 예약 (남은재시도=%d, 지연=%dms, err=%hs)"),
								nRetryLeft, nRetryDelayMs, res.strVal.c_str());

							if( jobQueue && nRetryDelayMs > 0 )
							{
								// [수정] DoTimer() 대신 detached 스레드에서
								// sleep 후 DoAsync()로 복귀 — 위 설명 참고.
								std::thread([jobQueue, fnSend, nRetryLeft, nRetryDelayMs]()
									{
										std::this_thread::sleep_for(std::chrono::milliseconds(nRetryDelayMs));
										jobQueue->DoAsync([fnSend, nRetryLeft]()
											{
												(*fnSend)(nRetryLeft - 1);
											});
									}).detach();
							}
							else
							{
								(*fnSend)(nRetryLeft - 1);
							}
							return;
						}
						onResult(res);
					});
			};
		(*fnSend)(nMaxRetry);
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

	const std::string senderPublicIdHex = Crypto::CCryptoUtil::ToHex(senderPublicId.data(), senderPublicId.size());
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

	// ZADD RoomChatOrder:{roomId} {messageId} {messageId} — score와 member
	// 둘 다 messageId(문자열)로 준다. score는 정렬 기준(오름차순 발급되는
	// int64라 시간순과 정확히 일치), member는 조회 후 HMGET에 그대로 쓸 키.
	CVector<std::string> zaddArgs;
	zaddArgs.push_back("ZADD");
	zaddArgs.push_back(BuildRoomChatOrderKey(roomId));
	zaddArgs.push_back(messageIdStr);
	zaddArgs.push_back(messageIdStr);
	_redisService->SendCommand(zaddArgs, [](const RedisValue& /*res*/) {});

	// HSET RoomChatMsg:{roomId} {messageId} {serialized}
	CVector<std::string> hsetArgs;
	hsetArgs.push_back("HSET");
	hsetArgs.push_back(BuildRoomChatMsgKey(roomId));
	hsetArgs.push_back(messageIdStr);
	hsetArgs.push_back(serialized);
	_redisService->SendCommand(hsetArgs, [](const RedisValue& /*res*/) {});
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
// @brief 방의 대화 기록 전체를 지운다(방 삭제 시 호출).
//***************************************************************************
void CChatServerMain::ClearRoomChatHistory(int32 roomId)
{
	if( roomId == kLobbyRoomId || _redisService == nullptr )
		return;

	// DEL은 여러 키를 한 번에 받으므로 한 커맨드로 두 키를 같이 지운다.
	CVector<std::string> delArgs;
	delArgs.push_back("DEL");
	delArgs.push_back(BuildRoomChatOrderKey(roomId));
	delArgs.push_back(BuildRoomChatMsgKey(roomId));
	_redisService->SendCommand(delArgs, [](const RedisValue& /*res*/) {});
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
	// [추가 — 진단용] 이 함수 자체가 호출되는지부터 확인.
	LOG_INFO(_T("RequestRoomChatHistory: 시작 (roomId=%d, session=%s)"), roomId, session ? _T("유효") : _T("null"));

	if( session == nullptr )
		return;

	if( roomId == kLobbyRoomId || _redisService == nullptr )
	{
		LOG_INFO(_T("RequestRoomChatHistory: 조기 반환 (roomId=%d, _redisService=%s)"), roomId, _redisService ? _T("유효") : _T("null"));
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

	// [추가 — 진단용] ZREVRANGE를 실제로 보내는지 확인.
	LOG_INFO(_T("RequestRoomChatHistory: ZREVRANGE 요청 전송 (key=%hs)"), orderKey.c_str());

	// [수정 — 버그 수정] SendCommand() 대신 SendCommandWithRetry()를 쓴다 —
	// 최대 1회 재시도(원요청 포함 최대 2번 시도)로, 그 순간 이 명령을
	// 맡은 커넥션이 마침 끊겨 있었던(=일시적) 경우를 흡수한다. 위 헬퍼
	// 설명 참고 — 이게 "대화 기록이 간헐적으로 안 불러와지던" 현상의
	// 실제 원인이었다.
	// [수정] 1 -> 2. 재시도 로그를 보니 재시도로 새로 받은 커넥션마저
	// 마침 끊겨있던 경우가 실제로 관측됐다 — Docker Desktop 환경에서
	// 풀의 여러 커넥션이 거의 동시에(네트워크 계층의 일괄 정리 등으로)
	// 끊길 수 있다는 뜻이므로, 한 번 더 여유를 둔다.
	// [수정] 2회/500ms(최대 약 1초 대기)로는 부족했다 — 실제 재현에서
	// 최소 1초 이상 지속되면서 풀의 여러 커넥션을 한꺼번에 덮치는
	// "일괄 장애 구간"이 있었다(Docker Desktop 환경의 네트워크 계층이
	// 주기적으로 전체 경로를 흔드는 것으로 추정 — 개별 커넥션의 idle
	// 끊김과는 다른 패턴). 4회/800ms로 늘려 최대 약 3.2초까지 버틴다.
	// 그래도 남는 근본 문제(왜 이런 일괄 장애가 나는지)는 코드 쪽
	// 재시도만으로 완전히 없앨 수 없다 — Docker Desktop/WSL2 네트워크
	// 계층 자체를 봐야 한다.
	constexpr int32 kChatHistoryMaxRetry = 4;

	// [추가] 재시도 사이의 지연 — CRedisConnectionPool::ReconnectLoop()의
	// 스윕 주기(기본 3초)보다 짧지만, 즉시 재시도보다는 재연결이 끝날
	// 시간을 벌어주는 정도로 잡았다. 방 입장 응답 자체가 최악의 경우
	// 이 지연×재시도 횟수만큼(지금 설정으로 최대 3.2초) 늦어질 수 있음을
	// 감안한 값이다.
	constexpr int32 kChatHistoryRetryDelayMs = 800;

	SendCommandWithRetry(_redisService.get(), jobQueue, zrevrangeArgs, kChatHistoryMaxRetry, kChatHistoryRetryDelayMs, [this, jobQueue, onComplete, msgKey, roomId](const RedisValue& orderRes)
		{
			CRedisResultSet orderSet(orderRes);

			// [추가 — 진단용] ZREVRANGE 응답이 실제로 왔는지, 몇 개인지 확인.
			LOG_INFO(_T("RequestRoomChatHistory: ZREVRANGE 응답 수신 (roomId=%d, 빈응답=%s, size=%d)"),
				roomId, orderSet.IsEmpty() ? _T("true") : _T("false"), static_cast<int>(orderSet.GetSize()));

			if( orderSet.IsEmpty() )
			{
				if( jobQueue == nullptr )
					return;
				jobQueue->DoAsync([onComplete]()
					{
						if( onComplete )
							onComplete(std::vector<SChatHistoryEntry>());
					});
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

			// [추가 — 진단용] HMGET을 실제로 보내는지 확인.
			LOG_INFO(_T("RequestRoomChatHistory: HMGET 요청 전송 (roomId=%d, 필드수=%d)"), roomId, static_cast<int>(messageIds.size()));

			SendCommandWithRetry(_redisService.get(), jobQueue, hmgetArgs, kChatHistoryMaxRetry, kChatHistoryRetryDelayMs, [jobQueue, onComplete, messageIds, roomId](const RedisValue& hmgetRes)
				{
					// [추가 — 진단용] HMGET 응답이 실제로 왔는지, 그리고 어떤
					// 타입인지 확인 — 지금까지는 "응답 수신" 로그만 있어서
					// 그게 진짜 빈 배열(Array, 원소 0개 또는 전부 null)인지,
					// 아니면 에러 값(예: 커넥션 문제로 인한 "ERR connection
					// closed")인지 구분이 안 됐다. CRedisResultSet이 에러
					// 타입을 조용히 "빈 결과"로 처리해버릴 가능성을 배제하기
					// 위해 원본 타입을 직접 찍는다.
					LOG_INFO(_T("RequestRoomChatHistory: HMGET 응답 수신 (roomId=%d, eType=%d, strVal=%hs)"),
						roomId, static_cast<int>(hmgetRes.eType), hmgetRes.strVal.c_str());

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

					// [추가 — 진단용] 최종적으로 onComplete에 몇 개를 실어
					// 보내는지, jobQueue가 살아있는지 확인 — 여기가 null이면
					// onComplete 자체가 호출 안 되고 조용히 버려진다.
					LOG_INFO(_T("RequestRoomChatHistory: 최종 완료 (roomId=%d, 개수=%d, jobQueue=%s)"),
						roomId, static_cast<int>(historyOldestFirst.size()), jobQueue ? _T("유효") : _T("null"));

					if( jobQueue == nullptr )
						return;

					jobQueue->DoAsync([onComplete, historyOldestFirst]()
						{
							if( onComplete )
								onComplete(historyOldestFirst);
						});
				});
		});
}