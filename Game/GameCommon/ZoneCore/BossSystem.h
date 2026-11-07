
//***************************************************************************
// BossSystem.h : interface for the CBossSystem class.
//
//***************************************************************************

#ifndef BOSSSYSTEM_H
#define BOSSSYSTEM_H

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "BossData.h"
#include "FieldZoneBase.h"
#include "HeroData.h"
#include "Monster.h"
#include "MonsterData.h"

class CMonsterSystem;

//***************************************************************************
// @struct SBossRuntime
// @brief 보스 개체 하나의 전투 진행 상태. 템플릿(불변)은 가리키기만 하고 개체별 상태만 가진다.
//***************************************************************************
struct SBossRuntime
{
	const SBossTemplate*	tmpl = nullptr;			// 보스 정의
	size_t					phaseIdx = 0;			// 현재 페이즈 위치 (tmpl->phases 인덱스)
	bool					engaged = false;		// 전투 중인지 (첫 추적 사고에서 켜지고, 복귀/사망/부활에서 꺼짐)
	bool					enraged = false;		// 격노가 걸렸는지
	uint64					engagedAtMs = 0;		// 전투를 시작한 존 논리 시각
	size_t					actionIdx = 0;			// 현재 페이즈에서 다음에 할 행동 위치
	uint64					nextActionMs = 0;		// 이 시각 전에는 행동하지 않는다
	uint64					blockedSinceMs = 0;		// 현재 행동이 조건(대상/사거리/재사용)을 못 채워 기다리기 시작한 시각 (0이면 막히지 않음)
	std::unordered_map<uint32, uint64>	readyAtMs;	// 스킬 ID -> 다시 쓸 수 있는 시각
	std::vector<uint64>		adds;					// 이 보스가 소환한 몬스터의 엔티티 ID
};

//***************************************************************************
// @class CBossSystem
// @brief 데이터(Boss/BossPhase/BossAction)로 정의한 보스 전투를 구동하는 시스템. CMonsterSystem 위에 얹는다.
// @details
// 보스는 평범한 몬스터(CMonster)이며, 템플릿 ID가 보스 데이터에 있으면 보스 규칙이 추가로 적용된다.
// 추적/기본 공격/어그로/복귀는 CMonsterSystem이 그대로 하고, 이 시스템은 추적 사고마다 다음을 한다.
//   1) 격노: 전투 시작 후 정해진 시간이 지나면 상태 효과를 건다
//   2) 페이즈: 체력이 다음 페이즈의 기준 % 이하가 되면 한 사고에 한 단계씩 올라간다 (상태 효과, 회복, 첫 행동 지연)
//   3) 행동: 때가 된 행동을 하나 실행한다 (스킬 시전 / 소환 / 어그로 교체). 조건이 안 맞으면 kActionTimeoutMs까지 기다린 뒤 건너뛴다
// 시전 시간이 있는 스킬을 쓰는 동안 보스는 멈추고 기본 공격도 하지 않는다. 범위 스킬의 중심은 시전 시작 때 정해지므로
// 플레이어가 피할 수 있다. 시전 시작/취소/적중 알림은 기존 스킬 패킷을 그대로 쓰므로 클라이언트 프로토콜 변경이 없다.
// 보스가 복귀하거나 죽거나 부활하면 페이즈/격노/소환수가 초기화된다.
// 모든 메서드는 존 스레드에서만 호출한다.
//***************************************************************************
class CBossSystem
{
public:
	static constexpr uint32 kActionTimeoutMs = 5000;		// 행동 조건을 못 채울 때 기다리는 기본 최대 시간. 지나면 그 행동을 건너뛴다
	static constexpr uint32 kAddCorpseMs = 3000;			// 소환수가 죽은 뒤 시체가 남는 시간 (CMonsterSystem이 사용)
	static constexpr uint32 kAggroBoostPercent = 125;		// Aggro 행동이 대상의 위협을 올리는 목표 (현재 최대 위협의 %)

	CBossSystem(CFieldZoneBase& zone, CMonsterSystem& monsters, std::shared_ptr<const CBossData> data,
		std::shared_ptr<const CHeroData> skills, std::shared_ptr<const CMonsterData> monsterData);

	bool Validate(std::string& error, float opponentMaxSpeed = 0.0f) const;

	// 행동 조건(대상/사거리/재사용)이 안 맞을 때 기다리는 최대 시간을 바꿉니다. 존 구동 전에 호출한다. (기본 kActionTimeoutMs)
	void SetActionTimeoutMs(uint32 ms) { _actionTimeoutMs = ms; }

	// ── CMonsterSystem이 호출하는 훅 ─────────────────────
	void OnSpawned(CMonster& monster);
	bool OnChase(CMonster& monster, CZoneEntity& target, uint64 nowMs);
	void OnReset(CMonster& monster);
	void OnDied(CMonster& monster);
	void OnRemoved(CMonster& monster);

	// ── 조회 (테스트/운영 확인용) ────────────────────────
	size_t GetBossCount() const { return _runtimes.size(); }
	const SBossRuntime* Find(uint64 entityId) const;

private:
	enum class EOutcome : uint8 { Done, Retry, Blocked };		// 행동 시도 결과

	void StartPhase(SBossRuntime& rt, CMonster& boss, size_t phaseIdx, uint64 nowMs);
	void CheckEnrage(SBossRuntime& rt, CMonster& boss, uint64 nowMs);
	void CheckPhase(SBossRuntime& rt, CMonster& boss, uint64 nowMs);
	void RunAction(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, uint64 nowMs);
	void Advance(SBossRuntime& rt, const SBossPhaseTemplate& phase, uint64 nowMs, uint32 waitMs);
	EOutcome DoCast(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act, uint64 nowMs, uint32& waitMs);
	void DoSummon(SBossRuntime& rt, CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act);
	void DoAggro(CMonster& boss, CZoneEntity& current, const SBossActionTemplate& act);
	CZoneEntity* PickEntity(CMonster& boss, CZoneEntity& current, EBossTarget rule, float radius);
	void DespawnAdds(SBossRuntime& rt);
	void PruneAdds(SBossRuntime& rt);
	void ResetRuntime(SBossRuntime& rt);

	CFieldZoneBase&						_zone;
	CMonsterSystem&						_monsters;
	std::shared_ptr<const CBossData>	_data;			// 보스 데이터
	std::shared_ptr<const CHeroData>	_skills;		// 스킬/상태 효과 데이터 (영웅과 같은 시트를 쓴다)
	std::shared_ptr<const CMonsterData>	_monsterData;
	std::unordered_map<uint64, std::unique_ptr<SBossRuntime>>	_runtimes;	// 엔티티 ID -> 보스 상태 (포인터가 CMonster에 저장되므로 박스로 안정화)
	uint32								_actionTimeoutMs = kActionTimeoutMs;
	std::mt19937_64						_rng;
	std::vector<CZoneEntity*>			_candidates;	// 대상 후보 작업 공간 (존 스레드 전용)
};

#endif // ndef BOSSSYSTEM_H
