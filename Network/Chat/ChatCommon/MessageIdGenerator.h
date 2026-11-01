
//***************************************************************************
// MessageIdGenerator.h : 방 대화 메시지 id 발급 (header-only)
//
// @details
// 채팅 서버 인스턴스(노드)마다 하나씩 두고, 방 대화 메시지에 붙이는 64비트 id를 발급한다.
// id는 Redis의 방 대화 기록(RoomChatOrder ZSET의 score, RoomChatMsg HASH의 field)과 클라이언트의
// 메시지 식별에 쓰이므로 프로세스·채널·재시작을 가로질러 겹치면 안 된다.
//
//   id = (카운터 << kNodeBits) | 노드 번호        카운터 = 시각(ms) << kCounterBitsPerMs 에서 시작해 1씩 증가
//
//   - 노드 번호는 하위 kNodeBits(10)비트에 고정한다. 카운터가 증가하며 올림이 생겨도 위쪽(시각 쪽)으로만
//     번지므로 노드 번호는 깨지지 않고, 노드 번호가 다른 두 서버는 같은 id를 낼 수 없다(최대 1,024개).
//   - 카운터는 시각에서 시작하므로 재시작해도 이전 실행이 남긴 기록의 id보다 커진다. 1ms에 최대
//     2^kCounterBitsPerMs(64)개까지는 시계와 보조를 맞춘다. 그보다 오래 빠르게 발급하면 카운터가 시계를 앞서 가며
//     id는 그 동안에도 유일하지만, 그 직후 재시작하면 시계가 카운터보다 뒤일 수 있으므로 floorId를 함께 쓴다.
//   - 값은 약 2^57로 int64 안에 충분히 들어간다.
//   - 시작 시점 시계가 거꾸로 갔을 때를 막으려면 이전 실행의 마지막 id를 floorId로 넘긴다(Init() 또는 RaiseFloor()).
//     시작 id가 그 값보다 커지도록 올려 준다. 채팅 서버는 LastIssuedId()를 Redis(ChatServerKeys의 MessageIdFloor 키)에
//     하트비트 주기로 기록해 두었다가, 시작할 때 읽어 RaiseFloor()에 넘긴다.
//***************************************************************************

#ifndef MESSAGEIDGENERATOR_H
#define MESSAGEIDGENERATOR_H

#include <atomic>
#include <cstdint>

namespace ChatId
{
	inline constexpr int32_t kNodeBits = 10;
	inline constexpr int64_t kMaxNodeId = (int64_t(1) << kNodeBits) - 1;	// 1023
	inline constexpr int32_t kCounterBitsPerMs = 6;							// 1ms당 카운터 칸 수 = 64

	// @brief 노드 번호가 하위 kNodeBits에 들어갈 수 있는지 확인합니다.
	inline constexpr bool IsValidNodeId(int64_t nodeId)
	{
		return nodeId >= 0 && nodeId <= kMaxNodeId;
	}

	// @brief id에서 노드 번호를 꺼냅니다(진단·테스트용).
	inline constexpr int64_t NodeOf(int64_t id)
	{
		return id & kMaxNodeId;
	}

	class CMessageIdGenerator
	{
	public:
		// @brief 발급을 시작합니다. 서버 시작 시 한 번 호출합니다.
		// @param nodeId  이 인스턴스의 노드 번호(0~kMaxNodeId). 같은 Redis를 쓰는 인스턴스끼리 서로 달라야 합니다.
		// @param nowMs   현재 시각(에포크 ms). 발급 id가 이 시각보다 뒤에서 시작합니다.
		// @param floorId 이전 실행이 발급한 마지막 id를 알면 넘깁니다(0이면 무시). 이 값 이하로는 발급하지 않습니다.
		// @return nodeId가 범위를 벗어나면 false(발급기는 초기화되지 않음).
		bool Init(int64_t nodeId, int64_t nowMs, int64_t floorId = 0)
		{
			if( !IsValidNodeId(nodeId) )
				return false;

			_nodeId = nodeId;
			_nextCounter.store(nowMs << kCounterBitsPerMs);
			if( floorId > 0 )
				RaiseFloor(floorId);
			return true;
		}

		// @brief 이후 발급하는 id가 floorId보다 커지도록 카운터를 올립니다(내리지는 않음). 스레드 안전합니다.
		// @details Init() 뒤에 호출할 수 있다 — Redis에서 읽은 이전 실행의 마지막 id를 시작 직후에 반영하는 용도.
		//          이미 floorId보다 큰 id를 내보내는 중이면 아무것도 바꾸지 않는다.
		void RaiseFloor(int64_t floorId)
		{
			if( floorId <= 0 )
				return;

			const int64_t floorCounter = (floorId >> kNodeBits) + 1;
			int64_t current = _nextCounter.load();
			while( current < floorCounter && !_nextCounter.compare_exchange_weak(current, floorCounter) )
			{
			}
		}

		// @brief 지금까지 발급한 id 중 가장 큰 값을 돌려줍니다. 아직 발급한 적이 없으면 시작 지점 바로 앞의 값입니다.
		// @details 이 값을 다음 실행의 floorId로 쓰면 그 실행은 이 값보다 큰 id부터 발급한다. 스레드 안전합니다.
		int64_t LastIssuedId() const
		{
			return ((_nextCounter.load() - 1) << kNodeBits) | _nodeId;
		}

		// @brief 다음 id를 발급합니다. 스레드 안전하며 같은 인스턴스 안에서 항상 증가합니다.
		int64_t Next()
		{
			return (_nextCounter.fetch_add(1) << kNodeBits) | _nodeId;
		}

		int64_t NodeId() const { return _nodeId; }

	private:
		int64_t					_nodeId = 0;
		std::atomic<int64_t>	_nextCounter{ 1 };
	};
}

#endif // MESSAGEIDGENERATOR_H