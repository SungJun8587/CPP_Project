
//***************************************************************************
// ZoneEntity.h : interface for the CZoneEntity class.
//
//***************************************************************************

#ifndef ZONEENTITY_H
#define ZONEENTITY_H

#include <cstddef>

#include <BaseRedefineDataType.h>

#include "GridAOI.h"
#include "ZoneMath.h"

class CFieldZoneBase;

//***************************************************************************
// @brief 존에 배치되는 엔티티 종류.
//***************************************************************************
enum class EntityType : uint8
{
	Player,
	Monster,
	Npc,
	Item,
};

//***************************************************************************
// @struct SCombatStats
// @brief 엔티티의 전투 능력치. 서버가 정한 값만 쓰며 클라이언트가 보낸 값은 신뢰하지 않는다.
// @details 플레이어는 설정 파일 값으로, 몬스터는 이후 단계의 데이터 테이블 값으로 채운다.
//          SetCombatStats()가 범위를 보정하므로 잘못된 값이 들어와도 전투 계산이 깨지지 않는다.
//***************************************************************************
struct SCombatStats
{
	int32	maxHp = 100;				// 최대 체력 (1 이상)
	int32	attack = 10;				// 공격력 (0 이상)
	int32	defense = 0;				// 방어력 (0 이상)
	float	attackRange = 2.0f;			// 기본 공격 사거리 (units)
	uint32	attackIntervalMs = 1000;	// 기본 공격 최소 간격 (ms, 1 이상)
};

//***************************************************************************
// @struct SReqGate
// @brief 클라이언트 요청(이동/공격 등)의 최소 간격 검사기. 존 논리 시간 기준이며 엔티티마다 요청 종류별로 하나씩 둔다.
// @details 통과한 요청만 시각을 갱신하므로, 거부된 요청이 간격을 계속 밀어내지 않는다.
//***************************************************************************
struct SReqGate
{
	uint64	lastMs = 0;		// 마지막으로 통과한 요청의 존 논리 시각
	bool	has = false;	// 통과한 요청이 있는지

	//***************************************************************************
	// @brief 요청을 통과시킬지 판정하고, 통과하면 시각을 기록합니다.
	// @param nowMs         현재 존 논리 시각
	// @param minIntervalMs 요청 최소 간격(ms). 0이면 제한 없음
	// @return 통과하면 true, 간격이 짧아 무시해야 하면 false
	//***************************************************************************
	bool TryPass(uint64 nowMs, uint32 minIntervalMs)
	{
		if( minIntervalMs != 0 && has && nowMs - lastMs < minIntervalMs )
			return false;

		has = true;
		lastMs = nowMs;
		return true;
	}

	void Reset() { has = false; lastMs = 0; }
};

//***************************************************************************
// @class CZoneEntity
// @brief 존에 배치되는 모든 객체(플레이어/몬스터/NPC/아이템)의 기반 클래스.
// @details
// 위치/이동/AOI 소속 정보는 CFieldZoneBase와 CGridAOI만 변경하며(friend),
// 모든 접근은 존 스레드에서만 이루어지므로 별도의 락이 없다.
// AOI 진입/이탈 통지(OnAoiEnter/OnAoiLeave)는 파생 클래스가 재정의하여
// 스폰/디스폰 패킷 전송 등에 사용한다.
//
// 통지 콜백 안에서는 AOI 구조를 변경(Add/Remove/Update)하거나
// 엔티티를 제거해서는 안 된다.
//***************************************************************************
class CZoneEntity
{
public:
	CZoneEntity(uint64 id, EntityType type);
	virtual ~CZoneEntity();

	CZoneEntity(const CZoneEntity&) = delete;
	CZoneEntity& operator=(const CZoneEntity&) = delete;

	void SetSpawnPos(const Vec2& pos);
	void SetCombatStats(const SCombatStats& stats);

	//***************************************************************************
	// @brief 엔티티 ID를 반환합니다.
	// @return 존 내에서 유일한 엔티티 ID
	//***************************************************************************
	uint64 GetId() const { return _id; }

	//***************************************************************************
	// @brief 엔티티 종류를 반환합니다.
	// @return 엔티티 타입
	//***************************************************************************
	EntityType GetType() const { return _type; }

	//***************************************************************************
	// @brief 서버가 보유한 현재 위치를 반환합니다.
	// @return 현재 좌표
	//***************************************************************************
	const Vec2& GetPos() const { return _pos; }

