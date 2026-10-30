
//***************************************************************************
// ChatServerMainRelay.cpp : CChatServerMain의 채널(인스턴스) 간 전달 계층 구현
//   - 다른 채널에 접속한 사용자에게 패킷을 Redis Pub/Sub으로 넘기는 경로
//
// @details
// 채팅 서버는 채널마다 따로 떠 있고 세션은 프로세스 메모리에만 있다. 수신자가 이 프로세스에 없으면
//   1) 로그인 키 User:{id}의 serverGroupId/serverChannelId로 수신자의 채널을 한 번에 조회하고(EVAL 하나),
//   2) 채널별 토픽 Chat:{group}:{channel}에 "수신자 목록 + 패킷" 프레임 하나를 발행한다(채널당 1회).
// 각 서버는 자기 토픽만 구독하고, 프레임을 받으면 로컬 세션에 그대로 전달한다(프레임 형식은 ChatRelayFrame.h).
//
// 설계 원칙:
//   - 로컬 우선: 이 프로세스에 있는 수신자는 Redis를 거치지 않고 바로 보낸다. 조회는 로컬에 없는 수신자에게만 한다.
//   - 혼자면 비용 0: 유지보수 스레드가 1초마다 PUBSUB CHANNELS Chat:*로 다른 채널의 구독자를 확인하고,
//     없으면 조회/발행을 모두 건너뛴다(오프라인 수신자마다 하던 Redis 조회가 사라진다).
//   - 오프라인 단기 기억: 방금 "접속 중이 아님"으로 확인된 수신자는 kOfflineCacheMs 동안 다시 조회하지 않는다
//     (사람이 많은 그룹 대화에서 메시지마다 같은 오프라인 멤버를 조회하는 것을 막는다).
//   - 알림 전용: Pub/Sub은 구독자가 없거나 끊겨 있으면 메시지가 사라진다. 대화 내용은 항상 DB에 먼저 저장되고
//     이 경로는 "접속 중인 상대에게 즉시 알리는" 용도로만 쓴다(놓쳐도 다음 목록/기록 조회에서 복구된다).
//   - 차단 규칙 유지: 발신자 id를 프레임에 실어, 수신 서버가 수신자 세션의 차단 목록으로 같은 필터를 적용한다.
//   - 정상 종료 통지: 서버가 내려갈 때는 로컬 접속자 목록을 다른 채널 토픽에 한 번에 발행해(PresenceOffline),
//     받은 서버가 각 계정의 친구에게 종료를 알린다 — 종료 중에는 Redis 응답을 처리할 JobQueue가 돌지 않아
//     수신자 채널을 조회해서 보내는 일반 경로를 쓸 수 없기 때문이다.
//***************************************************************************

#include "pch.h"
#include "ChatServerMain.h"
#include "ChatSession.h"
#include "ChatRelayFrame.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <type_traits>

static_assert(std::is_same<PublicId, ChatRelay::Id>::value, "PublicId와 ChatRelay::Id는 같은 타입이어야 한다");

namespace
{
	constexpr int64 kOfflineCacheMs = 2000;			// "접속 중이 아님"을 기억하는 시간
	constexpr size_t kOfflineCacheMax = 50000;		// 이 크기를 넘으면 만료 항목부터 정리한다
	constexpr size_t kShutdownFrameAccounts = 256;	// 종료 통지 프레임 하나에 담는 계정 수 — 받는 서버가 프레임 하나를 한 작업으로 처리하므로 작게 나눈다

