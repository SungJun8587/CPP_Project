
//***************************************************************************
// GatewayServer.cpp : CGatewayServerMain 구동 예시 (게이트웨이 프로세스 진입점)
//
// 설정: Config\gatewayserver_config.json (CGatewayServerConfig). 명령행 인자를 주면 같은 항목의 파일 값을 덮어쓴다.
// 실행 예: GatewayServer.exe --config Config\gatewayserver_config.json --client-port 7001
//          GatewayServer.exe --dump-config Config\gatewayserver_config.json   (기본값 템플릿 생성)
//***************************************************************************

#include "pch.h"
#include "../GameCommon/ZoneCommon/ServerMainCommon.h"
#include "GatewayServerMain.h"
#include "GatewayServerConfig.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

int _tmain(int argc, TCHAR* argv[])
{
	ServerMainCommon::Startup();

	// 설정 — 우선순위: 명령행 인자 > JSON 설정 파일 > SGatewayOptions 기본값
	const CMainArgParser args(argc, argv);

	// --dump-config=경로 : 기본값으로 채운 설정 파일 템플릿을 저장하고 종료한다.
	int exitCode = 0;
	if( ServerMainCommon::TryDumpConfig<CGatewayServerConfig>(args, exitCode) )
	{
		ServerMainCommon::Close();
		return exitCode;
	}

	CGatewayServerConfig config;
	if( !config.Init(args.GetString(_T("config"), _T("Config\\gatewayserver_config.json")).c_str()) )
	{
		ServerMainCommon::Close();
		return -1;
	}
	config.PrintServerSettingInfo();

	SGatewayOptions options;
	config.ToOptions(options);

	// 형식/범위 오류(오타 포함)는 LOG_ERROR 후 기동하지 않는다.
	bool argsOk = true;
	argsOk &= args.ReadString(_T("client-ip"), options.clientBindIp);
	argsOk &= args.ReadInt(_T("client-port"), options.clientPort, 1, 65535);
	argsOk &= args.ReadInt(_T("max-clients"), options.maxClientSessions, 1, INT32_MAX);
	argsOk &= args.ReadInt(_T("client-workers"), options.clientWorkerCount, 0, 1024);
	argsOk &= args.ReadString(_T("zone-ip"), options.zoneBindIp);
	argsOk &= args.ReadInt(_T("zone-port"), options.zonePort, 1, 65535);
	argsOk &= args.ReadInt(_T("max-zones"), options.maxZoneSessions, 1, INT32_MAX);
	argsOk &= args.ReadInt(_T("zone-workers"), options.zoneWorkerCount, 0, 1024);
	argsOk &= args.ReadInt(_T("heartbeat-ms"), options.heartbeatIntervalMs, 0, UINT32_MAX);
	argsOk &= args.ReadInt(_T("heartbeat-timeout-ms"), options.heartbeatTimeoutMs, 0, UINT32_MAX);
	if( !argsOk )
	{
		ServerMainCommon::Close();
		return -1;
	}

	CGatewayServerMain server;
	if( !server.Start(options) )
	{
		LOG_ERROR(_T("CGatewayServerMain::Start Fail."));
		ServerMainCommon::Close();
		return -1;
	}

	std::cout << "GatewayServer started. client port=" << options.clientPort
		<< ", zone port=" << options.zonePort << ". Press Ctrl+C to stop." << std::endl;

	// 메인 스레드 대기 루프 — 실제 I/O는 IOCP 워커 스레드들이 처리한다.
	while( !ServerMainCommon::ShouldExit() )
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	server.Stop();
	ServerMainCommon::Close();
	CloseConsole();

	return 0;
}