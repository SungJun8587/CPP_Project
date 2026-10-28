
//***************************************************************************
// FileServerOptions.h : CFileServerMain::Start()에 넘기는 구동 옵션
//
//***************************************************************************

#ifndef FILESERVEROPTIONS_H
#define FILESERVEROPTIONS_H

#include <ServerConnectInfo.h>

#include <string>

//***************************************************************************
// @struct SFileServerOptions
// @brief 파일 서버 구동 옵션. 값마다 기본값이 있어 필요한 것만 바꿔 쓸 수 있다.
// @details 설정 파일 값은 CFileServerConfig::ToOptions()가 이 구조체로
//          옮겨 담는다. Redis 노드 목록은 기본값이 없으므로 호출부가 채워야 한다.
//***************************************************************************
struct SFileServerOptions
{
	// ── 클라이언트 접속 ──────────────────────────────────
	_tstring			bindIp = _T("0.0.0.0");
	uint16				bindPort = 8081;
	int32				maxSessionCount = 500;				// 동시 접속 상한(초과 접속은 거절)
	uint32				workerThreadCount = 0;				// IOCP 워커 스레드 수. 0 = 자동
	uint32				idleTimeoutMs = 60 * 1000;			// 이 시간 동안 송수신 완료가 없으면 연결을 끊음. 0 = 끄기
	// (큰 파일을 올리거나 받는 중에는 I/O 완료가 계속 들어와 걸리지 않음)

// ── Redis (업로드 토큰 검증 / 삭제 예약 큐) ───────────
	CVector<CRedisNode>	redisNodes;							// 필수
	int32				redisPoolSize = 4;					// 노드별 커넥션 풀 크기

	// ── 파일 저장 ────────────────────────────────────────
	_tstring			storageDir = _T("ProfileImages");	// 업로드 파일을 저장할 로컬 경로(하위 ".tmp"는 업로드 임시 파일용)
	std::string			publicBaseUrl;						// 클라이언트에게 돌려줄 URL의 기본 주소(예: "http://192.168.0.10:8081")
	int64				maxUploadBytes = 1024LL * 1024 * 1024;	// 업로드 가능한 최대 파일 크기(바이트)
};

#endif // ndef FILESERVEROPTIONS_H