	//***************************************************************************
	// @brief 채널 간 전달을 허용하는 패킷 종류인지 확인한다.
	// @details 수신 서버는 Redis에서 온 프레임의 패킷을 그대로 클라이언트에 보내므로, 알림 계열만 통과시킨다
	//          (Redis에 접근할 수 있는 쪽이 응답/로그인 같은 임의 패킷을 주입하는 것을 막는다).
	//          전달 대상이 되는 알림을 새로 만들면 여기에 추가해야 한다.
	//***************************************************************************
	bool IsRelayablePacketType(uint16 type)
	{
		switch( static_cast<EChatPacketType>(type) )
		{
		case EChatPacketType::ConvMessageNotify:
		case EChatPacketType::ConvUpdatedNotify:
		case EChatPacketType::ConvRenamedNotify:
		case EChatPacketType::ConvDeletedNotify:
		case EChatPacketType::ConvReadNotify:
		case EChatPacketType::FriendStatusNotify:
		case EChatPacketType::FriendProfileChangedNotify:
		case EChatPacketType::FriendRequestNotify:
		case EChatPacketType::FriendAcceptedNotify:
		case EChatPacketType::FriendRemovedNotify:
			return true;
		default:
			return false;
		}
	}

	// 조회 결과의 "알 수 없음" 표시(조회 실패/응답 형식 오류). 빈 문자열은 "로그인 키 없음 = 접속 중이 아님"이다.
	const std::string kChannelUnknown = "?";

	//***************************************************************************
	// @brief 수신자 각각의 "group:channel"(로그인 키가 없으면 빈 문자열)을 같은 순서로 돌려주는 조회 스크립트.
	// @details 키 이름을 스크립트 안에서 조립하므로 KEYS를 선언하지 않는다 — Redis Cluster에서는 쓸 수 없다
	//          (단일 노드 전제, RefreshLoginKeys와 같음). 접두사는 ChatServerKeys::kUserPrefix와 같은 값이다.
	//***************************************************************************
	const CRedisScript& GetChannelLookupScript()
	{
		static const CRedisScript kLookupScript(
			std::string("local r = {}\n"
				"for i = 1, #ARGV do\n"
				"  local v = redis.call('HMGET', '") + ChatServerKeys::kUserPrefix + "' .. ARGV[i], '" + ChatServerKeys::kUserFieldGroupId + "', '" + ChatServerKeys::kUserFieldChannelId + "')\n"
			"  if v[1] and v[2] then r[i] = v[1] .. ':' .. v[2] else r[i] = '' end\n"
			"end\n"
			"return r");
		return kLookupScript;
	}
}

//***************************************************************************
// @brief 채널 간 전달 계층을 시작합니다 — 이 채널의 토픽(Chat:{group}:{channel})을 구독합니다.
// @details Start()에서 Redis 초기화 뒤에 호출한다. enableRelay가 꺼져 있으면 아무것도 하지 않는다.
//          구독 연결은 끊기면 스스로 다시 붙고 재구독한다. 첫 접속이 실패해도 Subscribe()는 등록만 해 두므로
//          Redis가 늦게 떠도 이후 자동으로 수신이 시작된다.
//***************************************************************************
bool CChatServerMain::StartRelay(bool enableRelay)
{
	_relayEnabled = false;
	if( !enableRelay || _redisService == nullptr )
		return true;

	_ownChannelKey = ChatServerKeys::BuildChannelKey(_serverGroupId, _serverChannelId);
	_relayTopic = ChatServerKeys::BuildRelayTopic(_ownChannelKey);
	if( !_redisService->Subscribe(_relayTopic,
		[this](const std::string& /*channel*/, const std::string& payload)
		{
			OnRelayMessage(payload);	// JobQueue(로직) 스레드에서 호출됨
		}) )
	{
		return false;
	}

	_relayEnabled = true;
	RefreshRelayPeers();	// 첫 결과가 오기 전까지는 "다른 채널이 있을 수 있다"로 보고 동작한다
	return true;
}

//***************************************************************************
// @brief 채널 간 전달 계층을 멈춥니다(구독 해제). Redis 서비스를 해제하기 전에 호출해야 한다.
// @details 세션을 닫기 전에 호출한다 — 먼저 로컬 접속자의 종료를 다른 채널에 일괄 통지한 뒤 전달을 끈다.
//***************************************************************************
void CChatServerMain::StopRelay()
{
	if( !_relayEnabled || _redisService == nullptr )
		return;

	BroadcastShutdownPresence();

	_relayEnabled = false;	// 이후 RelayToRemote()는 아무것도 하지 않는다
	_redisService->Unsubscribe(_relayTopic);
}

