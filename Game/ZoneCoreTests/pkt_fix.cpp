#include "pch.h"
#include "ZoneServerMain.h"
#include "BattleZone.h"
#include <future>
#include <thread>
#include <chrono>
static int g_failed = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("  FAIL %d %s\n", __LINE__, #c); ++g_failed; } }while(0)
template<class F> static void Run(CBattleZone& z, F fn){ std::promise<void> p; auto f=p.get_future(); z.Post([&](CZone&){ fn(); p.set_value(); }); f.wait(); }
static void Sleep(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static FieldZoneConfig Cfg(){ FieldZoneConfig c; c.zone.zoneId = 1; c.zone.tickIntervalMs = 10; c.respawnDelayMs = 100; c.respawnInvulnMs = 0; c.cellSize = 50; c.viewCells = 3; return c; }
static SCombatStats Stats(){ SCombatStats s; s.maxHp = 500; s.attack = 40; s.defense = 0; s.attackRange = 3; s.attackIntervalMs = 100; return s; }
static std::shared_ptr<CMonsterData> Data(float speed, float x)
{
	auto d = std::make_shared<CMonsterData>();
	SMonsterTemplate t; t.id = 7; t.name = "wolf"; t.stats.maxHp = 100; t.stats.attack = 10; t.stats.attackRange = 2; t.stats.attackIntervalMs = 100;
	t.speed = speed; t.aggroRange = 12; t.leashRange = 40; t.respawnMs = 300; d->AddMonster(t);
	SSpawnEntry sp; sp.zoneId = 1; sp.monsterId = 7; sp.count = 1; sp.x = x; sp.y = 100; sp.radius = 0; d->AddSpawn(sp);
	return d;
}

int main()
{
	SMonsterSystemConfig mc; mc.thinkIntervalMs = 20;

	// ValidateMonsters: 플레이어 속도 / 스폰 위치 / 사거리 검증
	{
		CZoneServerMain srv; std::string err;
		{ CBattleZone z(Cfg(), &srv, Vec2{100,100}, 0.0f, Stats(), Data(40,130), mc); CHECK(!z.ValidateMonsters(err)); }
		{ CBattleZone z(Cfg(), &srv, Vec2{100,100}, std::numeric_limits<float>::quiet_NaN(), Stats(), Data(40,130), mc); CHECK(!z.ValidateMonsters(err)); }
		{ CBattleZone z(Cfg(), &srv, Vec2{-5000,100}, 6.0f, Stats(), Data(40,130), mc); CHECK(!z.ValidateMonsters(err)); }
		{ SCombatStats s = Stats(); s.attackRange = 500; CBattleZone z(Cfg(), &srv, Vec2{100,100}, 6.0f, s, Data(40,130), mc); CHECK(!z.ValidateMonsters(err)); }
		{ CBattleZone z(Cfg(), &srv, Vec2{100,100}, 6.0f, Stats(), Data(40,130), mc); CHECK(z.ValidateMonsters(err)); }
	}

	// 플레이어가 없으면 몬스터 이동/전투 브로드캐스트를 만들지 않는다
	{
		CZoneServerMain srv;
		CBattleZone z(Cfg(), &srv, Vec2{100,100}, 6.0f, Stats(), Data(40,130), mc);
		std::string err; CHECK(z.ValidateMonsters(err)); CHECK(z.SpawnMonsters() == 1);
		z.Start(); Sleep(300);
		CHECK(srv.multicastCalls == 0);
		z.Stop();
	}

	{
		CZoneServerMain srv;
		CBattleZone z(Cfg(), &srv, Vec2{100,100}, 6.0f, Stats(), Data(40,130), mc);
		std::string err; CHECK(z.ValidateMonsters(err)); CHECK(z.SpawnMonsters() == 1);
		z.Start();

		// charId 0 은 "없음"이므로 입장 거부
		size_t gw0 = srv.gw.size();
		Run(z,[&]{ z.HandleEnter(50, 0); });
		CHECK(srv.gw.size() == gw0 + 1);
		auto* res = (const ZoneEnterResPacket*)srv.gw.back().data();
		CHECK(res->success == 0 && res->reason == (uint8)EZoneEnterResult::InvalidCharId);
		Run(z,[&]{ CHECK(z.GetPlayerCount() == 0); });

		// 퇴장하는 플레이어는 자기 시야의 엔티티 디스폰 패킷을 받지 않는다 (이미 세션이 사라졌을 수 있다)
		Run(z,[&]{ z.HandleEnter(11, 1); });
		CHECK(srv.Count(11, EZoneClientPacketType::ZcSpawnNotify) >= 1);	// 몬스터 스폰은 받았다
		int before = srv.Count(11, EZoneClientPacketType::ZcDespawnNotify);
		Run(z,[&]{ z.HandleLeave(11, 1); });
		CHECK(srv.Count(11, EZoneClientPacketType::ZcDespawnNotify) == before);
		Run(z,[&]{ CHECK(z.GetPlayerCount() == 0); });
		z.Stop();
	}
	std::printf(g_failed ? "FAILED %d\n" : "ALL PASSED\n", g_failed);
	return g_failed;
}
