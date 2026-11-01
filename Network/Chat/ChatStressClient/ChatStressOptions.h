
//***************************************************************************
// ChatStressOptions.h : 스트레스 클라이언트 실행 옵션
//
//***************************************************************************

#ifndef CHATSTRESSOPTIONS_H
#define CHATSTRESSOPTIONS_H

#include <string>

//***************************************************************************
// @struct SChatStressOptions
// @brief IOCP 스트레스 테스트 클라이언트의 실행 옵션. 값마다 기본값이 있어 필요한 것만 바꿔 쓸 수 있다.
// @details 설정 파일 값은 CChatStressConfig::ToOptions()가 이 구조체로 옮겨 담고,
//          명령행 인자(ParseArgs)가 그 위에 덮어쓴다(명령행 > 설정 파일 > 기본값).
//          실행 중에는 읽기 전용으로 쓰며, 전역 인스턴스 g_cfg 하나만 사용한다.
//***************************************************************************
struct SChatStressOptions
{
	// ── 대상 서버 ────────────────────────────────────────
	_tstring			host = _T("127.0.0.1");				// 접속할 서버 주소
	uint16				port = 9000;						// 접속할 서버 포트

	// ── 부하 규모 ────────────────────────────────────────
	int					clients = 100;						// 가상 클라이언트(=CChatClientSession과 같은 종류의 IOCP 세션) 수
	int					workers = 0;						// IOCP 워커 스레드 수. 0 = 자동(하드웨어 코어 수)
	int					schedThreads = 2;					// 주기 송신(채팅/목록/…)을 돌리는 스케줄러 스레드 수
	int					durationSec = 60;					// 테스트 시간(초). 0 = 무한(Ctrl+C)
	double				rampPerSec = 50;					// 초당 신규 접속 수. 0 이하이면 Start() 한 번에 전부 접속(burst)

	// ── 송신 주기 ────────────────────────────────────────
	int					chatIntervalMs = 1000;				// 0 = 채팅 안 함
	int					listIntervalMs = 5000;				// 0 = 방 목록 조회 안 함
	int					memberIntervalMs = 0;				// 0 = 방 멤버 목록 조회 안 함(방 입장 시에만 의미)
	int					countIntervalMs = 0;				// 0 = 동접자 조회 안 함

	// ── 방 / 메시지 ──────────────────────────────────────
	int					roomId = 0;							// 0 = 로비 유지, >0 = 로그인 후 그 방 입장
	int					roomSpread = 1;						// >1이면 roomId..roomId+spread-1에 round-robin 분산
	int					msgBytes = 32;						// 채팅 본문 길이
	int					pageSize = 20;						// 목록 조회 요청의 pageSize

	// ── 계정 ─────────────────────────────────────────────
	std::string			prefix = "S";						// 신규 가입 닉네임 접두어(닉네임 = prefix + 태그 + 번호, 16자 이내, ASCII)
	_tstring			accountsFile;						// 지정하면 계정(public_id+token)을 저장/재사용 -> 재실행 시 신규 가입 대신 재접속
};

#endif // ndef CHATSTRESSOPTIONS_H