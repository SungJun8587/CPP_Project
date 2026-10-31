
//***************************************************************************
// MonsterSystem.h : interface for the CMonsterSystem class.
//
//***************************************************************************

#ifndef MONSTERSYSTEM_H
#define MONSTERSYSTEM_H

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "FieldZoneBase.h"
#include "Monster.h"
#include "MonsterData.h"

//***************************************************************************
// @struct SMonsterSystemConfig
// @brief 몬스터 AI 튜닝값.
//
// - thinkIntervalMs : 몬스터 한 마리의 사고 주기(ms). 틱 간격 이상이어야 의미가 있다. 작을수록 반응이 빠르고 비용이 는다
// - repathDistance  : 추적 중 대상이 이만큼 움직여야 이동 명령을 다시 내린다(이동 브로드캐스트 억제)
// - engageRatio     : 사거리의 이 비율 안에 들어오면 이동을 멈추고 공격한다 (0 초과 1 이하)
//***************************************************************************
struct SMonsterSystemConfig
{
	uint32	thinkIntervalMs = 100;		// 사고 주기(ms)
	float	repathDistance = 1.5f;		// 재경로 거리
	float	engageRatio = 0.9f;			// 교전 거리 비율
};

//***************************************************************************
// @class CMonsterSystem
// @brief 필드 존에 몬스터를 스폰하고 AI(대기/추적/공격/복귀)와 어그로를 구동하는 시스템.
//        존(CFieldZoneBase)을 상속하지 않고 참조로 사용하는 구성 요소이므로, 어떤 존 클래스에도 붙일 수 있다.
// @details
// 존 클래스가 연결해야 하는 지점:
//   - 구동 전    : Validate() 후 SpawnAll()
//   - 매 틱      : OnUpdate()에서 이동 갱신 뒤 Update() (이동 갱신 중 호출 금지)
//   - 훅 전달    : OnEntityDamaged -> OnDamaged, OnEntityDied -> OnDied, OnEntityRespawned -> OnRespawned,
//                  OnEntityRemoved -> OnRemoved (엔티티가 제거되기 전에 AI 활성 목록에서 뺀다)
//   - 정책 전달  : GetRespawnDelayMs/GetRespawnPos를 같은 이름의 존 가상 함수에서 위임
// 모든 메서드는 존 스레드에서만 호출한다.
//
// 비용 모델: 시야에 플레이어가 있는 몬스터만 활성 목록에 오르고, 활성 몬스터도 thinkIntervalMs마다 한 번만 사고한다.
// 플레이어가 없는 지역의 몬스터 수천 마리는 틱 비용이 없다.
//***************************************************************************
class CMonsterSystem
{
public:
	CMonsterSystem(CFieldZoneBase& zone, std::shared_ptr<const CMonsterData> data, const SMonsterSystemConfig& config);

	bool Validate(std::string& error, float opponentMaxSpeed = 0.0f) const;
	float GetMaxSpawnSpeed() const;
	uint32 SpawnAll();
	void Update();

	// ── 존 훅 전달 ───────────────────────────────────────
	void OnDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage);
	void OnDied(CZoneEntity& victim);
	void OnRespawned(CZoneEntity& entity);
	void OnRemoved(CZoneEntity& entity);

	// ── 존 정책 위임 ─────────────────────────────────────
	bool IsAiMonster(const CZoneEntity& entity) const;
	uint32 GetRespawnDelayMs(const CZoneEntity& entity) const;
	bool GetRespawnPos(const CZoneEntity& entity, Vec2& out) const;

	// ── CMonster가 호출 ──────────────────────────────────
	void Activate(CMonster& monster);

	size_t GetMonsterCount() const { return _monsterCount; }		// 존에 있는 AI 몬스터 수
	size_t GetActiveCount() const { return _active.size(); }

private:
	void Deactivate(CMonster& monster);
	void Think(CMonster& monster, uint64 nowMs);
	void ThinkIdle(CMonster& monster, uint64 nowMs);
	void ThinkChase(CMonster& monster, uint64 nowMs);
	void ThinkReturn(CMonster& monster);
	void EnterReturn(CMonster& monster);
	CZoneEntity* PickTarget(CMonster& monster);
	CZoneEntity* FindAggroCandidate(CMonster& monster);
	Vec2 RandomPointInDisc(const Vec2& center, float radius) const;
	Vec2 ClampToZone(const Vec2& pos) const;

	CFieldZoneBase& _zone;			// 대상 존
	std::shared_ptr<const CMonsterData>	_data;			// 몬스터 데이터 (템플릿 수명 보장)
	SMonsterSystemConfig				_config;		// AI 튜닝값
	mutable std::mt19937_64				_rng;			// 스폰/부활 위치용 난수 (존 ID로 시드해 재현 가능)
	uint64								_serial;		// 몬스터 ID 발급 번호
	size_t								_monsterCount;	// 존에 있는 AI 몬스터 수
	std::vector<CMonster*>				_active;		// AI 활성 몬스터 목록
};

#endif // ndef MONSTERSYSTEM_H