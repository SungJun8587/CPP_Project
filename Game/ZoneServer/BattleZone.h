
//***************************************************************************
// BattleZone.h : interface for the CBattleZone class.
//
//***************************************************************************

#ifndef BATTLEZONE_H
#define BATTLEZONE_H

#include "../GameCommon/ZoneCore/FieldZone.h"
#include "ZonePlayer.h"

#include <atomic>
#include <unordered_map>
#include <vector>

class CZoneServerMain;

//***************************************************************************
// @class CBattleZone
// @brief 존 서버 프로세스가 호스팅하는 필드/던전 존. CFieldZone 위에 클라이언트 입장/퇴장,
//        패킷 처리, 스폰·이동 알림 전송을 얹는다.
// @details 클라이언트로 나가는 모든 패킷은 CZoneServerMain::SendToClient()를 통해 Gateway로 중계된다.
//          public 메서드 중 HandleEnter/HandleLeave/HandleClientPacket/Send*는 존 스레드 전용이고,
//          GetPlayerCount()만 다른 스레드(하트비트)에서 호출할 수 있다.
//          소멸자에서 Stop()을 먼저 호출해, 틱 스레드가 살아 있는 동안 이 클래스의 멤버가 파괴되지 않게 한다.
//***************************************************************************
class CBattleZone : public CFieldZone<>
{
public:
	CBattleZone(const FieldZoneConfig& config, CZoneServerMain* server, const Vec2& spawnPos, float playerSpeed, const SCombatStats& playerStats);
	virtual ~CBattleZone();

	// ── 존 스레드 전용 ───────────────────────────────────
	void HandleEnter(uint64 clientSessionId, uint64 charId);
	void HandleLeave(uint64 clientSessionId, uint64 charId);
	void HandleClientPacket(uint64 clientSessionId, const PacketHeader* header);

	void SendSpawn(CZonePlayer& to, CZoneEntity& about);
	void SendDespawn(CZonePlayer& to, CZoneEntity& about);

	//***************************************************************************
	// @brief 현재 플레이어 수를 반환합니다. 다른 스레드(하트비트)에서 호출해도 안전합니다.
	// @return 존에 입장해 있는 플레이어 수
	//***************************************************************************
	uint32 GetPlayerCount() const { return _playerCount.load(std::memory_order_relaxed); }

protected:
	virtual void OnEntityMoveStarted(CZoneEntity& entity) override;
	virtual void OnEntityMoveStopped(CZoneEntity& entity, bool arrived) override;
	virtual void OnEntityTeleported(CZoneEntity& entity) override;
	virtual void OnMoveRejected(CZoneEntity& entity, MoveResult reason) override;
	virtual void OnEntityDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage) override;
	virtual void OnEntityDied(CZoneEntity& victim, CZoneEntity* killer) override;
	virtual void OnEntityRespawned(CZoneEntity& entity) override;
	virtual void OnAttackRejected(CZoneEntity& attacker, uint64 targetId, AttackResult reason) override;

private:
	void SendToEntity(CZoneEntity& target, const void* data, uint16 size);
	void AddRecipient(CZoneEntity& target);
	void FlushRecipients(const void* data, uint16 size);
	void BroadcastAround(CZoneEntity& entity, const void* data, uint16 size);
	void BroadcastCombat(CZoneEntity& victim, CZoneEntity* other, const void* data, uint16 size);

	CZoneServerMain* _server;		// 패킷 중계 경로를 가진 존 서버
	Vec2				_spawnPos;		// 입장 시 스폰 위치
	float				_playerSpeed;	// 플레이어 기본 이동 속도 (units/sec)
	std::vector<uint64>	_recipients;	// 브로드캐스트 수신자 세션 ID 모음 (존 스레드 전용, 재사용으로 힙 할당 방지)
	SCombatStats		_playerStats;	// 플레이어 전투 능력치 (서버 설정값. 클라이언트 값은 신뢰하지 않음)

	std::unordered_map<uint64, CZoneEntity*>	_sessionToEntity;	// 클라이언트 세션 ID -> 플레이어 엔티티 (존 스레드 전용, 패킷마다 해시 조회 1회로 끝내기 위함)
	std::atomic<uint32>						_playerCount;	// 플레이어 수 (존 스레드가 갱신, 하트비트 스레드가 읽음)
};

#endif // ndef BATTLEZONE_H