//***************************************************************************
// @brief 다른 채널의 구독자(토픽)를 확인해 _relayHasPeers/_relayPeerTopics를 갱신합니다(비동기).
// @details 유지보수 스레드가 주기적으로 호출한다. 조회가 실패하면 이전 값을 유지한다.
//***************************************************************************
void CChatServerMain::RefreshRelayPeers()
{
	if( !_relayEnabled || _redisService == nullptr )
		return;

	_redisService->SendCommand(Redis::MakeArgs({ "PUBSUB", "CHANNELS", ChatServerKeys::kRelayTopicPattern }),
		[this](const RedisValue& res)
		{
			if( res.eType != ERedisType::Array )
				return;

			std::vector<std::string> peers;
			for( const RedisValue& topic : res.arrayVal )
			{
				if( topic.eType == ERedisType::BulkString && topic.strVal != _relayTopic )
					peers.push_back(topic.strVal);
			}

			const bool hasPeers = !peers.empty();
			{
				std::lock_guard<std::mutex> lock(_relayPeerMutex);
				_relayPeerTopics = std::move(peers);
			}
			_relayHasPeers = hasPeers;
		});
}

//***************************************************************************
// @brief 서버 종료 시 이 프로세스의 접속자 전원이 나간다는 것을 다른 채널 서버들에게 한 번에 알립니다.
// @details 각 서버는 받은 계정의 친구 중 자기 프로세스 접속자에게 FriendStatusNotify(오프라인)를 보낸다.
//          Redis 응답을 기다리지 않는다 — 종료 중에는 응답 콜백을 처리할 JobQueue가 더 이상 돌지 않는다.
//***************************************************************************
void CChatServerMain::BroadcastShutdownPresence()
{
	std::vector<std::string> topics;
	{
		std::lock_guard<std::mutex> lock(_relayPeerMutex);
		topics = _relayPeerTopics;
	}
	if( topics.empty() )
		return;

	std::vector<PublicId> accounts;
	{
		std::lock_guard<std::mutex> lock(_onlineMutex);
		accounts.reserve(_onlineSessions.size());
		for( const auto& kv : _onlineSessions )
		{
			for( auto rit = kv.second.rbegin(); rit != kv.second.rend(); ++rit )
			{
				if( auto s = rit->lock() )
				{
					accounts.push_back(s->GetPublicId());
					break;
				}
			}
		}
	}

	for( size_t from = 0; from < accounts.size(); from += kShutdownFrameAccounts )
	{
		const size_t to = (std::min)(from + kShutdownFrameAccounts, accounts.size());
		const std::string frame = ChatRelay::BuildFrame(ChatRelay::EKind::PresenceOffline,
			std::vector<PublicId>(accounts.begin() + from, accounts.begin() + to), nullptr, 0, nullptr);
		for( const std::string& topic : topics )
			_redisService->Publish(topic, frame);
	}
}

//***************************************************************************
// @brief 최근 "접속 중이 아님"으로 확인된 수신자를 걸러냅니다.
//***************************************************************************
std::vector<PublicId> CChatServerMain::FilterOfflineCached(const std::vector<PublicId>& ids)
{
	std::vector<PublicId> result;
	result.reserve(ids.size());

	const auto now = std::chrono::steady_clock::now();
	std::lock_guard<std::mutex> lock(_offlineCacheMutex);
	for( const PublicId& id : ids )
	{
		auto it = _offlineCache.find(PublicIdToHex(id));
		if( it != _offlineCache.end() )
		{
			if( now < it->second )
				continue;			// 아직 유효한 "오프라인" 기록 — 조회하지 않는다
			_offlineCache.erase(it);
		}
		result.push_back(id);
	}
	return result;
}

