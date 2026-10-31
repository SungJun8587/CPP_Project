
//***************************************************************************
// GameDataLoader.h : 기획 데이터(JSON) 로더
//
//***************************************************************************

#ifndef UC_GAMEDATALOADER_H
#define UC_GAMEDATALOADER_H

#include <Network/NetworkCommon.h>
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
//
// 모든 필드는 필수이며 타입/범위가 맞지 않으면 어느 행의 어느 필드인지 로그를 남기고 실패한다.
// 테이블 간 참조(스폰 -> 몬스터)와 값 범위는 CMonsterData::Validate()가 검사한다.
// 존의 영역/AOI에 의존하는 검사는 존이 만들어진 뒤 CBattleZone::ValidateMonsters()가 한다.
//***************************************************************************
class CGameDataLoader
{
public:
	static std::shared_ptr<const CMonsterData> LoadMonsters(const _tstring& dir);
};

#endif // ndef UC_GAMEDATALOADER_H
