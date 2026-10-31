// 게임 데이터 로더 테스트: 샘플 GameData 로드, 정상/비정상 JSON 검증
#include "pch.h"
#include "GameDataLoader.h"
#include "MonsterData.h"
#include "TestSuites.h"
#include "test_common.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	namespace fs = std::filesystem;

	const char* kMonster = "{\"Monster\":[{\"Id\":1,\"Name\":\"a\",\"MaxHp\":10,\"Attack\":1,\"Defense\":0,\"AttackRange\":2,\"AttackIntervalMs\":500,\"Speed\":3,\"AggroRange\":8,\"LeashRange\":20,\"RespawnMs\":1000}]}";
	const char* kSpawn = "{\"Spawn\":[{\"ZoneId\":1,\"MonsterId\":1,\"Count\":5,\"X\":10,\"Y\":10,\"Radius\":3}]}";

	//***************************************************************************
	// @brief 경로를 TCHAR 문자열로 바꿉니다.
	//***************************************************************************
	_tstring ToTString(const fs::path& path)
	{
#ifdef _UNICODE
		return path.native();
#else
		return path.string();
#endif
	}

	//***************************************************************************
	// @brief 파일에 내용을 씁니다.
	//***************************************************************************
	void WriteFile(const fs::path& path, const std::string& text)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << text;
	}

	//***************************************************************************
	// @brief Monster.json / Spawn.json을 쓰고 로드합니다.
	// @return 로드 결과 (실패하면 nullptr)
	//***************************************************************************
	std::shared_ptr<const CMonsterData> LoadFrom(const fs::path& dir, const std::string& monster, const std::string& spawn)
	{
		WriteFile(dir / "Monster.json", monster);
		WriteFile(dir / "Spawn.json", spawn);
		return CGameDataLoader::LoadMonsters(ToTString(dir));
	}

	//***************************************************************************
	// @brief 잘못된 데이터를 로더가 거부하는지 확인합니다.
	//***************************************************************************
	void ExpectRejected(const fs::path& dir, const char* what, const std::string& monster, const std::string& spawn)
	{
		const bool rejected = LoadFrom(dir, monster, spawn) == nullptr;
		if( !rejected )
			std::printf("  accepted invalid data: %s\n", what);
		CHECK(rejected);
	}

	void TestSampleData(const _tstring& dataDir)
	{
		auto data = CGameDataLoader::LoadMonsters(dataDir);
		CHECK(data != nullptr);
		if( data == nullptr )
			return;

		CHECK(data->GetMonsterCount() >= 3 && data->GetSpawns().size() >= 3);	// 샘플 데이터가 늘어나도 깨지지 않게 하한만 본다
		const SMonsterTemplate* m = data->FindMonster(2);
		CHECK(m != nullptr);
		if( m != nullptr )
		{
			CHECK(m->stats.maxHp == 300 && m->stats.attack == 18 && m->stats.defense == 4);
			CHECK(m->speed == 4.5f && m->aggroRange == 12.0f && m->respawnMs == 15000 && !m->name.empty());
		}
	}

	void TestLoadAndReject(const fs::path& dir)
	{
		CHECK(LoadFrom(dir, kMonster, kSpawn) != nullptr);
		CHECK(CGameDataLoader::LoadMonsters(ToTString(dir / "nonexistent")) == nullptr);

		ExpectRejected(dir, "스폰 키 없음", "{\"Monster\":[]}", "{}");
		ExpectRejected(dir, "몬스터 키 없음", "{}", kSpawn);
		ExpectRejected(dir, "필드 누락", "{\"Monster\":[{\"Id\":1}]}", kSpawn);
		ExpectRejected(dir, "최상위가 배열", "[]", kSpawn);
		ExpectRejected(dir, "maxHp 0", "{\"Monster\":[{\"Id\":1,\"Name\":\"a\",\"MaxHp\":0,\"Attack\":1,\"Defense\":0,\"AttackRange\":2,\"AttackIntervalMs\":500,\"Speed\":3,\"AggroRange\":8,\"LeashRange\":20,\"RespawnMs\":1000}]}", kSpawn);
		ExpectRejected(dir, "leash < aggro", "{\"Monster\":[{\"Id\":1,\"Name\":\"a\",\"MaxHp\":10,\"Attack\":1,\"Defense\":0,\"AttackRange\":2,\"AttackIntervalMs\":500,\"Speed\":3,\"AggroRange\":8,\"LeashRange\":5,\"RespawnMs\":1000}]}", kSpawn);

		const std::string one = kMonster;
		const std::string duplicated = one.substr(0, one.size() - 2) + "," + one.substr(12);		// 같은 Id의 몬스터를 두 번
		ExpectRejected(dir, "id 중복", duplicated, kSpawn);

		ExpectRejected(dir, "없는 몬스터 참조", kMonster, "{\"Spawn\":[{\"ZoneId\":1,\"MonsterId\":9,\"Count\":5,\"X\":10,\"Y\":10,\"Radius\":3}]}");
		ExpectRejected(dir, "count 0", kMonster, "{\"Spawn\":[{\"ZoneId\":1,\"MonsterId\":1,\"Count\":0,\"X\":10,\"Y\":10,\"Radius\":3}]}");
		ExpectRejected(dir, "타입 오류", kMonster, "{\"Spawn\":[{\"ZoneId\":1,\"MonsterId\":1,\"Count\":1,\"X\":\"a\",\"Y\":10,\"Radius\":3}]}");
	}
}

//***************************************************************************
// @brief 게임 데이터 로더 테스트를 실행합니다.
// @param dataDir 샘플 GameData 폴더 (Monster.json / Spawn.json)
// @return 실패한 검사가 없으면 0
//***************************************************************************
int RunGameDataTests(const _tstring& dataDir)
{
	const fs::path dir = fs::temp_directory_path() / "ZoneCoreTests_GameData";
	fs::create_directories(dir);

	TestSampleData(dataDir);
	TestLoadAndReject(dir);

	std::error_code ec;
	fs::remove_all(dir, ec);
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}