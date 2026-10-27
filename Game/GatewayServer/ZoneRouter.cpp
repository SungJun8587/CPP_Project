
//***************************************************************************
// ZoneRouter.cpp: implementation of the CZoneRouter class.
//
//***************************************************************************

#include "pch.h"
#include "ZoneRouter.h"
#include "GatewayZoneSession.h"

//***************************************************************************
// @brief 존 서버를 라우팅 테이블에 등록합니다.
// @details 같은 zoneId가 이미 살아있는(연결 중인) 세션에 의해 등록돼 있으면 거부한다.
//          이전 세션이 이미 끊겼지만 종료 통지가 아직 처리되지 않은 상태라면 새 노드로 교체한다
//          (존 서버가 빠르게 재시작한 경우). 이 경우 이전 세션의 종료 통지는 sessionKey 불일치로 새 노드를 지우지 않는다.
// @param session    등록하는 존 서버 세션
// @param zoneId     존 ID
// @param kind       존 종류
// @param capacity   최대 수용 인원
// @param serverName 서버 이름
// @param nowMs      현재 시각(SteadyNowMs) — 하트비트 기준 시각 초기화용
// @param outNode    [out] 등록된 노드 (성공 시)
// @return 등록 결과
//***************************************************************************
EZoneRegisterResult CZoneRouter::Register(const std::shared_ptr<CGatewayZoneSession>& session, uint32_t zoneId, EZoneKind kind,
	uint32_t capacity, const std::string& serverName, int64_t nowMs, std::shared_ptr<SZoneNode>& outNode)
{
	if( zoneId == 0 || capacity == 0 || session == nullptr )
		return EZoneRegisterResult::InvalidZone;

	PRWriteLockGuard guard(_lock, __FUNCTION__);

	auto it = _nodes.find(zoneId);
	if( it != _nodes.end() )
	{
		std::shared_ptr<CGatewayZoneSession> existing = it->second->session.lock();
		if( existing != nullptr && existing->IsConnected() )
			return EZoneRegisterResult::DuplicateZone;
	}

	std::shared_ptr<SZoneNode> node = std::make_shared<SZoneNode>();
	node->zoneId = zoneId;
	node->kind = kind;
	node->capacity = capacity;
	node->serverName = serverName;
	node->session = session;
	node->sessionKey = session.get();
	node->lastHeartbeatMs.store(nowMs);

	_nodes[zoneId] = node;
	outNode = node;
	return EZoneRegisterResult::Ok;
}

//***************************************************************************
// @brief 존 서버를 라우팅 테이블에서 제거합니다. 등록된 노드의 세션이 인자와 같을 때만 제거한다.
// @param zoneId  존 ID
// @param session 제거를 요청하는 세션 (동일성 비교에만 쓰이며 역참조하지 않는다)
// @return 실제로 제거했으면 true, 이미 다른 세션으로 교체됐거나 없으면 false
//***************************************************************************
bool CZoneRouter::Unregister(uint32_t zoneId, const CGatewayZoneSession* session)
{
	PRWriteLockGuard guard(_lock, __FUNCTION__);

	auto it = _nodes.find(zoneId);
	if( it == _nodes.end() || it->second->sessionKey != session )
		return false;

	_nodes.erase(it);
	return true;
}

//***************************************************************************
// @brief zoneId로 존 노드를 찾습니다.
// @param zoneId 존 ID
// @return 노드, 없으면 nullptr
//***************************************************************************
std::shared_ptr<SZoneNode> CZoneRouter::Find(uint32_t zoneId) const
{
	PRReadLockGuard guard(_lock, __FUNCTION__);

	auto it = _nodes.find(zoneId);
	return it != _nodes.end() ? it->second : nullptr;
}

//***************************************************************************
// @brief zoneId에 해당하는 존 서버의 연결 세션을 찾습니다.
// @param zoneId 존 ID
// @return 연결 세션, 없거나 이미 사라졌으면 nullptr
//***************************************************************************
std::shared_ptr<CGatewayZoneSession> CZoneRouter::FindSession(uint32_t zoneId) const
{
	std::shared_ptr<SZoneNode> node = Find(zoneId);
	return node != nullptr ? node->session.lock() : nullptr;
}

//***************************************************************************
// @brief 등록된 모든 노드의 스냅샷을 반환합니다. 하트비트 타임아웃 점검/모니터링용.
// @return 노드 목록(락 밖에서 안전하게 순회 가능)
//***************************************************************************
std::vector<std::shared_ptr<SZoneNode>> CZoneRouter::Snapshot() const
{
	PRReadLockGuard guard(_lock, __FUNCTION__);

	std::vector<std::shared_ptr<SZoneNode>> nodes;
	nodes.reserve(_nodes.size());
	for( const auto& kv : _nodes )
		nodes.push_back(kv.second);
	return nodes;
}

//***************************************************************************
// @brief 존 정원 슬롯 하나를 예약합니다. 락 없이 CAS로 처리한다.
// @param node 대상 존 노드
// @return 예약했으면 true, 정원이 찼으면 false
//***************************************************************************
bool CZoneRouter::TryReserveSlot(SZoneNode& node)
{
	int32_t current = node.activeUsers.load(std::memory_order_relaxed);
	while( current < static_cast<int32_t>(node.capacity) )
	{
		if( node.activeUsers.compare_exchange_weak(current, current + 1, std::memory_order_acq_rel) )
			return true;
	}
	return false;
}

//***************************************************************************
// @brief 예약한 정원 슬롯을 반환합니다. 0 아래로 내려가지 않는다(노드 교체 후 늦은 반환 방어).
// @param node 대상 존 노드
//***************************************************************************
void CZoneRouter::ReleaseSlot(SZoneNode& node)
{
	int32_t current = node.activeUsers.load(std::memory_order_relaxed);
	while( current > 0 )
	{
		if( node.activeUsers.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel) )
			return;
	}
}