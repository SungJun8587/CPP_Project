
//***************************************************************************
// BossData.h : 보스 전투 데이터 (서버 구동 시 로드되는 불변 데이터)
//
//***************************************************************************

#ifndef BOSSDATA_H
#define BOSSDATA_H

#include <BaseRedefineDataType.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Generated/Enums.gen.h"
#include "../Generated/Consts.gen.h"

class CMonsterData;
class CHeroData;

using GameDataEnums::EBossActionType;
using GameDataEnums::EBossTarget;
using GameDataEnums::EBossTrigger;

constexpr uint32 kMaxBossAdds = GameDataConsts::kMaxBossAdds;							// 보스 한 마리가 동시에 데리고 있을 수 있는 소환 몬스터 수 (Const 시트)
constexpr uint32 kMaxBossActionsPerPhase = GameDataConsts::kMaxBossActionsPerPhase;		// 페이즈 하나의 행동 수 상한 (Const 시트)
constexpr uint32 kMaxBossPhases = 8;													// 보스 하나의 페이즈 수 상한
constexpr uint32 kMaxBossDelayMs = 600000;												// 행동 뒤 대기 시간 상한
constexpr uint32 kMaxBossTransitionMs = 60000;											// 페이즈 전환 지연 상한
constexpr uint32 kMaxBossEnrageMs = 86400000;											// 격노 시간 상한 (24시간)
constexpr uint32 kMinBossLoopMs = 500;													// 반복 행동 한 바퀴의 최소 길이. 이보다 짧으면 매 틱 행동하는 보스가 된다
constexpr float  kMaxBossSpreadRadius = 30.0f;											// 소환 위치 반경 상한

//***************************************************************************
// @struct SBossActionTemplate
// @brief 보스 행동 하나. 기획 데이터의 BossAction 시트 한 행에 대응한다.
// @details
// - Cast   : skillId의 스킬을 target 쪽으로 시전한다. 대상 후보는 스킬 사거리 안의 플레이어이며 Self는 보스 위치다.
//            시전 시간이 있는 Area 스킬은 시전을 시작할 때 중심이 정해져, 맞는 쪽이 피할 시간을 갖는다.
// - Summon : monsterId 몬스터를 count마리, 보스 주변 spreadRadius 안에 소환한다. 보스의 현재 대상에게 어그로를 둔다.
// - Aggro  : target으로 고른 플레이어를 보스의 어그로 1순위로 올린다. skillId가 있으면 그 사거리를, 없으면 보스 선공 거리를 후보 범위로 쓴다.
// 행동이 끝나면(시전 시간 포함) delayMs 뒤에 다음 행동으로 넘어간다.
//***************************************************************************
struct SBossActionTemplate
{
	uint32			seq = 0;						// 같은 (페이즈, trigger) 안의 실행 순서 (1부터, 중복 불가)
	EBossTrigger	trigger = EBossTrigger::Loop;
	EBossActionType	type = EBossActionType::Cast;
	EBossTarget		target = EBossTarget::Current;
	uint32			skillId = 0;
	uint32			monsterId = 0;
	uint32			count = 0;
	float			spreadRadius = 0.0f;
	uint32			delayMs = 0;
};

//***************************************************************************
// @struct SBossPhaseTemplate
// @brief 보스 페이즈 하나. 기획 데이터의 BossPhase 시트 한 행과 그 페이즈의 BossAction들.
// @details actions는 OnEnter 행동(Seq 순)이 앞에, Loop 행동(Seq 순)이 뒤에 온다. loopStart는 첫 Loop 행동의 위치다.
//***************************************************************************
struct SBossPhaseTemplate
{
	uint32	phase = 0;					// 1부터 빠짐없이
	uint32	hpBelowPercent = 100;		// 체력이 이 % 이하가 되면 진입 (1페이즈는 100)
	uint32	enterStatusId = 0;			// 진입 때 보스에게 거는 Self 상태 효과 (0이면 없음)
	uint32	enterHealPercent = 0;		// 진입 때 최대 체력의 이 %만큼 회복
	uint32	transitionMs = 0;			// 진입 후 첫 행동까지의 지연
	std::vector<SBossActionTemplate>	actions;
	size_t	loopStart = 0;
};

//***************************************************************************
// @struct SBossTemplate
// @brief 보스 한 종류. 기획 데이터의 Boss 시트 한 행과 그 페이즈들.
//***************************************************************************
struct SBossTemplate
{
	uint32			id = 0;					// 보스 ID (유일)
	uint32			monsterId = 0;			// Monster 시트의 몬스터. 이 몬스터는 어디에 스폰되든 보스 규칙을 따른다 (몬스터당 하나)
	std::string		name;
	uint32			enrageAfterMs = 0;		// 전투 시작 후 이 시간이 지나면 격노 (0이면 없음)
	uint32			enrageStatusId = 0;		// 격노 때 보스에게 거는 Self 상태 효과
	std::vector<SBossPhaseTemplate>	phases;	// phase 오름차순, 1개 이상
};

//***************************************************************************
// @class CBossData
// @brief 보스 템플릿 모음. 로드가 끝나면 변경하지 않으며, shared_ptr<const>로 존들이 공유한다.
// @details Validate()는 값 범위와 테이블 간 참조(몬스터, 스킬, 상태 효과)를 검사하며, 통과해야 서버가 구동된다.
//          존의 AOI 시야에 의존하는 검사는 CBossSystem::Validate()가 한다.
//***************************************************************************
class CBossData
{
public:
	bool AddBoss(const SBossTemplate& boss);
	bool Validate(const CMonsterData& monsters, const CHeroData& skills, std::string& error) const;

	//***************************************************************************
	// @brief 몬스터 템플릿 ID로 보스 정의를 찾습니다.
	// @return 보스, 보스가 아니면 nullptr
	//***************************************************************************
	const SBossTemplate* FindByMonster(uint32 monsterId) const
	{
		auto it = _byMonster.find(monsterId);
		return it != _byMonster.end() ? &_bosses[it->second] : nullptr;
	}

	const std::vector<SBossTemplate>& GetBosses() const { return _bosses; }
	size_t GetBossCount() const { return _bosses.size(); }

private:
	std::vector<SBossTemplate>				_bosses;
	std::unordered_map<uint32, size_t>		_byMonster;		// 몬스터 ID -> _bosses 위치
};

#endif // ndef BOSSDATA_H