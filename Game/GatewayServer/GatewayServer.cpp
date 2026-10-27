
//***************************************************************************
// GatewayServer.cpp : CGatewayServerMain 구동 예시 (게이트웨이 프로세스 진입점)
//
// 실행 예: GatewayServer.exe --client-port=7000 --zone-ip=127.0.0.1 --zone-port=7100
//***************************************************************************

#include "pch.h"
#include "GatewayServerMain.h"

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

	// 설정 — 서버 설정 파일(JSON) 연동 전까지는 명령행 인자로 받는다.
	const CArgParser args(argc, argv);

	SGatewayOptions options;
	options.clientBindIp = args.GetString(_T("client-ip"), _T("0.0.0.0"));
	options.clientPort = static_cast<uint16>(args.GetUInt(_T("client-port"), 7000));
	options.maxClientSessions = static_cast<int32>(args.GetUInt(_T("max-clients"), 5000));
	options.clientWorkerCount = args.GetUInt(_T("client-workers"), 0);
	options.zoneBindIp = args.GetString(_T("zone-ip"), _T("127.0.0.1"));
	options.zonePort = static_cast<uint16>(args.GetUInt(_T("zone-port"), 7100));
	options.maxZoneSessions = static_cast<int32>(args.GetUInt(_T("max-zones"), 64));
	options.zoneWorkerCount = args.GetUInt(_T("zone-workers"), 2);
	options.heartbeatIntervalMs = args.GetUInt(_T("heartbeat-ms"), kDefaultHeartbeatIntervalMs);
	options.heartbeatTimeoutMs = args.GetUInt(_T("heartbeat-timeout-ms"), kDefaultHeartbeatTimeoutMs);

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
