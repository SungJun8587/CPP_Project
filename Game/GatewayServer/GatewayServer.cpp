
//***************************************************************************
// GatewayServer.cpp : CGatewayServerMain 구동 예시 (게이트웨이 프로세스 진입점)
//
// 설정: Config\gatewayserver_config.json (CGatewayServerConfig). 명령행 인자를 주면 같은 항목의 파일 값을 덮어쓴다.
// 실행 예: GatewayServer.exe --config=Config\gatewayserver_config.json --client-port=7001
//          GatewayServer.exe --dump-config=Config\gatewayserver_config.json   (기본값 템플릿 생성)
//***************************************************************************

#include "pch.h"
#include "../GameCommon/ZoneCommon/ArgParser.h"
#include "GatewayServerMain.h"
#include "GatewayServerConfig.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
	std::atomic<bool> g_bShouldExit{ false };

	//***************************************************************************
	// @brief 콘솔 Ctrl+C / 창 닫기 등을 감지해 메인 스레드에 종료를 알립니다.
	// @details 실제 정지(Stop())는 메인 스레드에서 수행한다 — 이 핸들러는 별도 스레드 컨텍스트에서 호출된다.
	//***************************************************************************
	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		switch( ctrlType )
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			g_bShouldExit.store(true);
			return TRUE;
		default:
			return FALSE;
		}
	}
}

//***************************************************************************
// @brief 프로세스 종료 직전 정리 — 실패 경로/정상 종료 경로가 공유한다.
//***************************************************************************
void MainClose()
{
	BaseGlobal::Destroy();
	CSocketUtils::Clear();
}

int _tmain(int argc, TCHAR* argv[])
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	InitUtf8Console();

	CSocketUtils::Init();
	BaseGlobal::Init();

	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 설정 — 우선순위: 명령행 인자 > JSON 설정 파일 > SGatewayOptions 기본값
	const CArgParser args(argc, argv);

	// --dump-config=경로 : 기본값으로 채운 설정 파일 템플릿을 저장하고 종료한다.
	const _tstring dumpPath = args.GetString(_T("dump-config"), _T(""));
	if( !dumpPath.empty() )
	{
		const bool saved = dumpPath != _T("1") && CGatewayServerConfig().Save(dumpPath.c_str());
		if( saved )
			LOG_INFO(_T("Default config saved. path=%s"), dumpPath.c_str());
		else
			LOG_ERROR(_T("--dump-config needs a writable file path. value=%s"), dumpPath.c_str());
		MainClose();
		return saved ? 0 : -1;
	}

	CGatewayServerConfig config;
	if( !config.Init(args.GetString(_T("config"), _T("Config\\gatewayserver_config.json")).c_str()) )
	{
		MainClose();
		return -1;
	}
	config.PrintServerSettingInfo();

	SGatewayOptions options;
	config.ToOptions(options);

	options.clientBindIp = args.GetString(_T("client-ip"), options.clientBindIp.c_str());
	options.clientPort = static_cast<uint16>(args.GetUInt(_T("client-port"), options.clientPort));
	options.maxClientSessions = static_cast<int32>(args.GetUInt(_T("max-clients"), options.maxClientSessions));
	options.clientWorkerCount = args.GetUInt(_T("client-workers"), options.clientWorkerCount);
	options.zoneBindIp = args.GetString(_T("zone-ip"), options.zoneBindIp.c_str());
	options.zonePort = static_cast<uint16>(args.GetUInt(_T("zone-port"), options.zonePort));
	options.maxZoneSessions = static_cast<int32>(args.GetUInt(_T("max-zones"), options.maxZoneSessions));
	options.zoneWorkerCount = args.GetUInt(_T("zone-workers"), options.zoneWorkerCount);
	options.heartbeatIntervalMs = args.GetUInt(_T("heartbeat-ms"), options.heartbeatIntervalMs);
	options.heartbeatTimeoutMs = args.GetUInt(_T("heartbeat-timeout-ms"), options.heartbeatTimeoutMs);

	CGatewayServerMain server;
	if( !server.Start(options) )
	{
		LOG_ERROR(_T("CGatewayServerMain::Start Fail."));
		MainClose();
		return -1;
	}

	std::cout << "GatewayServer started. client port=" << options.clientPort
		<< ", zone port=" << options.zonePort << ". Press Ctrl+C to stop." << std::endl;

	// 메인 스레드 대기 루프 — 실제 I/O는 IOCP 워커 스레드들이 처리한다.
	while( !g_bShouldExit.load() )
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	server.Stop();
	MainClose();
	CloseConsole();

	return 0;
}
