
//***************************************************************************
// FileServer.cpp : CFileServerMain 구동 예시
//
// [설계] ChatServer.cpp와 완전히 같은 초기화/종료 골격을 그대로 재사용한다
// (콘솔 종료 핸들러, BaseGlobal/Winsock 초기화 순서, 대기 루프). 다른 점은
// DB(ODBC/MySQL) 계층이 통째로 빠졌다는 것 — 파일 서버는 user_profile_images
// 테이블을 전혀 모른다(그건 채팅 서버 소관).
//***************************************************************************

#include "pch.h"
#include "FileServerMain.h"
#include "FileServerConfig.h"
#include <ServerConnectInfo.h>
#include <Util/MainArgParser.h>
#include <iostream>

namespace
{
	CFileServerMain* GServer = nullptr;

	// [참고] ChatServer.cpp와 동일한 이유 — ConsoleCtrlHandler가 별도 스레드
	// 컨텍스트에서 호출되므로 atomic으로 둔다.
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
			g_bShouldExit.store(true);
			return TRUE;
		default:
			return FALSE;
		}
	}

	//***************************************************************************
	// @brief CFileServerConfig의 싱글톤 포인터에 접근합니다.
	// @return CFileServerConfig* 서버 설정 싱글톤 포인터
	//***************************************************************************
#define FILE_SERVER_CONFIG						CFileServerConfig::GetSingletonPtr()
}

//***************************************************************************
// @brief 프로세스 종료 직전 정리 — 실패 경로/정상 종료 경로가 공유한다.
// @details 채팅 서버(ChatServer.cpp::MainClose())와 달리 DB 비동기 서비스
// 종료 단계가 없다 — 파일 서버는 애초에 DB 계층을 안 갖고 있다.
//***************************************************************************
void MainClose()
{
	// 1. 파일 서버 설정 싱글턴 해제
	FILE_SERVER_CONFIG->ReleaseInstance();

	// 2. 전역 프레임워크(메모리 풀 등) 정리
	BaseGlobal::Destroy();

	// 3. Winsock 라이브러리 자원(WSACleanup)을 해제
	CSocketUtils::Clear();
}

//***************************************************************************
// @brief 명령행 인자 사용법을 콘솔에 출력합니다.
//***************************************************************************
static void PrintUsage()
{
	std::cout <<
		"usage: FileServer [options]\n"
		"  --config <file>       JSON config file (Config\\fileserver_config_mysql.json)\n"
		"  --name <str>          server name\n"
		"  --ip <addr>           bind address\n"
		"  --port <n>            bind port\n"
		"  --max-sessions <n>    max concurrent sessions\n"
		"  --workers <n>         IOCP worker threads, 0=auto\n"
		"  --storage-dir <dir>   directory for uploaded files\n"
		"  --public-url <url>    public base URL handed to clients (e.g. http://host:8081)\n"
		"  --max-upload-bytes <n> max upload size in bytes\n"
		"options given here override the config file.\n";
}

//***************************************************************************
// @brief FileServer 진입점
// @param argc 명령행 인자 개수
// @param argv 명령행 인자. "--key value" 또는 "--key=value" 형식이며 설정 파일 값을 덮어쓴다(--help로 목록 확인).
// @return 프로세스 종료 코드(0=정상, -1=초기화/시작 실패)
//***************************************************************************
int wmain(int argc, TCHAR* argv[])
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 및 기본 프레임워크 초기화
	InitUtf8Console();

	// 2-1. 명령행 인자 파싱 — --help는 아무것도 초기화하기 전에 처리한다.
	const CMainArgParser args(argc, argv);
	if( args.Has(_T("help")) )
	{
		PrintUsage();
		return 0;
	}

	// 3. Winsock 라이브러리(WSAStartup) 및 IOCP 확장 함수 포인터를 초기화
	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";

	// 4. 전역 프레임워크(메모리 풀 등) 초기화
	BaseGlobal::Init();

	// 5. 서버 인스턴스 생성 + 콘솔 종료 시그널 핸들러 등록
	CFileServerMain server;
	GServer = &server;
	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 6. 서버 설정 파일(JSON) 로드
	// 설정 파일 경로 — 기본값은 아래이며 --config로 바꿀 수 있다.
	_tstring configPath = _T("Config\\fileserver_config_mysql.json");
	if( false == args.ReadString(_T("config"), configPath) )
	{
		PrintUsage();
		MainClose();
		return -1;
	}
	const TCHAR* tszConfigPath = configPath.c_str();

	if( false == FILE_SERVER_CONFIG->Init(tszConfigPath) )
	{
		LOG_ERROR(_T("CFileServerConfig::Init Fail. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	// 6-0. 명령행 인자로 설정 파일 값을 덮어쓴다(포트/저장 경로처럼 인스턴스마다 다른 값).
	if( false == FILE_SERVER_CONFIG->ApplyArgs(args) )
	{
		PrintUsage();
		MainClose();
		return -1;
	}

	// 6-1. 필수 설정값 검증 — Redis 노드 목록이 비어있으면 업로드 토큰
	// 검증이 불가능해 서버가 의미 있게 동작할 수 없다.
	const auto& redisNodeVec = FILE_SERVER_CONFIG->GetRedisNodeVec();
	if( redisNodeVec.empty() )
	{
		LOG_ERROR(_T("RedisNode configuration is empty. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	FILE_SERVER_CONFIG->PrintServerSettingInfo();

	// 7. 서버 시작
	SFileServerOptions options;
	FILE_SERVER_CONFIG->ToOptions(options);
	const bool started = server.Start(options);

	if( !started )
	{
		LOG_ERROR(_T("CFileServerMain::Start Fail."));
		MainClose();
		return -1;
	}

	std::cout << "FileServer started. Press Ctrl+C to stop." << std::endl;

	// 8. 메인 스레드 대기 루프
	// [수정 — 근본 원인] CJobQueue::Push()는 "지금 이 스레드가 이미 다른
	// 큐를 실행 중"이면(LCurrentJobQueue != nullptr) 직접 실행하지 않고
	// gpGlobalQueue에 위임한다. 그런데 gpGlobalQueue는 그냥 큐 컨테이너일
	// 뿐 스스로 도는 워커 스레드가 없고, 이 프로젝트 어디에도(채팅 서버
	// 포함) gpThreadManager가 그 역할로 쓰이지 않는다 — 즉 위임된 작업을
	// 퍼가서 실행해주는 존재가 없었다. 그래서 두 번째 업로드처럼 콜백
	// 안에서 재귀적으로 또 다른 Redis 명령을 보내는 패턴이 이 위임 경로를
	// 타면, onComplete가 영원히 안 불리고 조용히 멈춰버렸다(실제로 재현됨).
	// 메인 스레드가 노는 동안 이 전역 큐를 직접 퍼가서 실행하게 해서
	// 근본적으로 해결한다.
	while( !g_bShouldExit.load() )
	{
		bool didWork = false;
		if( gpGlobalQueue != nullptr )
		{
			CJobQueueRef jobQueue = gpGlobalQueue->Pop();
			if( jobQueue != nullptr )
			{
				jobQueue->Execute();
				didWork = true;
			}
		}

		if( !didWork )
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	// 9. 정상 종료 경로
	MainClose();
	CloseConsole();

	return 0;
}