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

	// [추가] Ctrl+C 등으로 종료가 요청됐음을 메인 스레드에 알리는 플래그.
	// ConsoleCtrlHandler는 별도 스레드 컨텍스트에서 호출되므로 atomic으로
	// 둔다. 이게 없으면 GServer->Stop()은 호출되지만 main()의 무한루프를
	// 빠져나올 방법이 없어, 그 아래 MainClose()/CloseConsole()이 영원히
	// 실행되지 않는 죽은 코드가 된다.
	std::atomic<bool> g_bShouldExit{ false };

	//***************************************************************************
	// @brief 콘솔 Ctrl+C / 창 닫기 등을 감지해 정상 종료(Stop())를 유도합니다.
	//***************************************************************************
	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		switch( ctrlType )
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			if( GServer != nullptr )
				GServer->Stop();
			// [추가] Stop() 호출 뒤 메인 스레드가 대기 루프를 빠져나오도록 신호.
			g_bShouldExit.store(true);
			return TRUE;
		default:
			return FALSE;
		}
	}
	//***************************************************************************
	// @brief TCHAR 문자열을 std::string으로 변환함(UNICODE 빌드 대응).
	//***************************************************************************
	std::string TCharToString(const TCHAR* ptsz)
	{
		if( ptsz == nullptr ) return std::string();
		return TStringToString(ptsz);
	}
}

//***************************************************************************
// @brief 프로세스 종료 직전 정리 — 실패 경로/정상 종료 경로가 공유한다.
// @details 순서가 중요: DB 비동기 서비스(워커 스레드 보유)를 먼저 멈추고,
//          그 다음 설정 싱글턴/전역 프레임워크를 정리한다.
//***************************************************************************
void MainClose()
{
	// 1. DB 비동기 서비스(멤버/게임/로그) 종료 — Stop()을 전부 먼저 보내고
	//    Join()을 전부 기다리는 순서로 처리되므로, 아직 실행 중인 워커
	//    스레드가 이후 정리되는 다른 전역 자원을 참조하는 사고를 막는다.
	CDbServiceManager::Instance().ShutdownAll();

	// 2. 서버 설정 싱글턴 해제
	SERVER_CONFIG->ReleaseInstance();

	// 3. 전역 프레임워크(메모리 풀 등) 정리
	BaseGlobal::Destroy();
}

int main()
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 및 기본 프레임워크 초기화
	InitUtf8Console();

	// 2-1. 서버 인스턴스 생성 + 콘솔 종료 시그널(Ctrl+C 등) 핸들러 등록
	//      — GServer 포인터를 먼저 세팅해둬야 핸들러가 실제로 Stop()을
	//      호출할 대상을 찾을 수 있다.
	CChatServerMain server;
	GServer = &server;
	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 2-2. 전역 프레임워크(메모리 풀 등) 초기화 — 이후 모든 단계가
	//      이 초기화가 끝났다는 전제로 동작한다.
	BaseGlobal::Init();

	// 3. 서버 설정 파일(JSON) 로드
	// 3-1. 설정 파일 경로 지정
	TCHAR tszConfigPath[FULLPATH_STRLEN];
	_sntprintf_s(tszConfigPath, FULLPATH_STRLEN, _TRUNCATE, _T("..\\Config\\server_config_mysql.json"));

	// 3-2. 설정 파일 파싱 — 실패 시 여기까지 초기화된 자원(BaseGlobal 등)을
	//      MainClose()로 정리하고 종료
	if( false == SERVER_CONFIG->Init(tszConfigPath) )
	{
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	// 3-3. 필수 설정값 검증 — DB 노드 목록이 비어있으면 서버가 의미 있게
	//      동작할 수 없으므로 여기서 조기에 걸러낸다.
	const auto& dbNodeVec = SERVER_CONFIG->GetDBNodeVec();
	if( dbNodeVec.empty() )
	{
		LOG_ERROR(_T("DBNode configuration is empty. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	// 3-4. 로드된 설정 정보를 로그로 출력(운영 중 확인용)
	SERVER_CONFIG->PrintServerSettingInfo();

	// 4. 서버 시작
	// 4-1. CChatServerMain::Start() 호출 — IOCP/Redis/DB/하트비트를 전부
	//      이 한 번의 호출 안에서 순서대로 초기화한다. Redis/DB 풀 크기,
	//      DB 워커 스레드 수, 하트비트 TTL/주기는 전부 CServerConfig의
	//      JSON 스키마에 이미 있는 값을 그대로 쓴다(더 이상 데모용
	//      상수로 고정할 필요 없음 — 서버 재시작 없이 설정 파일만
	//      바꿔 튜닝 가능).
	const bool started = server.Start(
		SERVER_CONFIG->GetServerIP(), SERVER_CONFIG->GetServerPort(),
		SERVER_CONFIG->GetRedisNodeVec(), SERVER_CONFIG->GetRedisPoolSize(),
		SERVER_CONFIG->GetDBNodeVec(), SERVER_CONFIG->GetDbWorkerThreadCnt(),
		TCharToString(SERVER_CONFIG->GetServiceName()), TCharToString(SERVER_CONFIG->GetServerName()),
		SERVER_CONFIG->GetMaxSessionCount(), SERVER_CONFIG->GetWorkerThreadCnt(),
		SERVER_CONFIG->GetHeartbeatTtlSec(), SERVER_CONFIG->GetHeartbeatIntervalSec()
	);

	// 4-2. 시작 실패 시 정리 후 종료
	if( !started )
	{
		LOG_ERROR(_T("CChatServerMain::Start Fail."));
		MainClose();
		return -1;
	}

	std::cout << "ChatServer started. Press Ctrl+C to stop." << std::endl;

	// 5. 메인 스레드 대기 루프 — 실제 I/O는 IOCP 워커 스레드들이 처리한다.
	// Ctrl+C 등은 ConsoleCtrlHandler가 별도 스레드 컨텍스트에서
	// GServer->Stop() + g_bShouldExit 세팅을 하므로, 이 루프는 그 플래그를
	// 확인해 정상적으로 빠져나온다.
	while( !g_bShouldExit.load() )
		std::this_thread::sleep_for(std::chrono::seconds(1));

	// 6. 정상 종료 경로 — g_bShouldExit이 세팅되어 루프를 빠져나온 뒤
	// 실행된다. GServer->Stop()은 이미 ConsoleCtrlHandler에서 호출됐으므로
	// (CChatServerMain::~CChatServerMain()도 Stop()을 다시 호출하지만
	// 이미 정지된 상태에서는 안전하게 no-op에 가까움) 여기서는 DB
	// 서비스/설정/전역 프레임워크만 정리하면 된다.
	MainClose();
	CloseConsole();
	return 0;
}