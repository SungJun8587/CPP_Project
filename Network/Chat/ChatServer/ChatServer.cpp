//***************************************************************************
// ChatServer.cpp : CChatServerMain 구동 예시
//
//***************************************************************************

#include "pch.h"
#include "ChatServerMain.h"
#include <ServerConnectInfo.h>
#include <ServerConfig.h>

#include <iostream>

namespace
{
	CChatServerMain* GServer = nullptr;

	//***************************************************************************
	// @brief 콘솔 Ctrl+C / 창 닫기 등을 감지해 정상 종료(Stop())를 유도합니다.
	// @param ctrlType 감지된 콘솔 제어 이벤트 타입
	// @return BOOL 이벤트 처리 여부 (TRUE: 처리 완료, FALSE: 미처리)
	//***************************************************************************
	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		// 1. 발생한 콘솔 제어 이벤트 종류 확인
		switch( ctrlType )
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			// 1-1. 종료 권한 관련 이벤트 발생 시 서버 객체가 유효하면 Stop() 호출
			if( GServer != nullptr )
				GServer->Stop();
			return TRUE;

		default:
			// 1-2. 처리 대상이 아닌 이벤트는 기본 핸들러로 위임
			return FALSE;
		}
	}

	//***************************************************************************
	// @brief TCHAR 문자열을 std::string으로 변환함(UNICODE 빌드 대응).
	// @param ptsz 변환할 TCHAR 문자열 포인터
	// @return std::string 변환된 멀티바이트/UTF-8 std::string 문자열
	//***************************************************************************
	std::string TCharToString(const TCHAR* ptsz)
	{
		// 1. 입력 포인터 유효성 검사 (null인 경우 빈 문자열 반환)
		if( ptsz == nullptr ) return std::string();

		// 2. TStringToString 헬퍼 함수를 호출하여 문자열 변환 후 반환
		return TStringToString(ptsz);
	}
}

//***************************************************************************
// @brief 서버 종료 시 등록된 DB 서비스, 서버 설정, 글로벌 리소스를 해제합니다.
//***************************************************************************
void MainClose()
{
	// 1. DB 서비스 매니저에 등록된 모든 비동기 DB 서비스 종료 및 정리
	CDbServiceManager::Instance().ShutdownAll();

	// 2. 서버 설정 싱글턴 인스턴스 해제
	SERVER_CONFIG->ReleaseInstance();

	// 3. BaseGlobal 전역 프레임워크 리소스 해제
	BaseGlobal::Destroy();

	// 4. Winsock 라이브러리 정리
	CSocketUtils::Clear();
}

//***************************************************************************
// @brief 채팅 서버 메인 진입점 함수
// @return int 프로세스 종료 코드 (0: 정상 종료, -1: 초기화 실패)
//***************************************************************************
int main()
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 초기화
	InitUtf8Console();

	// 3. 서버 메인 객체 생성 및 콘솔 컨트롤 핸들러 등록
	CChatServerMain server;
	GServer = &server;
	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 4. BaseGlobal 프레임워크 초기화
	BaseGlobal::Init();

	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";

	// 5. 서버 설정(JSON) 파일 경로 생성 및 초기화 시도
	TCHAR tszConfigPath[FULLPATH_STRLEN];

	_sntprintf_s(tszConfigPath, FULLPATH_STRLEN, _TRUNCATE, _T("Config\\server_config_mysql.json"));
	if( false == SERVER_CONFIG->Init(tszConfigPath) )
	{
		// 5-1. 설정 파일 로드 실패 시 에러 로그 출력 및 자원 정리 후 종료
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail. (Path: %s)"), tszConfigPath);

		MainClose();
		return -1;
	}

	// 6. DB 노드 설정 검증
	const auto& dbNodeVec = SERVER_CONFIG->GetDBNodeVec();
	if( dbNodeVec.empty() )
	{
		// 6-1. DB 노드 목록이 비어있으면 에러 로그 출력 및 자원 정리 후 종료
		LOG_ERROR(_T("DBNode configuration is empty. (Path: %s)"), tszConfigPath);

		MainClose();
		return -1;
	}

	// 7. 로드된 서버 설정 정보 콘솔/로그 출력
	SERVER_CONFIG->PrintServerSettingInfo();

	// 8. 데모 및 실행용 상수 설정 (Redis 풀 크기, 워커 스레드 수, 핥비트 상속값 등)
	constexpr int32 kRedisPoolSize = 5;
	constexpr int32 kDbWorkerThreadCnt = 4;
	constexpr int32 kHeartbeatTtlSec = 15;
	constexpr int32 kHeartbeatIntervalSec = 5;

	// 9. 채팅 서버 메인 구동(Start) 시도
	const bool started = server.Start(
		SERVER_CONFIG->GetServerIP(), SERVER_CONFIG->GetServerPort(),
		SERVER_CONFIG->GetRedisNodeVec(), kRedisPoolSize,
		SERVER_CONFIG->GetDBNodeVec(), kDbWorkerThreadCnt,
		TCharToString(SERVER_CONFIG->GetServiceName()), TCharToString(SERVER_CONFIG->GetServerName()),
		1000, 0,						// maxSessionCount, workerThreadCount(자동)
		kHeartbeatTtlSec, kHeartbeatIntervalSec
	);

	if( !started )
	{
		// 9-1. 서버 구동 실패 시 에러 로그 출력 및 자원 정리 후 종료
		LOG_ERROR(_T("CChatServerMain::Start Fail."));

		MainClose();
		return -1;
	}

	// 10. 서버 정상 구동 안내 메시지 출력
	std::cout << "ChatServer started. Press Ctrl+C to stop." << std::endl;

	// 11. 메인 스레드 대기 루프 (ConsoleCtrlHandler를 통한 종료 시그널 전까지 유지)
	while( true )
		std::this_thread::sleep_for(std::chrono::seconds(1));

	// 12. 종료 시 전체 자원 정리 및 콘솔 닫기
	MainClose();
	CloseConsole();

	return 0;
}