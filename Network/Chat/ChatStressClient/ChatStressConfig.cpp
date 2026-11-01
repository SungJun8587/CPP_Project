
//***************************************************************************
// ChatStressConfig.cpp : implementation of the CChatStressConfig class and the run-option functions.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressConfig.h"

#include <limits>

#include <Util/MainArgParser.h>

#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

SChatStressOptions		g_cfg;
std::atomic<bool>		g_stop{ false };
std::string				g_runTag;

//***************************************************************************
// @brief 명령행 옵션 사용법을 콘솔에 출력한다.
//***************************************************************************
void PrintUsage()
{
	std::printf(
		"usage: ChatStressClient [options]\n"
		"  --config <file>       load options from a JSON file; options given on the command line override it\n"
		"  --help                show this list\n"
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
// Construction/Destruction
//***************************************************************************

//***************************************************************************
// @brief CChatStressConfig 클래스의 생성자. 값은 SChatStressOptions의 기본값으로 시작한다.
//***************************************************************************
CChatStressConfig::CChatStressConfig()
{
	const SChatStressOptions defaults;
	_host = defaults.host;
	_port = defaults.port;
	_clients = defaults.clients;
	_workers = defaults.workers;
	_schedThreads = defaults.schedThreads;
	_durationSec = defaults.durationSec;
	_rampPerSec = defaults.rampPerSec;
	_chatIntervalMs = defaults.chatIntervalMs;
	_listIntervalMs = defaults.listIntervalMs;
	_memberIntervalMs = defaults.memberIntervalMs;
	_countIntervalMs = defaults.countIntervalMs;
	_roomId = defaults.roomId;
	_roomSpread = defaults.roomSpread;
	_msgBytes = defaults.msgBytes;
	_pageSize = defaults.pageSize;
	_prefix.assign(defaults.prefix.begin(), defaults.prefix.end());
	_accountsFile = defaults.accountsFile;
}

//***************************************************************************
// @brief CChatStressConfig 클래스의 소멸자
//***************************************************************************
CChatStressConfig::~CChatStressConfig()
{
}

namespace
{
	// 포트 번호 허용 범위
	constexpr uint16 kMinPort = 1;
	constexpr uint16 kMaxPort = 65535;
	// 정수 옵션 상한(int32 전체)
	constexpr int32 kMaxInt = (std::numeric_limits<int32>::max)();
}

//***************************************************************************
// @brief JSON 설정 파일로부터 실행 옵션을 읽어와 초기화합니다.
// @param tszConfigPath JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
// @details 파일에 없는 키는 기본값을 유지하고, 있는데 타입이 맞지 않거나 범위를 벗어나면
//          모아서 로그로 남기고 false를 돌려준다. 읽지 않는 키는 경고만 남긴다.
//***************************************************************************
bool CChatStressConfig::Init(const TCHAR* tszConfigPath)
{
	CRapidJSONUtil json;
	if( !json.LoadFromFile(tszConfigPath) || !json.IsObject() )
	{
		LOG_ERROR(_T("[Config] load failed (missing file or not a JSON object). path=%s"), tszConfigPath);
		return false;
	}

	CJSONConfigFieldReader reader(json);

	reader.Read(_T("Host"), _host);
	reader.Read(_T("Port"), _port, kMinPort, kMaxPort);

	reader.Read(_T("Clients"), _clients, 1, kMaxInt);
	reader.Read(_T("Workers"), _workers, 0, kMaxInt);				// 0이면 자동
	reader.Read(_T("SchedThreads"), _schedThreads, 1, kMaxInt);
	reader.Read(_T("DurationSec"), _durationSec, 0, kMaxInt);		// 0이면 무한
	reader.Read(_T("RampPerSec"), _rampPerSec, 0.0, 1.0e9);			// 0이면 한 번에 전부 접속

	reader.Read(_T("ChatIntervalMs"), _chatIntervalMs, 0, kMaxInt);
	reader.Read(_T("ListIntervalMs"), _listIntervalMs, 0, kMaxInt);
	reader.Read(_T("MemberIntervalMs"), _memberIntervalMs, 0, kMaxInt);
	reader.Read(_T("CountIntervalMs"), _countIntervalMs, 0, kMaxInt);

	reader.Read(_T("RoomId"), _roomId, 0, kMaxInt);
	reader.Read(_T("RoomSpread"), _roomSpread, 1, kMaxInt);
	reader.Read(_T("MsgBytes"), _msgBytes, 0, kMaxInt);
	reader.Read(_T("PageSize"), _pageSize, 1, kMaxInt);

	reader.Read(_T("Prefix"), _prefix);
	reader.Read(_T("AccountsFile"), _accountsFile);

	return reader.Validate();
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @param tszConfigPath 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
//***************************************************************************
bool CChatStressConfig::Save(const TCHAR* tszConfigPath)
{
	// 기존 파일이 있으면 읽어 와서 모르는 키와 키 순서를 보존한다(없으면 빈 문서에서 시작).
	CRapidJSONUtil json;
	json.LoadFromFile(tszConfigPath);

	CJSONConfigFieldWriter writer(json);

	writer.Write(_T("Host"), _host);
	writer.Write(_T("Port"), _port);

	writer.Write(_T("Clients"), _clients);
	writer.Write(_T("Workers"), _workers);
	writer.Write(_T("SchedThreads"), _schedThreads);
	writer.Write(_T("DurationSec"), _durationSec);
	writer.Write(_T("RampPerSec"), _rampPerSec);

	writer.Write(_T("ChatIntervalMs"), _chatIntervalMs);
	writer.Write(_T("ListIntervalMs"), _listIntervalMs);
	writer.Write(_T("MemberIntervalMs"), _memberIntervalMs);
	writer.Write(_T("CountIntervalMs"), _countIntervalMs);

	writer.Write(_T("RoomId"), _roomId);
	writer.Write(_T("RoomSpread"), _roomSpread);
	writer.Write(_T("MsgBytes"), _msgBytes);
	writer.Write(_T("PageSize"), _pageSize);

	writer.Write(_T("Prefix"), _prefix);
	writer.Write(_T("AccountsFile"), _accountsFile);

	return writer.Save(tszConfigPath);
}

//***************************************************************************
// @brief 로드된 설정 값을 실행 옵션으로 옮겨 담습니다.
//***************************************************************************
void CChatStressConfig::ToOptions(SChatStressOptions& options) const
{
	options.host = _host;
	options.port = _port;

	options.clients = _clients;
	options.workers = _workers;
	options.schedThreads = _schedThreads;
	options.durationSec = _durationSec;
	options.rampPerSec = _rampPerSec;

	options.chatIntervalMs = _chatIntervalMs;
	options.listIntervalMs = _listIntervalMs;
	options.memberIntervalMs = _memberIntervalMs;
	options.countIntervalMs = _countIntervalMs;

	options.roomId = _roomId;
	options.roomSpread = _roomSpread;
	options.msgBytes = _msgBytes;
	options.pageSize = _pageSize;

	options.prefix = ToNarrow(_prefix.c_str());
	options.accountsFile = _accountsFile;
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CChatStressConfig::PrintSettingInfo()
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : Stress Setting Info] ---------------"));
	LOG_INFO(_T("Host : %s"), _host.c_str());
	LOG_INFO(_T("Port : %d"), _port);
	LOG_INFO(_T("Clients : %d"), _clients);
	LOG_INFO(_T("Workers : %d"), _workers);
	LOG_INFO(_T("SchedThreads : %d"), _schedThreads);
	LOG_INFO(_T("DurationSec : %d"), _durationSec);
	LOG_INFO(_T("RampPerSec : %g"), _rampPerSec);
	LOG_INFO(_T("ChatIntervalMs : %d"), _chatIntervalMs);
	LOG_INFO(_T("ListIntervalMs : %d"), _listIntervalMs);
	LOG_INFO(_T("MemberIntervalMs : %d"), _memberIntervalMs);
	LOG_INFO(_T("CountIntervalMs : %d"), _countIntervalMs);
	LOG_INFO(_T("RoomId : %d"), _roomId);
	LOG_INFO(_T("RoomSpread : %d"), _roomSpread);
	LOG_INFO(_T("MsgBytes : %d"), _msgBytes);
	LOG_INFO(_T("PageSize : %d"), _pageSize);
	LOG_INFO(_T("Prefix : %s"), _prefix.c_str());
	LOG_INFO(_T("AccountsFile : %s"), _accountsFile.c_str());
	LOG_INFO(_T("--------------- [End Print] ---------------"));
	LOG_INFO(_T("###################################################################"));
}

//***************************************************************************
// @brief 명령행 인자를 파싱해 g_cfg에 채운다.
// @details "--key value" 또는 "--key=value" 형식(CMainArgParser). --config <file>이 있으면 그 JSON 파일을 먼저 읽어
//          기본값을 바꾸고, 나머지 명령행 옵션이 그 값을 덮어쓴다(우선순위: 명령행 > 설정 파일 > 내장 기본값).
//          값의 형식이나 범위가 틀리면 사유를 출력하고 false를 반환한다(설정 파일과 같은 범위를 검사).
//          --help면 사용법을 출력하고 false를 반환한다.
// @param argc 인자 개수
// @param argv 인자 배열(argv[0]은 실행 파일 경로라 건너뜀)
// @param exitCode false 반환 시 프로세스 종료 코드(--help는 0, 그 외 1)
// @return true=계속 진행, false=exitCode로 종료
//***************************************************************************
bool ParseArgs(int argc, wchar_t** argv, int& exitCode)
{
	const CMainArgParser args(argc, argv);
	if( args.Has(_T("help")) )
	{
		PrintUsage();
		exitCode = 0;
		return false;
	}

	// 설정 파일을 먼저 읽는다 — 아래 명령행 옵션이 파일 값을 덮어쓰도록 순서를 고정한다.
	_tstring configPath;
	if( !args.ReadString(_T("config"), configPath) )
	{
		PrintUsage();
		exitCode = 1;
		return false;
	}
	if( !configPath.empty() )
	{
		CChatStressConfig config;
		if( !config.Init(configPath.c_str()) )
		{
			std::printf("config: invalid \"%ls\" (see log)\n", configPath.c_str());
			exitCode = 1;
			return false;
		}
		config.ToOptions(g_cfg);
		std::printf("config: loaded \"%ls\"\n", configPath.c_str());
	}

	// 명령행 옵션 — 설정 파일과 같은 범위. 모든 인자를 검사해 오류를 한꺼번에 알린다.
	_tstring prefixText;
	bool ok = true;
	ok &= args.ReadString(_T("host"), g_cfg.host);
	ok &= args.ReadInt(_T("port"), g_cfg.port, kMinPort, kMaxPort);
	ok &= args.ReadInt(_T("clients"), g_cfg.clients, 1, kMaxInt);
	ok &= args.ReadInt(_T("workers"), g_cfg.workers, 0, kMaxInt);
	ok &= args.ReadInt(_T("sched"), g_cfg.schedThreads, 1, kMaxInt);
	ok &= args.ReadInt(_T("duration"), g_cfg.durationSec, 0, kMaxInt);
	ok &= args.ReadDouble(_T("ramp"), g_cfg.rampPerSec, 0.0, 1.0e9);
	ok &= args.ReadInt(_T("chat"), g_cfg.chatIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("list"), g_cfg.listIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("members"), g_cfg.memberIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("count"), g_cfg.countIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("room"), g_cfg.roomId, 0, kMaxInt);
	ok &= args.ReadInt(_T("room-spread"), g_cfg.roomSpread, 1, kMaxInt);
	ok &= args.ReadInt(_T("msg-bytes"), g_cfg.msgBytes, 0, kMaxInt);
	ok &= args.ReadInt(_T("page-size"), g_cfg.pageSize, 1, kMaxInt);
	ok &= args.ReadString(_T("prefix"), prefixText);
	ok &= args.ReadString(_T("accounts"), g_cfg.accountsFile);
	if( !ok )
	{
		PrintUsage();
		exitCode = 1;
		return false;
	}
	if( !prefixText.empty() )
		g_cfg.prefix = ToNarrow(prefixText.c_str());

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