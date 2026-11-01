
//***************************************************************************
// BotClientConfig.cpp : implementation of the CBotClientConfig class and the run-option functions.
//
//***************************************************************************

#include "pch.h"
#include "BotClientConfig.h"

#include <limits>

#include <Util/MainArgParser.h>

#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

SBotClientOptions		g_cfg;
std::atomic<bool>		g_stop{ false };

namespace
{
	// 포트 번호 허용 범위
	constexpr uint16 kMinPort = 1;
	constexpr uint16 kMaxPort = 65535;
	// 정수 옵션 상한(int32 전체)
	constexpr int32 kMaxInt = (std::numeric_limits<int32>::max)();
	// 캐릭터 ID 시작값 상한. 몬스터 ID 대역(최상위 비트)을 침범하지 않도록 int32 범위로 제한한다.
	constexpr int32 kMaxCharIdBase = kMaxInt;

	//***************************************************************************
	// @brief 영웅 목록 문자열이 "숫자(,숫자)*" 형식인지 검사합니다. 공백은 허용합니다.
	//***************************************************************************
	bool IsValidHeroList(const _tstring& text)
	{
		bool hasDigit = false;
		for( const TCHAR ch : text )
		{
			if( ch >= _T('0') && ch <= _T('9') )
				hasDigit = true;
			else if( ch != _T(',') && ch != _T(' ') )
				return false;
		}
		return hasDigit;
	}
}

//***************************************************************************
// @brief 명령행 옵션 사용법을 콘솔에 출력한다.
//***************************************************************************
void PrintUsage()
{
	std::printf(
		"usage: BotClient [options]  (\"--key value\" or \"--key=value\")\n"
		"  --config <file>       load options from a JSON file; options given on the command line override it\n"
		"  --dump-config <file>  save a default config template to the file and exit\n"
		"  --host <addr>         Gateway client-port address (127.0.0.1)\n"
		"  --port <n>            Gateway client port (7000)\n"
		"  --bots <n>            bots (10)\n"
		"  --zone <id>           zone to enter (1)\n"
		"  --heroes <list>       hero ids, comma separated, assigned to bots in turn; 0=server default (0)\n"
		"  --charid-base <n>     first bot's charId; bot i uses base+i (100000)\n"
		"  --ramp-ms <n>         delay between bot logins (20)\n"
		"  --tick-ms <n>         bot decision tick (50)\n"
		"  --move-ms <n>         min interval between move requests (300)\n"
		"  --attack-ms <n>       interval between attack requests (400)\n"
		"  --workers <n>         IOCP worker threads (2)\n"
		"  --stats-sec <n>       stats print interval (5)\n"
		"  --duration <sec>      run time, 0=until Ctrl+C (0)\n"
		"  --help                show this usage\n");
}

//***************************************************************************
// Construction/Destruction
//***************************************************************************

//***************************************************************************
// @brief CBotClientConfig 클래스의 생성자. 값은 SBotClientOptions의 기본값으로 시작한다.
//***************************************************************************
CBotClientConfig::CBotClientConfig()
{
	const SBotClientOptions defaults;
	_host = defaults.host;
	_port = defaults.port;
	_botCount = defaults.botCount;
	_zoneId = defaults.zoneId;
	_heroes = defaults.heroes;
	_charIdBase = defaults.charIdBase;
	_rampMs = defaults.rampMs;
	_tickMs = defaults.tickMs;
	_moveIntervalMs = defaults.moveIntervalMs;
	_attackIntervalMs = defaults.attackIntervalMs;
	_workerThreadCnt = defaults.workerCount;
	_statsSec = defaults.statsSec;
	_durationSec = defaults.durationSec;
}

//***************************************************************************
// @brief CBotClientConfig 클래스의 소멸자
//***************************************************************************
CBotClientConfig::~CBotClientConfig()
{
}

