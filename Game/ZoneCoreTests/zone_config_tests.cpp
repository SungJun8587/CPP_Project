// 존 서버 설정 테스트: 기본값, 읽기, 저장 후 다시 읽기, 잘못된 값 거부
#include "pch.h"
#include "TestSuites.h"
#include "ZoneServerConfig.h"
#include "ZoneServerOptions.h"
#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	namespace fs = std::filesystem;

	_tstring ToTString(const fs::path& path)
	{
#ifdef _UNICODE
		return path.native();
#else
		return path.string();
#endif
	}

	void WriteFile(const fs::path& path, const char* text)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << text;
	}

	//***************************************************************************
	// @brief 설정 파일을 읽어 옵션으로 바꿉니다.
	// @return 읽기에 성공했으면 true
	//***************************************************************************
	bool Load(const fs::path& path, SZoneServerOptions& options)
	{
		CZoneServerConfig config;
		if( !config.Init(ToTString(path).c_str()) )
			return false;
		config.ToOptions(options);
		return true;
	}

	void TestDefaults(const fs::path& dir)
	{
		WriteFile(dir / "defaults.json", "{}");
		SZoneServerOptions o;
		CHECK(Load(dir / "defaults.json", o));
		CHECK(o.gameDataDir == _T("GameData") && o.monsterThinkIntervalMs == 100);
	}

	void TestReadAndSave(const fs::path& dir)
	{
		WriteFile(dir / "read.json", "{\"GameDataDir\":\"D:/data/live\",\"MonsterThinkIntervalMs\":250,\"PlayerMaxHp\":77}");

		CZoneServerConfig config;
		CHECK(config.Init(ToTString(dir / "read.json").c_str()));
		SZoneServerOptions o;
		config.ToOptions(o);
		CHECK(o.gameDataDir == _T("D:/data/live") && o.monsterThinkIntervalMs == 250 && o.playerMaxHp == 77);

		// 저장한 파일을 다시 읽어도 같은 값이어야 한다.
		const fs::path saved = dir / "saved.json";
		CHECK(config.Save(ToTString(saved).c_str()));
		SZoneServerOptions p;
		CHECK(Load(saved, p));
		CHECK(p.gameDataDir == _T("D:/data/live") && p.monsterThinkIntervalMs == 250);
		config.PrintServerSettingInfo();
	}

	void TestEmptyGameDataDir(const fs::path& dir)
	{
		WriteFile(dir / "empty.json", "{\"GameDataDir\":\"\"}");		// 빈 값 = 데이터 없이 구동
		SZoneServerOptions o;
		CHECK(Load(dir / "empty.json", o));
		CHECK(o.gameDataDir.empty());
	}

	void TestRejectInvalid(const fs::path& dir)
	{
		const char* invalid[] = {
			"{\"MonsterThinkIntervalMs\":0}",
			"{\"MonsterThinkIntervalMs\":70000}",
			"{\"MonsterThinkIntervalMs\":\"x\"}",
			"{\"GameDataDir\":5}",
		};
		for( const char* text : invalid )
		{
			WriteFile(dir / "invalid.json", text);
			SZoneServerOptions o;
			const bool rejected = !Load(dir / "invalid.json", o);
			if( !rejected )
				std::printf("  accepted invalid config: %s\n", text);
			CHECK(rejected);
		}
	}
}

//***************************************************************************
// @brief 존 서버 설정 테스트를 실행합니다.
// @return 실패한 검사가 없으면 0
//***************************************************************************
int RunZoneConfigTests()
{
	const fs::path dir = fs::temp_directory_path() / "ZoneCoreTests_Config";
	fs::create_directories(dir);

	TestDefaults(dir);
	TestReadAndSave(dir);
	TestEmptyGameDataDir(dir);
	TestRejectInvalid(dir);

	std::error_code ec;
	fs::remove_all(dir, ec);
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
