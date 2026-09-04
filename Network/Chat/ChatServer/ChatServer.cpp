
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

int main()
{
	CChatServerMain server;
	GServer = &server;
	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	TCHAR tszConfigPath[FULLPATH_STRLEN];
	_sntprintf_s(tszConfigPath, FULLPATH_STRLEN, _TRUNCATE, _T("..\\Config\\server_config_mysql.json"));

	if( false == SERVER_CONFIG->Init(tszConfigPath) )
	{
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail."));
		SERVER_CONFIG->ReleaseInstance();
		return -1;
	}

	SERVER_CONFIG->PrintServerSettingInfo();

	// TODO: Redis/DB 커넥션 풀 크기와 DB 비동기 워커 스레드 수는
	// CServerConfig의 JSON 스키마(ServerConfig.cpp::Init() 참고)에 항목이
	// 없어 데모용 상수로 고정합니다 — 실서비스에서는 설정 파일에 추가하는
	//것을 권장합니다.
	constexpr int32 kRedisPoolSize = 5;
	constexpr int32 kDbWorkerThreadCnt = 4;
	constexpr int32 kHeartbeatTtlSec = 15;
	constexpr int32 kHeartbeatIntervalSec = 5;

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
		LOG_ERROR(_T("CChatServerMain::Start Fail."));
		SERVER_CONFIG->ReleaseInstance();
		return -1;
	}

	std::cout << "ChatServer started. Press Ctrl+C to stop." << std::endl;

	// 메인 스레드는 그냥 대기 — 실제 I/O는 IOCP 워커 스레드들이 처리한다.
	// Ctrl+C 등은 ConsoleCtrlHandler가 별도 스레드 컨텍스트에서 GServer->Stop()을
	// 직접 호출하므로, 이 무한루프 자체는 정상 종료 경로를 막지 않는다.
	while( true )
		std::this_thread::sleep_for(std::chrono::seconds(1));

	return 0;
}