
//***************************************************************************
// ZoneServer.cpp : CZoneServerMain 구동 예시 (존 서버 프로세스 진입점)
//
// 실행 예: ZoneServer.exe --zone-id=1 --kind=1 --capacity=500 --gateway-ip=127.0.0.1 --gateway-port=7100
//          (필드/던전마다 zone-id를 다르게 해서 프로세스를 여러 개 띄운다)
//***************************************************************************

#include "pch.h"
#include "../GameCommon/ZoneCommon/ArgParser.h"
#include "ZoneServerMain.h"

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

	SZoneServerOptions options;
	options.zoneId = args.GetUInt(_T("zone-id"), 1);
	options.kind = static_cast<EZoneKind>(args.GetUInt(_T("kind"), static_cast<uint32>(EZoneKind::Field)));
	options.capacity = args.GetUInt(_T("capacity"), 500);
	options.serverName = TStringToString(args.GetString(_T("name"), _T("ZoneServer")).c_str());
	options.gatewayIp = args.GetString(_T("gateway-ip"), _T("127.0.0.1"));
	options.gatewayPort = static_cast<uint16>(args.GetUInt(_T("gateway-port"), 7100));
	options.workerThreadCount = args.GetUInt(_T("workers"), 1);
	options.tickIntervalMs = args.GetUInt(_T("tick-ms"), 50);
	options.minX = args.GetFloat(_T("min-x"), 0.0f);
	options.minY = args.GetFloat(_T("min-y"), 0.0f);
	options.maxX = args.GetFloat(_T("max-x"), 1000.0f);
	options.maxY = args.GetFloat(_T("max-y"), 1000.0f);
	options.cellSize = args.GetFloat(_T("cell-size"), 50.0f);
	options.spawnX = args.GetFloat(_T("spawn-x"), 100.0f);
	options.spawnY = args.GetFloat(_T("spawn-y"), 100.0f);
	options.playerSpeed = args.GetFloat(_T("player-speed"), 6.0f);

	CZoneServerMain server;
	if( !server.Start(options) )
	{
		LOG_ERROR(_T("CZoneServerMain::Start Fail. zoneId=%u"), options.zoneId);
		MainClose();
		return -1;
	}

	std::cout << "ZoneServer started. zoneId=" << options.zoneId << ". Press Ctrl+C to stop." << std::endl;

	// 메인 스레드 대기 루프 — 실제 처리는 존 틱 스레드와 IOCP 워커가 한다.
	// Gateway가 등록을 거부하면(중복 zoneId 등) 재시도해도 같은 결과이므로 스스로 종료한다.
	while( !g_bShouldExit.load() )
	{
		if( server.IsRegisterRejected() )
		{
			LOG_ERROR(_T("Gateway rejected zone registration. Shutting down. zoneId=%u"), options.zoneId);
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	server.Stop();
	MainClose();
	CloseConsole();

	return 0;
}