//***************************************************************************
// @brief 수신자가 접속 중이 아님을 kOfflineCacheMs 동안 기억합니다.
//***************************************************************************
void CChatServerMain::MarkOffline(const PublicId& id)
{
	const auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(_offlineCacheMutex);
	if( _offlineCache.size() >= kOfflineCacheMax )
	{
		for( auto it = _offlineCache.begin(); it != _offlineCache.end(); )
			it = (now >= it->second) ? _offlineCache.erase(it) : std::next(it);
		if( _offlineCache.size() >= kOfflineCacheMax )
			_offlineCache.clear();
	}
	_offlineCache[PublicIdToHex(id)] = now + std::chrono::milliseconds(kOfflineCacheMs);
}

//***************************************************************************
// @brief 수신자들이 각각 어느 채널에 접속 중인지 비동기로 조회합니다(EVAL 하나에 kUserKeyBatch명씩).
// @param onReady ids와 같은 순서의 "group:channel". 로그인 키가 없으면 빈 문자열, 조회가 실패했으면 kChannelUnknown.
//        전달 계층이 꺼져 있거나 ids가 비면 호출한 스레드에서 바로, Redis 응답이 필요하면 JobQueue 스레드에서 한 번 호출된다.
//***************************************************************************
void CChatServerMain::LookupChannels(const std::vector<PublicId>& ids, std::function<void(const std::vector<std::string>& channels)> onReady)
{
	auto out = std::make_shared<std::vector<std::string>>(ids.size(), kChannelUnknown);
	if( ids.empty() || !_relayEnabled || _redisService == nullptr )
	{
		onReady(*out);
		return;
	}

	constexpr size_t kBatch = ChatServerKeys::kUserKeyBatch;
	const size_t batchCount = (ids.size() + kBatch - 1) / kBatch;
	auto pending = std::make_shared<std::atomic<size_t>>(batchCount);
	auto finishBatch = [out, pending, onReady]()
		{
			if( pending->fetch_sub(1) == 1 )
				onReady(*out);
		};

	const CRedisScript& script = GetChannelLookupScript();
	for( size_t begin = 0; begin < ids.size(); begin += kBatch )
	{
		const size_t end = (std::min)(begin + kBatch, ids.size());

		CVector<std::string> argv;
		argv.reserve(end - begin);
		for( size_t i = begin; i < end; ++i )
			argv.push_back(PublicIdToHex(ids[i]));

		const bool accepted = script.Run(*_redisService, CVector<std::string>(), argv,
			[out, begin, end, finishBatch](const RedisValue& res)
			{
				if( res.eType == ERedisType::Array && res.arrayVal.size() == end - begin )
				{
					for( size_t i = begin; i < end; ++i )
					{
						const RedisValue& v = res.arrayVal[i - begin];
						if( v.eType == ERedisType::BulkString )
							(*out)[i] = v.strVal;
					}
				}
				finishBatch();	// 실패한 배치는 kChannelUnknown으로 남는다
			});

		if( !accepted )
			finishBatch();
	}
}

