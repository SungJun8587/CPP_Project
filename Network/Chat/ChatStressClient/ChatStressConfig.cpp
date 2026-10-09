
//***************************************************************************
// ChatStressConfig.cpp : implementation of the ChatStressConfig struct and the run-option functions.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressConfig.h"

ChatStressConfig		g_cfg;
std::atomic<bool>		g_stop{ false };
std::string				g_runTag;

//***************************************************************************
// @brief 명령행 옵션 사용법을 콘솔에 출력한다.
//***************************************************************************
void PrintUsage()
{
	std::printf(
		"usage: ChatStressClient [options]\n"
		"  --host <addr>         server address (127.0.0.1)\n"
		"  --port <n>            server port (9000)\n"
		"  --clients <n>         virtual clients (100)\n"
		"  --workers <n>         IOCP worker threads, 0=auto (0)\n"
		"  --sched <n>           scheduler threads (2)\n"
		"  --duration <sec>      test time, 0=forever (60)\n"
		"  --ramp <n/sec>        new connections per second, 0=all at once (50)\n"
		"  --chat <ms>           chat interval per client, 0=off (1000)\n"
		"  --list <ms>           ListRooms interval, 0=off (5000)\n"
		"  --members <ms>        RoomMemberList interval, 0=off (needs --room) (0)\n"
		"  --count <ms>          ServerUserCount interval, 0=off (0)\n"
		"  --room <id>           room to enter after login, 0=lobby (0)\n"
		"  --room-spread <n>     spread over room..room+n-1 (1)\n"
		"  --msg-bytes <n>       chat body length (32)\n"
		"  --page-size <n>       pageSize for list requests (20)\n"
		"  --prefix <str>        nickname prefix (S)\n"
		"  --accounts <file>     save/reuse accounts (public_id+token) across runs\n");
}

//***************************************************************************
// @brief 와이드 문자열을 ASCII 문자열로 변환한다.
// @details 접두어는 ASCII만 허용하므로 128 이상의 문자는 '_'로 치환한다.
// @param w 변환할 NUL 종료 와이드 문자열
// @return 변환된 문자열
//***************************************************************************
static std::string ToNarrow(const wchar_t* w)
{
	std::string s;
	for( ; *w; ++w )
		s.push_back(static_cast<char>(*w < 128 ? *w : '_'));	// 접두어는 ASCII만 허용
	return s;
}

//***************************************************************************
// @brief 명령행 인자를 파싱해 g_cfg에 채운다.
// @details 알 수 없는 옵션이거나 --help/-h이면 사용법을 출력하고 false를 반환한다.
//          값이 필요한 옵션의 값이 없으면 사용법을 출력하고 프로세스를 종료한다.
// @param argc 인자 개수
// @param argv 인자 배열(argv[0]은 실행 파일 경로라 건너뜀)
// @param exitCode false 반환 시 프로세스 종료 코드(--help/-h는 0, 그 외 1)
// @return true=계속 진행, false=exitCode로 종료
//***************************************************************************
bool ParseArgs(int argc, wchar_t** argv, int& exitCode)
{
	for( int i = 1; i < argc; ++i )
	{
		const wchar_t* a = argv[i];
		auto next = [&]() -> const wchar_t*
			{
				if( i + 1 >= argc ) { PrintUsage(); std::exit(1); }
				return argv[++i];
			};
		auto is = [&](const wchar_t* name) { return std::wcscmp(a, name) == 0; };

		if( is(L"--host") )				g_cfg.host = next();
		else if( is(L"--port") )		g_cfg.port = static_cast<uint16>(_wtoi(next()));
		else if( is(L"--clients") )		g_cfg.clients = _wtoi(next());
		else if( is(L"--workers") )		g_cfg.workers = _wtoi(next());
		else if( is(L"--sched") )		g_cfg.schedThreads = _wtoi(next());
		else if( is(L"--duration") )	g_cfg.durationSec = _wtoi(next());
		else if( is(L"--ramp") )		g_cfg.rampPerSec = _wtof(next());
		else if( is(L"--chat") )		g_cfg.chatIntervalMs = _wtoi(next());
		else if( is(L"--list") )		g_cfg.listIntervalMs = _wtoi(next());
		else if( is(L"--members") )		g_cfg.memberIntervalMs = _wtoi(next());
		else if( is(L"--count") )		g_cfg.countIntervalMs = _wtoi(next());
		else if( is(L"--room") )		g_cfg.roomId = _wtoi(next());
		else if( is(L"--room-spread") )	g_cfg.roomSpread = _wtoi(next());
		else if( is(L"--msg-bytes") )	g_cfg.msgBytes = _wtoi(next());
		else if( is(L"--page-size") )	g_cfg.pageSize = _wtoi(next());
		else if( is(L"--prefix") )		g_cfg.prefix = ToNarrow(next());
		else if( is(L"--accounts") )	g_cfg.accountsFile = next();
		else
		{
			PrintUsage();
			exitCode = (is(L"--help") || is(L"-h")) ? 0 : 1;
			return false;
		}
	}

	if( g_cfg.clients < 1 ) g_cfg.clients = 1;
	if( g_cfg.schedThreads < 1 ) g_cfg.schedThreads = 1;
	return true;
}

//***************************************************************************
// @brief 닉네임/RTT 마커에 쓰는 4자리 실행 태그(g_runTag)를 생성한다.
// @details 계정 파일에서 이미 읽어온 태그가 있으면 그대로 유지한다.
//***************************************************************************
void GenerateRunTag()
{
	if( !g_runTag.empty() )
		return;	// 계정 파일에서 이미 읽어온 태그가 있으면 유지(재실행 시 같은 닉네임 체계 사용)

	std::mt19937 rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
	const char* alphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
	for( int i = 0; i < 4; ++i )
		g_runTag += alphabet[rng() % 36];
	std::srand(static_cast<unsigned>(rng()));
}