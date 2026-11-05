
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
#include "HeroData.h"
#include "Monster.h"
#include "MonsterData.h"

class CBossSystem;

//***************************************************************************
// @struct SMonsterSystemConfig
// @brief 몬스터 AI 튜닝값.
//
// - thinkIntervalMs : 몬스터 한 마리의 사고 주기(ms). 틱 간격 이상이어야 의미가 있다. 작을수록 반응이 빠르고 비용이 는다
// - repathDistance  : 추적 중 대상이 이만큼 움직여야 이동 명령을 다시 내린다(이동 브로드캐스트 억제)
// - engageRatio     : 사거리의 이 비율 안에 들어오면 이동을 멈추고 공격한다 (0 초과 1 이하)
// - pathMaxNodes    : 우회 경로탐색 한 번에 펼칠 수 있는 셀 수 상한. 도달 불가 대상에서의 비용 상한이다
// - pathSearchesPerTick : 한 틱에 할 수 있는 경로탐색 횟수. 많은 몬스터가 동시에 탐색해 틱이 튀는 것을 막는다
// - pathRetryMs     : 경로를 못 찾은 몬스터가 다시 탐색하기까지, 닿을 수 없는 대상을 포기한 뒤 다시 선공하기까지 기다리는 시간(ms)
// - maxMonsters     : 이 존에 스폰할 수 있는 몬스터 총 마릿수 상한. 스폰 테이블 합계가 넘으면 Validate()가 거부한다.
//                     존 한 개의 틱 비용은 마릿수에 비례하므로 서버 용량에 맞춰 운영 설정으로 정한다.
//                     CMonsterData::kMaxSpawnPerZone(데이터 자체의 절대 상한)을 넘는 값은 의미가 없다
//***************************************************************************
struct SMonsterSystemConfig
{
	uint32	thinkIntervalMs = 100;		// 사고 주기(ms)
	float	repathDistance = 1.5f;		// 재경로 거리
	float	engageRatio = 0.9f;			// 교전 거리 비율
	uint32	pathMaxNodes = 8192;		// 경로탐색 셀 수 상한 (CBlockMap::kDefaultPathNodes)
	uint32	pathSearchesPerTick = 4;	// 틱(Update 호출) 한 번에 할 수 있는 경로탐색 수. 넘으면 다음 사고로 미룬다
	uint32	pathRetryMs = 2000;			// 경로 없음 뒤 재탐색 대기(ms)
	uint32	maxMonsters = static_cast<uint32>(CMonsterData::kMaxSpawnPerZone);	// 존당 몬스터 총 마릿수 상한
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

	// 보스 시스템을 붙입니다 (nullptr이면 떼어냄). 보스 시스템은 이 객체보다 오래 살거나 존 파괴 전에 떼어야 한다. 존 구동 전에 한 번 호출한다.
	void SetBossSystem(CBossSystem* boss) { _boss = boss; }

	// 몬스터 스킬(SMonsterTemplate::skillId)을 찾을 스킬 데이터를 붙입니다. 스킬을 쓰는 몬스터가 있으면 존 구동 전에 반드시 호출한다(Validate가 확인).
	void SetSkillData(std::shared_ptr<const CHeroData> skills) { _skills = std::move(skills); }
	const std::shared_ptr<const CMonsterData>& GetData() const { return _data; }
	CMonster* SpawnSummon(uint32 monsterId, const Vec2& center, float spreadRadius, uint64 ownerId, uint64 threatTargetId);

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
	bool StepToward(CMonster& monster, const Vec2& goal);
	void GiveUpTarget(CMonster& monster, CZoneEntity& target, uint64 nowMs);
	CZoneEntity* PickTarget(CMonster& monster);
	CZoneEntity* FindAggroCandidate(CMonster& monster);
	void AlertAllies(CMonster& source, uint64 targetId);
	bool TryUseSkill(CMonster& monster, CZoneEntity& target, uint64 nowMs);
	Vec2 RandomPointInDisc(const Vec2& center, float radius) const;
	Vec2 PickFreePoint(const Vec2& center, float radius) const;

	CFieldZoneBase& _zone;			// 대상 존
	std::shared_ptr<const CMonsterData>	_data;			// 몬스터 데이터 (템플릿 수명 보장)
	SMonsterSystemConfig				_config;		// AI 튜닝값
	mutable std::mt19937_64				_rng;			// 스폰/부활 위치용 난수 (존 ID로 시드해 재현 가능)
	uint64								_serial;		// 몬스터 ID 발급 번호
	size_t								_monsterCount;	// 존에 있는 AI 몬스터 수
	std::vector<CMonster*>				_active;		// AI 활성 몬스터 목록
	uint32								_pathBudget = 0;	// 이번 틱에 남은 경로탐색 횟수
	CBossSystem* _boss = nullptr;	// 보스 시스템 (없으면 일반 몬스터만)
	std::shared_ptr<const CHeroData>	_skills;		// 스킬 데이터 (몬스터 스킬 조회용. 없으면 몬스터 스킬 없음)
	std::vector<Vec2>					_pathScratch;	// 경로탐색 결과 작업 공간 (존 스레드 전용)
};

#endif // ndef MONSTERSYSTEM_H