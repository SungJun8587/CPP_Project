// ChatServer.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "ChatServerMain.h"

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
}

int main()
{
	CChatServerMain server;
	GServer = &server;
	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// TODO: 실서비스에서는 설정 파일/커맨드라인 인자로 대체
	const bool started = server.Start(
		_T("0.0.0.0"), 7777,			// bind 주소/포트
		"127.0.0.1", 6379, 5,			// Redis 접속 정보(pool size 5)
		"ChatServer", "1",				// serverType/serverId
		1000, 0,						// maxSessionCount, workerThreadCount(자동)
		15, 5							// 하트비트 TTL 15초 / 5초마다 갱신
	);

	if( !started )
	{
		std::cerr << "CChatServer::Start() 실패" << std::endl;
		return 1;
	}

	std::cout << "ChatServer started. Press Ctrl+C to stop." << std::endl;

	// 메인 스레드는 그냥 대기 — 실제 I/O는 IOCP 워커 스레드들이 처리한다.
	while( true )
		std::this_thread::sleep_for(std::chrono::seconds(1));

	return 0;
}