	//***************************************************************************
	// @brief 이동 중인 경우의 목적지를 반환합니다.
	// @return 목적지 좌표
	//***************************************************************************
	const Vec2& GetDest() const { return _dest; }

	//***************************************************************************
	// @brief 이동 속도를 반환합니다.
	// @return 이동 속도(units/sec)
	//***************************************************************************
	float GetSpeed() const { return _speed; }

	//***************************************************************************
	// @brief 이동 속도를 설정합니다. 속도는 서버 스탯으로만 결정되며 클라이언트 값은 신뢰하지 않습니다.
	// @param speed 이동 속도(units/sec)
	//***************************************************************************
	void SetSpeed(float speed) { _speed = speed; }

	//***************************************************************************
	// @brief 이동 중인지 반환합니다.
	// @return 이동 중이면 true
	//***************************************************************************
	bool IsMoving() const { return _moving; }

	//***************************************************************************
	// @brief 전투 능력치를 반환합니다.
	// @return 전투 능력치
	//***************************************************************************
	const SCombatStats& GetCombatStats() const { return _stats; }

	//***************************************************************************
	// @brief 현재 체력을 반환합니다.
	// @return 현재 체력 (사망 상태면 0)
	//***************************************************************************
	int32 GetHp() const { return _hp; }

	//***************************************************************************
	// @brief 살아 있는지 반환합니다. 사망한 엔티티는 리스폰하기 전까지 이동/공격/피격 대상이 될 수 없습니다.
	// @return 살아 있으면 true
	//***************************************************************************
	bool IsAlive() const { return !_dead; }

	//***************************************************************************
	// @brief 전투에 참여하는 종류(플레이어/몬스터)인지 반환합니다.
	// @return 전투 참여 엔티티면 true
	//***************************************************************************
	bool IsCombatant() const { return _type == EntityType::Player || _type == EntityType::Monster; }

	//***************************************************************************
	// @brief 무적이 끝나는 존 논리 시각을 반환합니다. 0이면 무적이 아닙니다.
	// @return 무적 종료 시각(ms)
	//***************************************************************************
	uint64 GetInvulnerableUntilMs() const { return _invulnerableUntilMs; }

	//***************************************************************************
	// @brief 리스폰 위치를 반환합니다. SetSpawnPos()로 정한 위치입니다.
	// @return 리스폰 좌표
	//***************************************************************************
	const Vec2& GetSpawnPos() const { return _spawnPos; }

protected:
	virtual void OnAoiEnter(CZoneEntity& other);
	virtual void OnAoiLeave(CZoneEntity& other);

private:
	template <typename T> friend class CGridAOI;
	friend class CFieldZoneBase;

	static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

	uint64		_id;					// 엔티티 ID
	EntityType	_type;					// 엔티티 종류
	Vec2		_pos;					// 현재 위치 (서버 권위)
	Vec2		_dest;					// 이동 목적지
	float		_speed;					// 이동 속도 (units/sec)
	bool		_moving;				// 이동 중 여부
	bool		_inZone;				// 존에 등록되었는지 여부
	Vec2		_spawnPos;				// 리스폰 위치 (SetSpawnPos로 설정)

	SCombatStats _stats;				// 전투 능력치
	int32		_hp;					// 현재 체력 - CFieldZoneBase 전용 변경
	bool		_dead;					// 사망 여부 - CFieldZoneBase 전용 변경
	uint64		_nextAttackMs;			// 다음 기본 공격이 가능한 존 논리 시각 - CFieldZoneBase 전용
	uint64		_invulnerableUntilMs;	// 무적 종료 시각 (존 논리 시간, 0이면 무적 아님) - CFieldZoneBase 전용
	SReqGate	_attackGate;			// 공격 요청 간격 검사 - CFieldZoneBase 전용
	uint64		_respawnTimerId;		// 예약된 자동 리스폰 타이머 ID (0이면 없음) - CFieldZoneBase 전용

	SAoiNode	_aoiNode;				// 소속 AOI 셀 정보 - CGridAOI 전용
	size_t		_movingIdx;				// 이동 목록 내 위치 - CFieldZoneBase 전용
	SReqGate	_moveGate;				// 이동 요청 간격 검사 - CFieldZoneBase 전용
};

#endif // ndef ZONEENTITY_H