//***************************************************************************
// @brief 이 프로세스에 접속하지 않은 수신자들에게 패킷을 전달합니다(다른 채널에 접속 중이면 전달됨).
// @param remoteIds 이 프로세스에서 세션을 찾지 못한 수신자들 — 호출부가 로컬 전달을 먼저 마친 뒤 남은 대상만 넘긴다
// @param data/size 전달할 패킷(PacketHeader로 시작해야 한다)
// @param sender 발신자(차단 필터용). nullptr이면 필터 없이 전달한다.
// @details 비동기다. 다른 채널의 구독자가 없으면 아무것도 하지 않는다. 있으면 수신자의 채널을 조회하고,
//          같은 채널 수신자는 프레임 하나로 묶어 발행한다. 접속 중이 아닌 수신자(로그인 키 없음)는 잠시 기억해 두고,
//          이 채널로 조회된 수신자(로컬에 없음이 확인됨)는 건너뛴다.
//          Pub/Sub이라 전달 보장은 없다.
//***************************************************************************
void CChatServerMain::RelayToRemote(const std::vector<PublicId>& remoteIds, const void* data, uint16 size, const PublicId* sender)
{
	if( !_relayEnabled || !_relayHasPeers.load() || _redisService == nullptr || remoteIds.empty()
		|| data == nullptr || size < sizeof(PacketHeader) )
		return;

	auto targets = std::make_shared<std::vector<PublicId>>(FilterOfflineCached(remoteIds));
	if( targets->empty() )
		return;

	const std::string packet(static_cast<const char*>(data), size);
	const bool hasSender = (sender != nullptr);
	const PublicId senderId = hasSender ? *sender : PublicId{};
	const std::string ownChannel = _ownChannelKey;

	LookupChannels(*targets,
		[this, targets, packet, hasSender, senderId, ownChannel](const std::vector<std::string>& channels)
		{
			if( !_relayEnabled || _redisService == nullptr )
				return;

			// 채널별로 수신자를 묶는다.
			std::map<std::string, std::vector<PublicId>> byChannel;
			for( size_t i = 0; i < targets->size(); ++i )
			{
				const std::string& channel = channels[i];
				if( channel == kChannelUnknown || channel == ownChannel )
					continue;	// 조회 실패(알림은 놓칠 수 있음) 또는 이 채널(로컬에 없음이 확인됨)
				if( channel.empty() )
				{
					MarkOffline((*targets)[i]);	// 로그인 키 없음 — 접속 중이 아님
					continue;
				}
				byChannel[channel].push_back((*targets)[i]);
			}

			for( auto& kv : byChannel )
			{
				const std::string topic = ChatServerKeys::BuildRelayTopic(kv.first);
				const std::vector<PublicId>& ids = kv.second;
				for( size_t from = 0; from < ids.size(); from += ChatRelay::kMaxRecipients )
				{
					const size_t to = (std::min)(from + ChatRelay::kMaxRecipients, ids.size());
					_redisService->Publish(topic,
						ChatRelay::BuildFrame(ChatRelay::EKind::Packet, std::vector<PublicId>(ids.begin() + from, ids.begin() + to),
							packet.data(), packet.size(), hasSender ? &senderId : nullptr));
				}
			}
		});
}

//***************************************************************************
// @brief 다른 채널에서 온 프레임을 파싱해 이 프로세스의 수신자 세션에 전달합니다.
// @details JobQueue(로직) 스레드에서 호출된다. 잘못된 프레임은 버린다.
//          Packet: 발신자가 있으면 수신자 세션의 차단 목록으로 걸러낸다(BroadcastChatToRoom/PushMessage와 같은 규칙).
//          PresenceOffline: 각 계정의 친구 중 이 프로세스 접속자에게만 종료를 알린다(다시 전달하지 않는다 — 루프 방지).
//***************************************************************************
void CChatServerMain::OnRelayMessage(const std::string& payload)
{
	ChatRelay::SFrameView frame;
	if( !ChatRelay::ParseFrame(payload, frame) )
		return;

	if( frame.kind == ChatRelay::EKind::PresenceOffline )
	{
		for( size_t i = 0; i < frame.count; ++i )
		{
			const PublicId account = frame.RecipientAt(i);
			if( !IsUserOnline(account) )	// 그 사이 이 서버로 다시 접속했다면 종료로 알리지 않는다
				NotifyFriendPresence(account, false, true);
		}
		return;
	}

	PacketHeader header{};
	std::memcpy(&header, frame.packet, sizeof(header));
	if( !IsRelayablePacketType(header.type) )
		return;	// 전달 대상이 아닌 패킷 종류 — 버린다

	const std::string senderKey = frame.hasSender ? PublicIdToHex(frame.sender) : std::string();
	std::vector<PublicId> ids;
	ids.reserve(frame.count);
	for( size_t i = 0; i < frame.count; ++i )
		ids.push_back(frame.RecipientAt(i));

	const auto sessions = FindOnlineSessions(ids);
	for( size_t i = 0; i < ids.size(); ++i )
	{
		const auto& session = sessions[i];
		if( session == nullptr || !session->IsConnected() )
			continue;
		if( frame.hasSender && session->IsBlockingHex(senderKey) )
			continue;
		session->Send(frame.packet, static_cast<uint16>(frame.packetSize));
	}
}

