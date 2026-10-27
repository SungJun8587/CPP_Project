
//***************************************************************************
// ZoneRouter.h : interface for the CZoneRouter class.
//
//***************************************************************************

#ifndef ZONEROUTER_H
#define ZONEROUTER_H

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CGatewayZoneSession;

//***************************************************************************
// @struct SZoneNode
// @brief Gateway가 관리하는 존 서버 하나의 등록 정보와 부하 상태.
// @details 가변 값은 전부 atomic이라 락 없이 갱신/조회된다. 노드는 shared_ptr로 공유되어,
//          라우팅 테이블에서 제거된 뒤에도 진행 중인 요청이 안전하게 마칠 수 있다.
//***************************************************************************
struct SZoneNode
{
	uint32_t							zoneId = 0;			// 존 ID
	EZoneKind							kind = EZoneKind::Field;
	uint32_t							capacity = 0;		// 최대 수용 인원
	std::string							serverName;			// 로그/모니터링용 이름

	std::weak_ptr<CGatewayZoneSession>	session;			// 이 존 서버와의 연결 세션
	const CGatewayZoneSession*			sessionKey = nullptr;	// 세션 동일성 비교용(Unregister) — 역참조 금지

	std::atomic<int32_t>				activeUsers{ 0 };	// Gateway 집계 인원(입장 진행 중 포함) — 입장 제한 기준
	std::atomic<int64_t>				lastHeartbeatMs{ 0 };	// 마지막 하트비트 수신 시각(SteadyNowMs 기준)
	std::atomic<uint32_t>				reportedUsers{ 0 };	// 존이 보고한 인원(모니터링용)
	std::atomic<uint32_t>				lastTickUs{ 0 };	// 존이 보고한 마지막 틱 처리 시간
	std::atomic<uint32_t>				maxTickUs{ 0 };		// 존이 보고한 최대 틱 처리 시간
};

//***************************************************************************
// @class CZoneRouter
// @brief zoneId -> 존 서버 노드 라우팅 테이블.
// @details 등록/해제는 드물고 조회는 입장 요청·패킷 중계마다 일어나므로 읽기/쓰기 락(PRWLock)으로 보호한다.
//          입장 정원은 노드의 atomic 카운터(TryReserveSlot/ReleaseSlot)로 락 없이 CAS 처리한다.
//***************************************************************************
class CZoneRouter
{
public:
	CZoneRouter() = default;
	~CZoneRouter() = default;

	CZoneRouter(const CZoneRouter&) = delete;
	CZoneRouter& operator=(const CZoneRouter&) = delete;

	EZoneRegisterResult Register(const std::shared_ptr<CGatewayZoneSession>& session, uint32_t zoneId, EZoneKind kind,
		uint32_t capacity, const std::string& serverName, int64_t nowMs, std::shared_ptr<SZoneNode>& outNode);

	bool Unregister(uint32_t zoneId, const CGatewayZoneSession* session);

	std::shared_ptr<SZoneNode> Find(uint32_t zoneId) const;
	std::shared_ptr<CGatewayZoneSession> FindSession(uint32_t zoneId) const;
	std::vector<std::shared_ptr<SZoneNode>> Snapshot() const;

	static bool TryReserveSlot(SZoneNode& node);
	static void ReleaseSlot(SZoneNode& node);

private:
	mutable PRWLock										_lock;		// _nodes 보호
	std::unordered_map<uint32_t, std::shared_ptr<SZoneNode>>	_nodes;		// zoneId -> 노드
};

#endif // ndef ZONEROUTER_H
