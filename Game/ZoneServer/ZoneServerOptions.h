
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
};

#endif // ndef UC_ZONESERVEROPTIONS_H