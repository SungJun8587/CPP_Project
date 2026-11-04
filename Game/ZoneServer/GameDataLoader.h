
//***************************************************************************
// GameDataLoader.h : 기획 데이터(JSON) 로더
//
//***************************************************************************

#ifndef UC_GAMEDATALOADER_H
#define UC_GAMEDATALOADER_H

#include <Network/NetworkCommon.h>

#include "../GameCommon/ZoneCore/BlockMap.h"
#include "../GameCommon/ZoneCore/BossData.h"
#include "../GameCommon/ZoneCore/HeroData.h"
#include "../GameCommon/ZoneCore/MonsterData.h"

#include <memory>

//***************************************************************************
// @class CGameDataLoader
// @brief 기획 데이터 폴더(JSON)를 읽어 검증된 불변 서버 데이터로 만든다.
// @details
// 기획자는 엑셀(.xlsx)로 데이터를 관리하고, 빌드/배포 단계에서 Tools/xlsx2json.py가 시트별 JSON으로 변환한다.
// 서버는 엑셀 파서에 의존하지 않고 JSON만 읽는다.
//
//     <dir>/Monster.json : { "Monster": [ { "Id","Name","MaxHp","Attack","Defense","AttackRange","AttackIntervalMs",
//                                           "Speed","AggroRange","LeashRange","RespawnMs" }, ... ] }
//     <dir>/Spawn.json   : { "Spawn":   [ { "ZoneId","MonsterId","Count","X","Y","Radius" }, ... ] }
//     <dir>/Hero.json    : { "Hero":    [ { "Id","Name","MaxHp","Attack","Defense","AttackRange","AttackIntervalMs",
//                                           "Speed" }, ... ] }   (영웅은 1개 이상, 플레이어가 입장할 때 하나를 선택한다)
//     <dir>/Skill.json   : { "Skill":   [ { "Id","Name","Shape","DamagePercent","FlatDamage","Range","Radius","CooldownMs","MaxTargets",
//                                            "CastTimeMs"(선택,0=즉시),"ProjectileSpeed"(선택,0=즉시 적중),"StatusId"(선택,0=없음) }, ... ] }   (선택)
//     <dir>/Status.json  : { "Status":  [ { "Id","Name","Kind"("Dot"/"Buff"),"Target"("Hit"/"Self"),"DurationMs","TickMs","DamagePercent","FlatDamage",
//                                            "AttackPercent","DefensePercent","SpeedPercent" }, ... ] }   (선택. Skill의 StatusId가 참조)
//                          Shape는 "Single"(대상 지정, Radius 0, MaxTargets 1) 또는 "Area"(지정 지점 중심의 원, Radius > 0, MaxTargets 1~32).
//                          피해 = max(최소 피해, 공격력 * DamagePercent / 100 + FlatDamage - 방어력). DamagePercent와 FlatDamage가 모두 0이면 피해 없이 상태 효과만 건다.
//                          CastTimeMs > 0이면 시전 시간이 있고(이동하면 취소), ProjectileSpeed > 0이면 거리/속도만큼 날아간 뒤 착탄 시점에 판정한다.
//                          Status의 Dot은 TickMs마다 공격력 * DamagePercent / 100 + FlatDamage(방어력 무시, 최소 1)를 주고, Buff는 공격/방어/이동속도를 %로 바꾼다.
//     <dir>/HeroSkill.json: { "HeroSkill": [ { "HeroId","SkillId" }, ... ] }   (선택. 영웅 한 명당 최대 8개)
//     <dir>/Block.json   : { "Block":   [ { "ZoneId","Type","X","Y","W","H","R" }, ... ] }   (선택. 없으면 장애물 없음)
//                          Type은 "Rect"(X,Y = 왼쪽 아래 모서리, W,H = 크기) 또는 "Circle"(X,Y = 중심, R = 반경).
//                          사용하지 않는 열(Rect의 R, Circle의 W/H)은 0으로 둔다.
//                          엔티티는 점으로 다루므로 장애물은 CBlockMap::kDefaultMargin만큼 사방으로 키워서 적용된다(엔티티 몸 크기 반영).
//     <dir>/Boss.json / BossPhase.json / BossAction.json : 보스 전투 (선택. Boss.json이 없으면 보스 없음. 있으면 세 파일이 모두 있어야 한다)
//                          Boss       : { "Id","MonsterId","Name","EnrageAfterMs"(선택),"EnrageStatusId"(선택) }
//                          BossPhase  : { "BossId","Phase","HpBelowPercent","EnterStatusId"(선택),"EnterHealPercent"(선택),"TransitionMs"(선택) }
//                          BossAction : { "BossId","Phase","Seq","Trigger"("Loop"/"OnEnter"),"Type"("Cast"/"Summon"/"Aggro"),"Target",
//                                         "SkillId"(선택),"MonsterId"(선택),"Count"(선택),"SpreadRadius"(선택),"DelayMs" }
//                          값 범위와 참조(몬스터, 스킬, 상태 효과)는 CBossData::Validate()가 검사한다.
//
// 모든 필드는 필수이며 타입/범위가 맞지 않으면 어느 행의 어느 필드인지 로그를 남기고 실패한다.
// 테이블 간 참조(스폰 -> 몬스터)와 값 범위는 CMonsterData::Validate()가 검사한다.
// 존의 영역/AOI에 의존하는 검사는 존이 만들어진 뒤 CBattleZone::ValidateMonsters()가 한다.
//***************************************************************************
class CGameDataLoader
{
public:
	// 차단 맵 셀 한 변의 길이. 엔티티를 점으로 취급하므로 작을수록 장애물 모양이 정확하다.
	static constexpr float kBlockCellSize = CBlockMap::kDefaultCellSize;

	static std::shared_ptr<const CHeroData> LoadHeroes(const _tstring& dir);
	static std::shared_ptr<const CMonsterData> LoadMonsters(const _tstring& dir);
	static std::shared_ptr<const CBossData> LoadBosses(const _tstring& dir, const CMonsterData& monsters, const CHeroData& skills);
	static bool LoadBlockMap(const _tstring& dir, uint32 zoneId, float minX, float minY, float maxX, float maxY,
		std::shared_ptr<const CBlockMap>& out);
};

#endif // ndef UC_GAMEDATALOADER_H