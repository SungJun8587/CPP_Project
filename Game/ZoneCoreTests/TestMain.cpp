// ZoneCore.Tests 진입점: 테스트 스위트를 골라 실행한다.
//
// 사용법:  ZoneCore.Tests [--fuzz] [--seeds N] [--gamedata <dir>] [suite ...]
//
//   suite : zone, combat, monster, gamedata, config, fuzzcombat, fuzzmonster, all
//   suite를 주지 않으면 단위 테스트(zone, combat, monster, gamedata, config)만 실행한다.
//   --fuzz       랜덤 연산 테스트(fuzzcombat, fuzzmonster)도 함께 실행한다. (오래 걸린다)
//   --seeds N    fuzzcombat의 시드 수 (기본 6)
//   --gamedata   샘플 GameData 폴더 (기본 GameData, 작업 디렉터리 기준)
//
// 종료 코드: 0 모두 통과, 1 실패한 스위트가 있음, 2 사용법 오류.
#include "pch.h"
#include "TestSuites.h"
#include "test_common.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace
{
	struct SSuite
	{
		const char* name;
		bool					isFuzz;
		std::function<int()>	run;
	};

	//***************************************************************************
	// @brief 명령행 인자를 스위트 이름과 비교할 수 있게 좁은 문자열로 바꿉니다. (스위트 이름은 ASCII뿐)
	//***************************************************************************
	std::string Narrow(const _tstring& text)
	{
		std::string out;
		out.reserve(text.size());
		for( const TCHAR ch : text )
			out.push_back(static_cast<char>(ch));	// ASCII만 쓰므로 손실 없음. (begin/end 생성자는 wchar_t->char 경고 C4244)
		return out;
	}

	//***************************************************************************
	// @brief 스위트 하나를 실행합니다. 스위트 사이에 실패 카운터가 섞이지 않게 먼저 0으로 되돌립니다.
	//***************************************************************************
	int RunSuite(const SSuite& suite)
	{
		std::printf("\n===== %s =====\n", suite.name);
		g_failed = 0;
		return suite.run();
	}
}

int _tmain(int argc, TCHAR* argv[])
{
	bool fuzz = false;
	int seeds = 6;
	_tstring dataDir = _T("GameData");
	std::vector<std::string> wanted;

	for( int i = 1; i < argc; ++i )
	{
		const _tstring arg = argv[i];
		if( arg == _T("--fuzz") )
			fuzz = true;
		else if( arg == _T("--seeds") && i + 1 < argc )
			seeds = _ttoi(argv[++i]);
		else if( arg == _T("--gamedata") && i + 1 < argc )
			dataDir = argv[++i];
		else if( arg.size() >= 2 && arg[0] == _T('-') && arg[1] == _T('-') )
		{
			std::fprintf(stderr, "usage: ZoneCore.Tests [--fuzz] [--seeds N] [--gamedata <dir>] [zone|combat|monster|gamedata|config|fuzzcombat|fuzzmonster|all ...]\n");
			return 2;
		}
		else
			wanted.push_back(Narrow(arg));
	}

	const std::vector<SSuite> suites =
	{
		{ "zone",		false,	[] { return RunZoneTests(); } },
		{ "combat",		false,	[] { return RunCombatTests(); } },
		{ "monster",	false,	[] { return RunMonsterTests(); } },
		{ "gamedata",	false,	[&] { return RunGameDataTests(dataDir); } },
		{ "config",		false,	[] { return RunZoneConfigTests(); } },
		{ "fuzzcombat",	true,	[&] { return RunCombatFuzz(seeds); } },
		{ "fuzzmonster",true,	[] { return RunMonsterFuzz(); } },
	};

	// 실행할 스위트 결정: 이름을 준 경우 그것만, 아니면 단위 테스트(--fuzz면 전부)
	std::vector<const SSuite*> selected;
	for( const SSuite& suite : suites )
	{
		bool pick = false;
		if( wanted.empty() )
			pick = !suite.isFuzz || fuzz;
		else
			for( const std::string& name : wanted )
				if( name == "all" || name == suite.name )
					pick = true;
		if( pick )
			selected.push_back(&suite);
	}

	for( const std::string& name : wanted )
	{
		bool known = (name == "all");
		for( const SSuite& suite : suites )
			if( name == suite.name )
				known = true;
		if( !known )
		{
			std::fprintf(stderr, "알 수 없는 스위트: %s\n", name.c_str());
			return 2;
		}
	}

	int failedSuites = 0;
	for( const SSuite* suite : selected )
		failedSuites += RunSuite(*suite) != 0 ? 1 : 0;

	std::printf("\n%d suite(s) run, %d failed\n", static_cast<int>(selected.size()), failedSuites);
	return failedSuites == 0 ? 0 : 1;
}