// 몬스터 스폰/AI/어그로 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "MonsterSystem.h"
#include "test_common.h"

#include <cmath>
#include <cstdio>
#include <future>
#include <map>
#include <memory>
#include <random>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로, 파일마다 헬퍼/타입 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{
	struct MonZone : CFieldZone<>
	{
		explicit MonZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
		~MonZone() override { Stop(); }

		std::unique_ptr<CMonsterSystem> sys;
		int died = 0, respawned = 0, healed = 0;

	protected:
		void OnUpdate(uint32 dt) override { CFieldZone<>::OnUpdate(dt); if( sys ) sys->Update(); }
		void OnEntityDamaged(CZoneEntity* a, CZoneEntity& v, int32 d) override { if( sys ) sys->OnDamaged(a, v, d); }
		void OnEntityDied(CZoneEntity& v, CZoneEntity*) override { ++died; if( sys ) sys->OnDied(v); }
		void OnEntityRespawned(CZoneEntity& e) override { ++respawned; if( sys ) sys->OnRespawned(e); }
		void OnEntityRemoved(CZoneEntity& e) override { if( sys ) sys->OnRemoved(e); }
		void OnEntityHealed(CZoneEntity&, int32) override { ++healed; }
		uint32 GetRespawnDelayMs(const CZoneEntity& v) const override
		{
			if( sys && sys->IsAiMonster(v) ) return sys->GetRespawnDelayMs(v);
			return CFieldZone<>::GetRespawnDelayMs(v);
		}
		Vec2 GetRespawnPos(const CZoneEntity& e) const override
		{
			Vec2 p;
			if( sys && sys->GetRespawnPos(e, p) ) return p;
			return CFieldZone<>::GetRespawnPos(e);
		}
	};

	struct Pl : CZoneEntity { Pl(uint64 id) : CZoneEntity(id, EntityType::Player) {} };

	static FieldZoneConfig Cfg()
	{
		FieldZoneConfig f;
		f.zone.zoneId = 1; f.zone.tickIntervalMs = 10;
		f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
		f.cellSize = 50; f.viewCells = 3;
		f.respawnDelayMs = 100; f.allowPvp = false; f.minDamage = 1; f.respawnInvulnMs = 0;
		return f;
	}

	static SMonsterTemplate Tmpl(uint32 id)
	{
		SMonsterTemplate t;
		t.id = id; t.name = "m";
		t.stats.maxHp = 100; t.stats.attack = 10; t.stats.defense = 0;
		t.stats.attackRange = 2.0f; t.stats.attackIntervalMs = 100;
		t.speed = 40.0f; t.aggroRange = 10.0f; t.leashRange = 30.0f; t.respawnMs = 500;
		return t;
	}

	static std::shared_ptr<CMonsterData> Data(const SMonsterTemplate& t, uint32 count, float x, float y, float r)
	{
		auto d = std::make_shared<CMonsterData>();
		d->AddMonster(t);
		SSpawnEntry s; s.zoneId = 1; s.monsterId = t.id; s.count = count; s.x = x; s.y = y; s.radius = r;
		d->AddSpawn(s);
		return d;
	}

	template <typename F> static void Run(MonZone& z, F fn);

	static CZoneEntity* AddPlayer(MonZone& z, uint64 id, Vec2 pos, int hp = 1000, int atk = 30)
	{
		if( z.IsRunning() && !z.IsZoneThread() )
		{
			CZoneEntity* r = nullptr;
			Run(z, [&] { r = AddPlayer(z, id, pos, hp, atk); });
			return r;
		}

		auto e = std::make_unique<Pl>(id);
		e->SetSpawnPos(pos);
		SCombatStats s; s.maxHp = hp; s.attack = atk; s.defense = 0; s.attackRange = 2.0f; s.attackIntervalMs = 100;
		e->SetCombatStats(s);
		e->SetSpeed(40.0f);
		return z.AddEntity(std::move(e));
	}

	template <typename F> static void Run(MonZone& z, F fn)
	{
		std::promise<void> p; auto f = p.get_future();
		z.Post([&](CZone&) { fn(); p.set_value(); });
		f.wait();
	}

	static CMonster* Mon(MonZone& z, uint64 serial) { return static_cast<CMonster*>(z.FindEntity(kMonsterIdBase | serial)); }

	static void Setup(MonZone& z, std::shared_ptr<const CMonsterData> d)
	{
		SMonsterSystemConfig c; c.thinkIntervalMs = 20;
		z.sys = std::make_unique<CMonsterSystem>(z, d, c);
	}


	static void CheckInvariants(MonZone& z, size_t monsters, const std::vector<uint64>& playerIds)
	{
		size_t activeSeen = 0;
		for( uint64 s = 1; s <= monsters; ++s )
		{
			CMonster* m = Mon(z, s);
			CHECK(m != nullptr);
			if( m == nullptr ) continue;
			CHECK(m->GetHp() >= 0 && m->GetHp() <= m->GetCombatStats().maxHp);
			CHECK(m->IsAlive() == (m->GetHp() > 0));
			CHECK(m->GetAggroCount() <= CMonster::kMaxAggro);
			const Vec2 p = m->GetPos();
			CHECK(p.x >= 0 && p.x <= 1000 && p.y >= 0 && p.y <= 1000);

			uint32 nearCount = 0;
			z.ForEachInAoi(*m, [&](CZoneEntity& o) { if( o.GetType() == EntityType::Player ) ++nearCount; });
			CHECK(nearCount == m->GetNearPlayers());

			if( !m->IsAlive() )
			{
				CHECK(!m->IsActive());
				CHECK(m->GetAggroCount() == 0);
			}
			else if( m->GetNearPlayers() > 0 )
			{
				CHECK(m->IsActive());					// 시야에 플레이어가 있으면 반드시 AI 목록에 있다
			}
			if( m->GetState() == EMonsterState::Return ) CHECK(m->GetAggroCount() == 0);
			if( m->IsActive() ) ++activeSeen;
		}
		CHECK(activeSeen == z.sys->GetActiveCount());
		for( uint64 id : playerIds )
			if( CZoneEntity* e = z.FindEntity(id) ) CHECK(e->GetHp() >= 0 && e->GetHp() <= e->GetCombatStats().maxHp);
	}

	int Main()
	{
		std::printf("[Monster fuzz]\n");
		FieldZoneConfig cfg = Cfg();
		cfg.maxX = 400; cfg.maxY = 400;
		MonZone z(cfg);
		SMonsterTemplate t = Tmpl(1); t.stats.maxHp = 60; t.respawnMs = 200; t.leashRange = 25;
		auto d = Data(t, 150, 200, 200, 120);
		Setup(z, d);
		std::string err; CHECK(z.sys->Validate(err));
		CHECK(z.sys->SpawnAll() == 150);
		z.Start();

		std::mt19937 rng(12345);
		std::vector<uint64> ids;
		uint64 next = 1;
		std::map<uint64, bool> alive;
		auto rnd = [&](int n) { return (int)(rng() % (unsigned)n); };
		auto rpos = [&]() { return Vec2{ 100.0f + rnd(200), 100.0f + rnd(200) }; };

		for( int step = 0; step < 600; ++step )
		{
			Run(z, [&]
				{
					for( int k = 0; k < 6; ++k )
					{
						switch( rnd(8) )
						{
						case 0: case 1:
							if( ids.size() < 30 ) { uint64 id = next++; AddPlayer(z, id, rpos(), 40 + rnd(400), 5 + rnd(40)); ids.push_back(id); }
							break;
						case 2:
							if( !ids.empty() ) { size_t i = rnd((int)ids.size()); z.RemoveEntity(ids[i]); ids.erase(ids.begin() + i); }
							break;
						case 3:
							if( !ids.empty() ) z.Teleport(ids[rnd((int)ids.size())], rpos());
							break;
						case 4:
							if( !ids.empty() ) { CZoneEntity* e = z.FindEntity(ids[rnd((int)ids.size())]); if( e ) z.MoveEntity(*e, rpos()); }
							break;
						case 5: case 6:
							if( !ids.empty() ) z.RequestAttack(ids[rnd((int)ids.size())], kMonsterIdBase | (1 + rnd(150)));
							break;
						case 7:
							if( !ids.empty() ) { CZoneEntity* m = z.FindEntity(kMonsterIdBase | (1 + rnd(150))); CZoneEntity* a = z.FindEntity(ids[rnd((int)ids.size())]); if( m && a ) z.DealDamage(*m, a, 1 + rnd(90)); }
							break;
						}
					}
					CheckInvariants(z, 150, ids);
				});
			if( step % 3 == 0 ) SleepMs(5);
		}

		for( uint64 id : ids ) Run(z, [&] { z.RemoveEntity(id); });
		SleepMs(2500);
		Run(z, [&] { CheckInvariants(z, 150, {}); CHECK(z.sys->GetActiveCount() == 0); });
		z.Stop();
		std::printf("  stats: died=%d respawned=%d healed=%d\n", z.died, z.respawned, z.healed);
		std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
		return g_failed ? 1 : 0;
	}
}

int RunMonsterFuzz()
{
	return Main();
}