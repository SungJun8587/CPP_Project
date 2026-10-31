
//***************************************************************************
// ZoneServerOptions.h : CZoneServerMain::Start()에 넘기는 구동 옵션
//
//***************************************************************************

#ifndef UC_ZONESERVEROPTIONS_H
#define UC_ZONESERVEROPTIONS_H

#include <Network/NetworkCommon.h>
#include "../GameCommon/ZoneCommon/ZonePacketTypes.h"		

#include <string>

//***************************************************************************
// @struct SZoneServerOptions
// @brief 존 서버 프로세스 설정값. 값마다 기본값이 있어 필요한 것만 바꿔 쓸 수 있다.
// @detail 설정 파일 값은 CZoneServerConfig::ToOptions()가 이 구조체로 옮겨 담는다.
//***************************************************************************
struct SZoneServerOptions
{
	// ── 존 식별/수용 ─────────────────────────────────────
	uint32		zoneId = 1;						// 이 프로세스가 호스팅하는 존 ID (Gateway 전체에서 유일)
	EZoneKind	kind = EZoneKind::Field;		// 필드/던전
	uint32		capacity = 500;					// 최대 수용 인원 (Gateway가 이 값으로 입장을 제한)
	std::string	serverName = "ZoneServer";		// 로그/모니터링용 이름

	// ── Gateway 접속 ─────────────────────────────────────
	_tstring	gatewayIp = _T("127.0.0.1");	// Gateway의 존 서버용 포트 주소
	uint16		gatewayPort = 7100;
	uint32		workerThreadCount = 1;			// 서버 간 연결 1개용이므로 1~2개면 충분

	// ── 존 시뮬레이션 ────────────────────────────────────
	uint32		tickIntervalMs = 50;
	float		minX = 0.0f;
	float		minY = 0.0f;
	float		maxX = 1000.0f;
	float		maxY = 1000.0f;
	float		cellSize = 50.0f;				// AOI 셀 크기 (최소 보장 시야 반경 이상)
	float		spawnX = 100.0f;
	float		spawnY = 100.0f;
	float		playerSpeed = 6.0f;				// units/sec

	// ── 전투 ─────────────────────────────────────────────
	int32		playerMaxHp = 100;				// 플레이어 최대 체력
	int32		playerAttack = 10;				// 플레이어 공격력
	int32		playerDefense = 0;				// 플레이어 방어력
	float		attackRange = 2.0f;				// 기본 공격 사거리 (units)
	uint32		attackIntervalMs = 1000;		// 기본 공격 최소 간격 (ms)
	uint32		respawnDelayMs = 5000;			// 사망 후 자동 리스폰까지의 시간 (ms)
	bool		allowPvp = true;				// 플레이어끼리 공격 허용 여부
	uint32		respawnInvulnMs = 3000;			// 리스폰 직후 무적 시간 (ms). 스폰 캠핑 방지, 본인이 공격하면 해제

	// ── 게임 데이터 / 몬스터 ─────────────────────────────
	_tstring	gameDataDir = _T("GameData");	// 게임 데이터(JSON) 폴더. 비어 있으면 데이터 없이 구동(몬스터 없음)
	uint32		monsterThinkIntervalMs = 100;	// 몬스터 AI 사고 주기 (ms). 틱 간격 이상으로 두는 것이 의미 있다
};

#endif // ndef UC_ZONESERVEROPTIONS_H