//***************************************************************************
// @brief JSON 설정 파일로부터 실행 옵션을 읽어와 초기화합니다.
// @param tszConfigPath JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
// @details 파일에 없는 키는 기본값을 유지하고, 있는데 타입이 맞지 않거나 범위를 벗어나면
//          모아서 로그로 남기고 false를 돌려준다. 읽지 않는 키는 경고만 남긴다.
//***************************************************************************
bool CBotClientConfig::Init(const TCHAR* tszConfigPath)
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

	reader.Read(_T("Bots"), _botCount, 1, 100000);
	reader.Read(_T("ZoneId"), _zoneId, 0, kMaxInt);
	reader.Read(_T("Heroes"), _heroes);
	reader.Read(_T("CharIdBase"), _charIdBase, 1, kMaxCharIdBase);

	reader.Read(_T("RampMs"), _rampMs, 0, kMaxInt);
	reader.Read(_T("TickMs"), _tickMs, 1, kMaxInt);
	reader.Read(_T("MoveIntervalMs"), _moveIntervalMs, 0, kMaxInt);
	reader.Read(_T("AttackIntervalMs"), _attackIntervalMs, 0, kMaxInt);

	reader.Read(_T("WorkerThreadCnt"), _workerThreadCnt, 1, 1024);
	reader.Read(_T("StatsSec"), _statsSec, 1, kMaxInt);
	reader.Read(_T("DurationSec"), _durationSec, 0, kMaxInt);		// 0이면 무한

	if( !reader.Validate() )
		return false;

	if( !IsValidHeroList(_heroes) )
	{
		LOG_ERROR(_T("[Config] Heroes must be comma-separated numbers (e.g. \"1,2,3\"). value=%s"), _heroes.c_str());
		return false;
	}

	return true;
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @param tszConfigPath 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
//***************************************************************************
bool CBotClientConfig::Save(const TCHAR* tszConfigPath)
{
	// 기존 파일이 있으면 읽어 와서 모르는 키와 키 순서를 보존한다(없으면 빈 문서에서 시작).
	CRapidJSONUtil json;
	json.LoadFromFile(tszConfigPath);

	CJSONConfigFieldWriter writer(json);

	writer.Write(_T("Host"), _host);
	writer.Write(_T("Port"), _port);

	writer.Write(_T("Bots"), _botCount);
	writer.Write(_T("ZoneId"), _zoneId);
	writer.Write(_T("Heroes"), _heroes);
	writer.Write(_T("CharIdBase"), _charIdBase);

	writer.Write(_T("RampMs"), _rampMs);
	writer.Write(_T("TickMs"), _tickMs);
	writer.Write(_T("MoveIntervalMs"), _moveIntervalMs);
	writer.Write(_T("AttackIntervalMs"), _attackIntervalMs);

	writer.Write(_T("WorkerThreadCnt"), _workerThreadCnt);
	writer.Write(_T("StatsSec"), _statsSec);
	writer.Write(_T("DurationSec"), _durationSec);

	return writer.Save(tszConfigPath);
}

//***************************************************************************
// @brief 로드된 설정 값을 실행 옵션으로 옮겨 담습니다.
//***************************************************************************
void CBotClientConfig::ToOptions(SBotClientOptions& options) const
{
	options.host = _host;
	options.port = _port;

	options.botCount = _botCount;
	options.zoneId = _zoneId;
	options.heroes = _heroes;
	options.charIdBase = _charIdBase;

	options.rampMs = _rampMs;
	options.tickMs = _tickMs;
	options.moveIntervalMs = _moveIntervalMs;
	options.attackIntervalMs = _attackIntervalMs;

	options.workerCount = _workerThreadCnt;
	options.statsSec = _statsSec;
	options.durationSec = _durationSec;
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CBotClientConfig::PrintSettingInfo()
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : BotClient Setting Info] ---------------"));
	LOG_INFO(_T("Host : %s"), _host.c_str());
	LOG_INFO(_T("Port : %d"), _port);
	LOG_INFO(_T("Bots : %d"), _botCount);
	LOG_INFO(_T("ZoneId : %d"), _zoneId);
	LOG_INFO(_T("Heroes : %s"), _heroes.c_str());
	LOG_INFO(_T("CharIdBase : %d"), _charIdBase);
	LOG_INFO(_T("RampMs : %d"), _rampMs);
	LOG_INFO(_T("TickMs : %d"), _tickMs);
	LOG_INFO(_T("MoveIntervalMs : %d"), _moveIntervalMs);
	LOG_INFO(_T("AttackIntervalMs : %d"), _attackIntervalMs);
	LOG_INFO(_T("WorkerThreadCnt : %d"), _workerThreadCnt);
	LOG_INFO(_T("StatsSec : %d"), _statsSec);
	LOG_INFO(_T("DurationSec : %d"), _durationSec);
	LOG_INFO(_T("--------------- [End Print] ---------------"));
	LOG_INFO(_T("###################################################################"));
}

