//***************************************************************************
// BotClientOptions.h : BotClient 구동 옵션
//
//***************************************************************************

#ifndef BOTCLIENTOPTIONS_H
#define BOTCLIENTOPTIONS_H

//***************************************************************************
// @struct SBotClientOptions
// @brief 봇 클라이언트 구동 옵션. 우선순위: 명령행 인자 > JSON 설정 파일 > 이 구조체의 기본값
//***************************************************************************
struct SBotClientOptions
{
	_tstring	host = _T("127.0.0.1");	// Gateway 클라이언트 포트 주소
	uint16		port = 7000;			// Gateway 클라이언트 포트
	int32		botCount = 10;			// 동시 접속 봇 수
	int32		zoneId = 1;				// 입장할 존 ID
	_tstring	heroes = _T("0");		// 영웅 ID 목록(쉼표 구분). 봇 번호 순서대로 돌려 쓴다. 0 = 서버 기본 영웅
	int32		charIdBase = 100000;	// 첫 봇의 캐릭터 ID. 봇 i는 base + i (몬스터 ID 대역인 최상위 비트를 쓰지 말 것)
	int32		rampMs = 20;			// 봇 사이 로그인 간격(ms)
	int32		tickMs = 50;			// 봇 행동 틱 주기(ms)
	int32		moveIntervalMs = 300;	// 이동 요청 최소 간격(ms)
	int32		attackIntervalMs = 400;	// 공격 요청 간격(ms)
	int32		workerCount = 2;		// IOCP 워커 스레드 수
	int32		statsSec = 5;			// 통계 출력 주기(초)
	int32		durationSec = 0;		// 실행 시간(초). 0이면 Ctrl+C까지
};

#endif // ndef BOTCLIENTOPTIONS_H