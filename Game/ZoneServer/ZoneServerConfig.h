
//***************************************************************************
// ZoneServerConfig.h : interface for the CZoneServerConfig class.
//
//***************************************************************************

#ifndef ZONESERVERCONFIG_H
#define ZONESERVERCONFIG_H

#include "ServerConfig.h"

#include "ZoneServerOptions.h"

//***************************************************************************
// @class CZoneServerConfig
// @brief 존 서버 설정 클래스. JSON 설정 파일을 읽어 SZoneServerOptions로 변환한다.
// @details
// 베이스(CServerConfig)의 필드 대응:
//     Name            -> 서버 이름(로그/모니터링용)
//     WorkerThreadCnt -> Gateway 연결용 IOCP 워커 수
//     ServerNode      -> 접속할 Gateway 목록. 첫 번째 원소가 Gateway의 존 서버용 포트(IP/Port)다.
// 존 시뮬레이션 값은 이 클래스 고유 필드다.
//     ZoneId / ZoneKind / Capacity / TickIntervalMs
//     MinX / MinY / MaxX / MaxY / CellSize / SpawnX / SpawnY / PlayerSpeed
//     PlayerMaxHp / PlayerAttack / PlayerDefense / AttackRange / AttackIntervalMs / RespawnDelayMs / RespawnInvulnMs / AllowPvp
//     GameDataDir / MonsterThinkIntervalMs  (몬스터/스폰 데이터 폴더와 AI 사고 주기)
// AttackRange는 (사거리 + 위치 허용 오차 + 이동 지연 보정)이 AOI 최소 보장 시야 이내여야 하며, CZoneServerMain::Start()가 구동 시 검사한다.
// 이 서버는 리스너가 없으므로 베이스의 IP/Port/MaxSessionCount는 읽지 않는다.
// 키가 없으면 SZoneServerOptions의 기본값을 유지한다.
// Save()는 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를 보존한 채
// 알고 있는 필드의 값만 교체한다.
//***************************************************************************
class CZoneServerConfig : public CServerConfig
{
public:
	CZoneServerConfig();
	virtual ~CZoneServerConfig() {}

	bool Init(const TCHAR* tszConfigPath);
	bool Save(const TCHAR* tszConfigPath) const;
	void PrintServerSettingInfo() const;

	//***************************************************************************
	// @brief 로드된 설정을 CZoneServerMain::Start()에 넘길 옵션으로 변환합니다.
	//***************************************************************************
	void ToOptions(SZoneServerOptions& options) const;

private:
	uint32		_nZoneId;
	uint32		_nZoneKind;			// EZoneKind (1 = Field, 2 = Dungeon)
	uint32		_nCapacity;
	uint32		_nTickIntervalMs;
	float		_fMinX;
	float		_fMinY;
	float		_fMaxX;
	float		_fMaxY;
	float		_fCellSize;
	float		_fSpawnX;
	float		_fSpawnY;
	float		_fPlayerSpeed;
	int32		_nPlayerMaxHp;
	int32		_nPlayerAttack;
	int32		_nPlayerDefense;
	float		_fAttackRange;
	uint32		_nAttackIntervalMs;
	uint32		_nRespawnDelayMs;
	uint32		_nRespawnInvulnMs;
	bool		_bAllowPvp;
	_tstring	_strGameDataDir;
	uint32		_nMonsterThinkIntervalMs;
};

#endif // ndef ZONESERVERCONFIG_H