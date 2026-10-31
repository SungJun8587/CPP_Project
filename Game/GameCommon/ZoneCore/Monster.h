
//***************************************************************************
// Monster.h : interface for the CMonster class.
//
//***************************************************************************

#ifndef MONSTER_H
#define MONSTER_H

#include "ZoneEntity.h"
#include "MonsterData.h"

class CMonsterSystem;

//***************************************************************************
// @brief 몬스터 엔티티 ID 대역. 최상위 비트가 켜진 ID는 CMonsterSystem이 발급하는 몬스터 전용이다.
// @details 플레이어(캐릭터 ID)는 이 비트를 쓰지 않아야 하며, 존 입장 시 거부한다.
//          이 규칙 덕분에 엔티티 종류가 Monster이면서 이 대역의 ID인 엔티티는 항상 CMonster라고 보고 static_cast 할 수 있다.
//***************************************************************************
constexpr uint64 kMonsterIdBase = 0x8000000000000000ull;

//***************************************************************************
// @brief 몬스터 AI 상태.
//***************************************************************************
enum class EMonsterState : uint8
{
	Idle,		// 제자리 대기. 주변 플레이어를 감시하다 선공 대상을 찾거나 피격되면 Chase
	Chase,		// 어그로 대상을 추적하고 사거리 안이면 공격
	Return,		// 추적을 포기하고 스폰 지점으로 복귀. 도착하면 체력을 회복하고 Idle. 복귀 중에는 어그로를 받지 않는다
};

//***************************************************************************
// @struct SAggroEntry
// @brief 어그로 표의 한 항목.
//***************************************************************************
struct SAggroEntry
{
	uint64	entityId = 0;	// 위협을 준 엔티티 ID
	int64	threat = 0;		// 누적 위협 수치
};

//***************************************************************************
// @class CMonster
// @brief AI로 움직이는 몬스터 엔티티. 데이터(템플릿)는 불변 공유 객체를 가리키고, 개체별 상태만 가진다.
// @details
// 시야(AOI)에 플레이어가 한 명도 없는 몬스터는 AI 목록에 올라가지 않아 틱 비용이 0이다.
// 플레이어가 시야에 들어오면(OnAoiEnter) 활성화되고, 대기 상태에서 시야가 비면 다음 사고(Think)에서 비활성화된다.
// 상태를 바꾸는 로직은 모두 CMonsterSystem이 가지며, 이 클래스는 상태 저장과 어그로 표 관리만 한다.
//***************************************************************************
class CMonster : public CZoneEntity
{
public:
	static constexpr size_t kMaxAggro = 8;		// 어그로 표 최대 항목 수 (가장 낮은 위협부터 밀려난다)

	CMonster(uint64 id, const SMonsterTemplate& tmpl, CMonsterSystem* system, const Vec2& anchor, float spawnRadius);

	//***************************************************************************
	// @brief 몬스터 템플릿을 반환합니다.
	//***************************************************************************
	const SMonsterTemplate& GetTemplate() const { return *_tmpl; }

	//***************************************************************************
	// @brief 스폰 중심점을 반환합니다. 부활 위치는 이 점을 중심으로 한 반경 안에서 정해진다.
	//***************************************************************************
	const Vec2& GetAnchor() const { return _anchor; }

	//***************************************************************************
	// @brief 스폰 반경을 반환합니다.
	//***************************************************************************
	float GetSpawnRadius() const { return _spawnRadius; }

	//***************************************************************************
	// @brief 복귀 지점(마지막으로 스폰된 위치)을 반환합니다.
	//***************************************************************************
	const Vec2& GetHome() const { return _home; }

	EMonsterState GetState() const { return _state; }
	uint64 GetTargetId() const { return _targetId; }
	uint32 GetNearPlayers() const { return _nearPlayers; }
	bool IsActive() const { return _active; }
	size_t GetAggroCount() const { return _aggroCount; }

	void AddThreat(uint64 entityId, int64 amount);
	void RemoveThreat(uint64 entityId);
	void ClearAggro();

protected:
	virtual void OnAoiEnter(CZoneEntity& other) override;
	virtual void OnAoiLeave(CZoneEntity& other) override;

private:
	friend class CMonsterSystem;

	const SMonsterTemplate* _tmpl;				// 몬스터 종류 데이터 (CMonsterData 소유)
	CMonsterSystem* _system;			// AI 시스템 (활성화 요청 대상)
	Vec2					_anchor;			// 스폰 중심점
	float					_spawnRadius;		// 스폰 반경
	Vec2					_home;				// 복귀 지점 (마지막 스폰 위치)
	EMonsterState			_state;				// AI 상태
	uint32					_nearPlayers;		// 시야 안 플레이어 수
	bool					_active;			// AI 목록에 올라 있는지
	size_t					_activeIdx;			// AI 목록 내 위치
	uint64					_nextThinkMs;		// 다음 사고 시각 (존 논리 시간)
	uint64					_targetId;			// 현재 추적/공격 대상 (0이면 없음)
	Vec2					_lastTargetPos;		// 마지막으로 이동 명령을 내릴 때의 대상 위치 (재경로 판단용)
	SAggroEntry				_aggro[kMaxAggro];	// 어그로 표
	size_t					_aggroCount;		// 어그로 표 항목 수
};

#endif // ndef MONSTER_H