//***************************************************************************
// @brief 명령행 인자를 파싱해 g_cfg에 채운다.
// @details --config <file>이 있으면 그 JSON 파일을 먼저 읽어 기본값을 바꾸고, 나머지 명령행 옵션이 그 값을 덮어쓴다
//          (우선순위: 명령행 > 설정 파일 > 내장 기본값). --dump-config <file>이 있으면 기본값 템플릿을 저장하고 종료한다.
//          --help이면 사용법을 출력하고 종료한다. 숫자가 아니거나 범위를 벗어난 값, 값이 빠진 옵션은
//          LOG_ERROR 후 false를 반환한다. 알 수 없는 옵션은 무시한다.
// @param argc 인자 개수
// @param argv 인자 배열(argv[0]은 실행 파일 경로라 건너뜀)
// @param exitCode false 반환 시 프로세스 종료 코드(--help/--dump-config 성공은 0, 그 외 1)
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

	// --dump-config는 다른 옵션과 무관하게 템플릿만 저장하고 끝낸다.
	if( args.Has(_T("dump-config")) )
	{
		_tstring dumpPath;
		if( !args.ReadString(_T("dump-config"), dumpPath) )
		{
			exitCode = 1;
			return false;
		}
		CBotClientConfig defaults;
		const bool saved = defaults.Save(dumpPath.c_str());
		std::printf(saved ? "config: default template saved \"%ls\"\n" : "config: cannot save \"%ls\" (see log)\n", dumpPath.c_str());
		exitCode = saved ? 0 : 1;
		return false;
	}

	// 설정 파일을 먼저 읽는다 — 아래 명령행 옵션이 파일 값을 덮어쓰도록 순서를 고정한다.
	if( args.Has(_T("config")) )
	{
		_tstring configPath;
		if( !args.ReadString(_T("config"), configPath) )
		{
			exitCode = 1;
			return false;
		}
		CBotClientConfig config;
		if( !config.Init(configPath.c_str()) )
		{
			std::printf("config: invalid \"%ls\" (see log)\n", configPath.c_str());
			exitCode = 1;
			return false;
		}
		config.ToOptions(g_cfg);
		std::printf("config: loaded \"%ls\"\n", configPath.c_str());
	}

	// 형식/범위 오류(오타 포함)는 LOG_ERROR 후 기동하지 않는다. 범위는 JSON(Init)과 같다.
	bool ok = true;
	ok &= args.ReadString(_T("host"), g_cfg.host);
	ok &= args.ReadInt(_T("port"), g_cfg.port, kMinPort, kMaxPort);
	ok &= args.ReadInt(_T("bots"), g_cfg.botCount, 1, 100000);
	ok &= args.ReadInt(_T("zone"), g_cfg.zoneId, 0, kMaxInt);
	ok &= args.ReadString(_T("heroes"), g_cfg.heroes);
	ok &= args.ReadInt(_T("charid-base"), g_cfg.charIdBase, 1, kMaxCharIdBase);
	ok &= args.ReadInt(_T("ramp-ms"), g_cfg.rampMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("tick-ms"), g_cfg.tickMs, 1, kMaxInt);
	ok &= args.ReadInt(_T("move-ms"), g_cfg.moveIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("attack-ms"), g_cfg.attackIntervalMs, 0, kMaxInt);
	ok &= args.ReadInt(_T("workers"), g_cfg.workerCount, 1, 1024);
	ok &= args.ReadInt(_T("stats-sec"), g_cfg.statsSec, 1, kMaxInt);
	ok &= args.ReadInt(_T("duration"), g_cfg.durationSec, 0, kMaxInt);		// 0이면 무한

	if( ok && !IsValidHeroList(g_cfg.heroes) )
	{
		LOG_ERROR(_T("[Arg] --heroes must be comma-separated numbers (e.g. 1,2,3). value=%s"), g_cfg.heroes.c_str());
		ok = false;
	}

	if( !ok )
	{
		exitCode = 1;
		return false;
	}
	return true;
}