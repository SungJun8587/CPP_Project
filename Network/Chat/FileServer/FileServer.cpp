
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
	// @brief TCHAR 문자열을 std::string으로 변환함(UNICODE 빌드 대응).
	//***************************************************************************
	std::string TCharToString(const TCHAR* ptsz)
	{
		if( ptsz == nullptr ) return std::string();
		return TStringToString(ptsz);
	}

	//***************************************************************************
	// @brief CFileServerConfig의 싱글톤 포인터에 접근합니다.
	// @return CFileServerConfig* 서버 설정 싱글톤 포인터
	//***************************************************************************
	#define FILESERVER_CONFIG						CFileServerConfig::GetSingletonPtr()
}

//***************************************************************************
// @brief 프로세스 종료 직전 정리 — 실패 경로/정상 종료 경로가 공유한다.
// @details 채팅 서버(ChatServer.cpp::MainClose())와 달리 DB 비동기 서비스
// 종료 단계가 없다 — 파일 서버는 애초에 DB 계층을 안 갖고 있다.
//***************************************************************************
void MainClose()
{
	// 1. 파일 서버 설정 싱글턴 해제
	FILESERVER_CONFIG->ReleaseInstance();

	// 2. 전역 프레임워크(메모리 풀 등) 정리
	BaseGlobal::Destroy();

	// 3. Winsock 라이브러리 자원(WSACleanup)을 해제
	CSocketUtils::Clear();
}

int main()
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 및 기본 프레임워크 초기화
	InitUtf8Console();

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
	TCHAR tszConfigPath[FULLPATH_STRLEN];
	_sntprintf_s(tszConfigPath, FULLPATH_STRLEN, _TRUNCATE, _T("Config\\fileserver_config_mysql.json"));

	if( false == FILESERVER_CONFIG->Init(tszConfigPath) )
	{
		LOG_ERROR(_T("CFileServerConfig::Init Fail. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	// 6-1. 필수 설정값 검증 — Redis 노드 목록이 비어있으면 업로드 토큰
	// 검증이 불가능해 서버가 의미 있게 동작할 수 없다.
	const auto& redisNodeVec = FILESERVER_CONFIG->GetRedisNodeVec();
	if( redisNodeVec.empty() )
	{
		LOG_ERROR(_T("RedisNode configuration is empty. (Path: %s)"), tszConfigPath);
		MainClose();
		return -1;
	}

	FILESERVER_CONFIG->PrintServerSettingInfo();

	// 7. 서버 시작
	const bool started = server.Start(
		FILESERVER_CONFIG->GetServerIP(), FILESERVER_CONFIG->GetServerPort(),
		FILESERVER_CONFIG->GetRedisNodeVec(), FILESERVER_CONFIG->GetRedisPoolSize(),
		FILESERVER_CONFIG->GetMaxSessionCount(), FILESERVER_CONFIG->GetWorkerThreadCnt(),
		FILESERVER_CONFIG->GetStorageDir(),
		TCharToString(FILESERVER_CONFIG->GetPublicBaseUrl()),
		FILESERVER_CONFIG->GetMaxUploadBytes()
	);

	if( !started )
	{
		LOG_ERROR(_T("CFileServerMain::Start Fail."));
		MainClose();
		return -1;
	}

	std::cout << "FileServer started. Press Ctrl+C to stop." << std::endl;

	// 8. 메인 스레드 대기 루프
	while( !g_bShouldExit.load() )
		std::this_thread::sleep_for(std::chrono::seconds(1));

	// 9. 정상 종료 경로
	MainClose();
	CloseConsole();

	return 0;
}