//***************************************************************************
// @brief ids 중 이 프로세스에 접속 중인 세션에는 바로 보내고, 나머지는 다른 채널로 전달합니다.
// @param ids 수신자들
// @param data/size 패킷
// @param skip nullptr이 아니면 이 id는 건너뜀(예: 행위자 본인)
// @param sender 발신자(차단 필터용, nullptr이면 필터 없음). 로컬 수신자에게도 이 규칙을 적용한다.
// @param allowRelay false면 로컬 세션에만 보낸다(다른 채널에서 받은 알림을 다시 퍼뜨리지 않기 위함).
//***************************************************************************
void CChatServerMain::DeliverToUsers(const std::vector<PublicId>& ids, const void* data, uint16 size,
	const PublicId* skip, const PublicId* sender, bool allowRelay)
{
	if( ids.empty() || data == nullptr || size == 0 )
		return;

	const std::string senderKey = (sender != nullptr) ? PublicIdToHex(*sender) : std::string();
	const auto sessions = FindOnlineSessions(ids);

	std::vector<PublicId> remote;
	for( size_t i = 0; i < ids.size(); ++i )
	{
		if( skip != nullptr && ids[i] == *skip )
			continue;

		const auto& session = sessions[i];
		if( session == nullptr || !session->IsConnected() )
		{
			if( allowRelay )
				remote.push_back(ids[i]);
			continue;
		}
		if( sender != nullptr && ids[i] != *sender && session->IsBlockingHex(senderKey) )
			continue;
		session->Send(data, size);
	}

	if( allowRelay )
		RelayToRemote(remote, data, size, sender);
}

//***************************************************************************
// @brief ids 각각이 지금 어느 채널에든 접속 중인지 비동기로 알려줍니다(로컬 세션 우선, 없으면 Redis 로그인 키 조회).
// @param onReady 결과(ids와 같은 순서)를 받는 콜백. 조회할 대상이 없거나 다른 채널이 없으면 호출한 스레드에서 바로,
//        Redis 조회가 필요하면 JobQueue(로직) 스레드에서 호출된다. 조회 실패 시 원격 대상은 오프라인으로 본다.
// @details 접속 목록/멤버 목록의 "online" 표시를 다른 채널 사용자까지 맞추려는 용도다. 오프라인 기억(MarkOffline)은
//          쓰지 않는다 — 표시는 항상 최신이어야 한다. 로그인 키는 TTL이 있어 서버가 비정상 종료하면
//          최대 TTL만큼 접속 중으로 보일 수 있다.
//***************************************************************************
void CChatServerMain::ResolveOnline(const std::vector<PublicId>& ids, std::function<void(const std::vector<bool>& online)> onReady)
{
	if( !onReady )
		return;

	const auto sessions = FindOnlineSessions(ids);
	auto online = std::make_shared<std::vector<bool>>(ids.size(), false);
	auto remote = std::make_shared<std::vector<PublicId>>();
	auto remoteIdx = std::make_shared<std::vector<size_t>>();
	for( size_t i = 0; i < ids.size(); ++i )
	{
		if( sessions[i] != nullptr )
		{
			(*online)[i] = true;
			continue;
		}
		remote->push_back(ids[i]);
		remoteIdx->push_back(i);
	}

	if( remote->empty() || !_relayEnabled || !_relayHasPeers.load() || _redisService == nullptr )
	{
		onReady(*online);
		return;
	}

	const std::string ownChannel = _ownChannelKey;
	LookupChannels(*remote,
		[online, remoteIdx, onReady, ownChannel](const std::vector<std::string>& channels)
		{
			for( size_t k = 0; k < channels.size(); ++k )
			{
				// 이 채널로 조회됐는데 로컬 세션이 없었다면 막 끊긴 것이라 오프라인으로 본다. 실패(?)도 오프라인.
				if( !channels[k].empty() && channels[k] != kChannelUnknown && channels[k] != ownChannel )
					(*online)[(*remoteIdx)[k]] = true;
			}
			onReady(*online);
		});
}