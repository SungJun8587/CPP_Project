
//***************************************************************************
// ChatServerOptions.h : CChatServerMain::Start()에 넘기는 구동 옵션
//
//***************************************************************************

#ifndef CHATSERVEROPTIONS_H
#define CHATSERVEROPTIONS_H

#include <ServerConnectInfo.h>

#include <string>

//***************************************************************************
// @struct SChatServerOptions
// @brief 채팅 서버 구동 옵션. 값마다 기본값이 있어 필요한 것만 바꿔 쓸 수 있다.
// @details 설정 파일 값은 CChatServerConfig::ToOptions()가 이 구조체로
//          옮겨 담는다. Redis/DB 노드 목록은 기본값이 없으므로 호출부가 채워야 한다.
//***************************************************************************
struct SChatServerOptions
{
	// ── 서버 식별 (하트비트 등록에 쓰임) ─────────────────
	std::string			serverName = "ChatServer";
	std::string			serverGroupId = "1";
	std::string			serverChannelId = "1";

	// ── 클라이언트 접속 ──────────────────────────────────
	_tstring			bindIp = _T("0.0.0.0");
	uint16				bindPort = 30201;
	int32				maxSessionCount = 1000;				// 동시 접속 상한(초과 접속은 거절)
	uint32				workerThreadCount = 0;				// IOCP 워커 스레드 수. 0 = 자동
	uint32				idleTimeoutMs = 0;					// 이 시간 동안 송수신이 없으면 연결을 끊음. 0 = 끄기
	// (채팅 클라이언트는 하트비트를 보내지 않아 조용히 읽기만 하는 정상 사용자가 끊길 수 있음)

// ── Redis (로그인 상태 / 하트비트 / 방 대화 기록) ─────
	CVector<CRedisNode>	redisNodes;							// 필수
	int32				redisPoolSize = 8;					// 노드별 커넥션 풀 크기

	// ── 회원 DB ──────────────────────────────────────────
	CVector<CDBNode>	dbNodes;							// 필수
	int32				dbWorkerThreadCount = 4;			// DB 비동기 워커 스레드 수

	// ── 서버 생존 감시 ───────────────────────────────────
	int32				heartbeatTtlSec = 15;				// 이 시간 안에 갱신하지 못하면 Redis에서 서버/로그인 키가 사라짐
	int32				heartbeatIntervalSec = 5;			// 하트비트 및 로그인 키 갱신 주기

	// ── 파일 서버 연동 ───────────────────────────────────
	std::string			fileServerUrl;						// 클라이언트에게 알려줄 파일 서버 주소. 비우면 업로드 기능 사용 불가

	// ── 방 ───────────────────────────────────────────────
	int32				maxRoomsPerOwner = 0;				// 1인당 생성 가능한 방 개수. 0 이하 = 무제한
};

#endif // ndef CHATSERVEROPTIONS_H