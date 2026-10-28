//***************************************************************************
// ZoneServer.cpp : CZoneServerMain 구동 예시 (존 서버 프로세스 진입점)
//
// 설정: Config\zoneserver_config.json (CZoneServerConfig). 명령행 인자를 주면 같은 항목의 파일 값을 덮어쓴다.
// 실행 예: ZoneServer.exe --config=Config\zoneserver_config.json --zone-id=2
//          ZoneServer.exe --dump-config=Config\zoneserver_config.json   (기본값 템플릿 생성)
//          (필드/던전마다 zone-id를 다르게 해서 프로세스를 여러 개 띄운다. 설정 파일을 공유하고 zone-id만 바꿔도 된다)
//***************************************************************************

#include "pch.h"
#include "../GameCommon/ZoneCommon/ArgParser.h"
#include "ZoneServerMain.h"
#include "ZoneServerConfig.h"

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

	// 설정 — 우선순위: 명령행 인자 > JSON 설정 파일 > SZoneServerOptions 기본값
	const CArgParser args(argc, argv);

	// --dump-config=경로 : 기본값으로 채운 설정 파일 템플릿을 저장하고 종료한다.
	const _tstring dumpPath = args.GetString(_T("dump-config"), _T(""));
	if( !dumpPath.empty() )
	{
		const bool saved = dumpPath != _T("1") && CZoneServerConfig().Save(dumpPath.c_str());
		if( saved )
			LOG_INFO(_T("Default config saved. path=%s"), dumpPath.c_str());
		else
			LOG_ERROR(_T("--dump-config needs a writable file path. value=%s"), dumpPath.c_str());
		MainClose();
		return saved ? 0 : -1;
	}

	CZoneServerConfig config;
	if( !config.Init(args.GetString(_T("config"), _T("Config\\zoneserver_config.json")).c_str()) )
	{
		MainClose();
		return -1;
	}
	config.PrintServerSettingInfo();

	SZoneServerOptions options;
	config.ToOptions(options);

	options.zoneId = args.GetUInt(_T("zone-id"), options.zoneId);
	options.kind = static_cast<EZoneKind>(args.GetUInt(_T("kind"), static_cast<uint32>(options.kind)));
	options.capacity = args.GetUInt(_T("capacity"), options.capacity);
	const _tstring nameArg = args.GetString(_T("name"), _T(""));
	if( !nameArg.empty() )
		options.serverName = TStringToString(nameArg.c_str());
	options.gatewayIp = args.GetString(_T("gateway-ip"), options.gatewayIp.c_str());
	options.gatewayPort = static_cast<uint16>(args.GetUInt(_T("gateway-port"), options.gatewayPort));
	options.workerThreadCount = args.GetUInt(_T("workers"), options.workerThreadCount);
	options.tickIntervalMs = args.GetUInt(_T("tick-ms"), options.tickIntervalMs);
	options.minX = args.GetFloat(_T("min-x"), options.minX);
	options.minY = args.GetFloat(_T("min-y"), options.minY);
	options.maxX = args.GetFloat(_T("max-x"), options.maxX);
	options.maxY = args.GetFloat(_T("max-y"), options.maxY);
	options.cellSize = args.GetFloat(_T("cell-size"), options.cellSize);
	options.spawnX = args.GetFloat(_T("spawn-x"), options.spawnX);
	options.spawnY = args.GetFloat(_T("spawn-y"), options.spawnY);
	options.playerSpeed = args.GetFloat(_T("player-speed"), options.playerSpeed);

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
