// 전투 퍼즈: 무작위 입장/퇴장/이동/공격/피해/리스폰/순간이동을 섞어 놓고 매 연산 후 불변식을 검사한다.
#include "pch.h"
#include "FieldZone.h"
#include "test_common.h"

#include <future>
#include <map>
#include <set>
#include <random>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로, 파일마다 헬퍼/타입 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{
struct FEnt : CZoneEntity
{
	FEnt(uint64 id, EntityType t) : CZoneEntity(id, t) {}
	std::set<uint64> seen;
protected:
	void OnAoiEnter(CZoneEntity& o) override { if( !seen.insert(o.GetId()).second ) { std::printf("  dup enter\n"); ++g_failed; } }
	void OnAoiLeave(CZoneEntity& o) override { if( seen.erase(o.GetId()) != 1 ) { std::printf("  leave w/o enter\n"); ++g_failed; } }
};

struct FZone : CFieldZone<>
{
	explicit FZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
	~FZone() override { Stop(); }
	int deaths = 0, respawns = 0, damages = 0, movesStoppedWhileDead = 0;
protected:
	void OnEntityDamaged(CZoneEntity*, CZoneEntity& v, int32 d) override
	{
		++damages;
		if( d <= 0 || v.GetHp() < 0 || v.GetHp() >= v.GetCombatStats().maxHp + 1 ) { std::printf("  bad damage hook\n"); ++g_failed; }
	}
	void OnEntityDied(CZoneEntity& v, CZoneEntity*) override
	{
		++deaths;
		if( v.IsAlive() || v.GetHp() != 0 || v.IsMoving() ) { std::printf("  bad death state\n"); ++g_failed; }
	}
	void OnEntityRespawned(CZoneEntity& e) override
	{
		++respawns;
		if( !e.IsAlive() || e.GetHp() != e.GetCombatStats().maxHp || e.IsMoving() ) { std::printf("  bad respawn state\n"); ++g_failed; }
		if( e.GetPos().x != e.GetSpawnPos().x || e.GetPos().y != e.GetSpawnPos().y ) { std::printf("  respawn not at spawn\n"); ++g_failed; }
	}
};

static void CheckInvariants(FZone& z, const std::vector<uint64>& ids)
{
	size_t live = 0;
	for( uint64 id : ids )
	{
		CZoneEntity* e = z.FindEntity(id);
		if( e == nullptr ) continue;
		++live;
		CHECK(e->GetHp() >= 0 && e->GetHp() <= e->GetCombatStats().maxHp);
		CHECK(e->IsAlive() == (e->GetHp() > 0));
		if( !e->IsAlive() ) CHECK(!e->IsMoving());
		// AOI 대칭: a가 b를 보면 b도 a를 본다 (히스테리시스 포함 동일 규칙)
		FEnt* fe = static_cast<FEnt*>(e);
		for( uint64 other : fe->seen )
		{
			FEnt* o = static_cast<FEnt*>(z.FindEntity(other));
			CHECK(o != nullptr && o->seen.count(id) == 1);
		}
	}
	CHECK(live == z.GetEntityCount());
}

int Main(int seeds)
{
	for( int seed = 1; seed <= seeds; ++seed )
	{
		std::printf("[fuzz seed %d]\n", seed);
		FieldZoneConfig cfg;
		cfg.zone.zoneId = 1; cfg.zone.tickIntervalMs = 5;
		cfg.minX = 0; cfg.minY = 0; cfg.maxX = 300; cfg.maxY = 300;
		cfg.cellSize = 50; cfg.viewCells = 1;
		cfg.respawnDelayMs = 20; cfg.respawnInvulnMs = (seed % 2) ? 15 : 0;
		cfg.allowPvp = (seed % 3) != 0;
		cfg.minMoveIntervalMs = 5;
		FZone z(cfg);
		z.Start();

		std::mt19937 rng(seed);				// 메인 스레드 전용 (연산 선택)
		std::mt19937 zrng(seed + 1000);		// 존 스레드 전용 (작업 안의 세부 값)
		std::vector<uint64> ids; for( uint64 i = 1; i <= 24; ++i ) ids.push_back(i);
		auto rnd = [&](int n) { return (int)(rng() % n); };
		auto pos = [&] { return Vec2{ (float)rnd(300), (float)rnd(300) }; };

		for( int step = 0; step < 4000; ++step )
		{
			const uint64 id = ids[rnd((int)ids.size())];
			const uint64 other = ids[rnd((int)ids.size())];
			const int op = rnd(10);
			const Vec2 p = pos();
			const int hpv = 5 + rnd(60), atk = rnd(40);
			const int tp = rnd(3);

			z.Post([&z, &zrng, &ids, op, id, other, p, hpv, atk, tp](CZone&)
			{
				auto rnd = [&](int n) { return (int)(zrng() % n); };
				switch( op )
				{
				case 0: case 1:		// 입장
				{
					auto e = std::make_unique<FEnt>(id, tp == 0 ? EntityType::Monster : EntityType::Player);
					e->SetSpawnPos(p);
					SCombatStats s; s.maxHp = hpv; s.attack = atk; s.defense = rnd(10); s.attackRange = 20.0f; s.attackIntervalMs = 1 + rnd(30);
					e->SetCombatStats(s); e->SetSpeed((float)rnd(60));
					z.AddEntity(std::move(e));
					break;
				}
				case 2: z.RemoveEntity(id); break;
				case 3: case 4:
					if( auto* e = z.FindEntity(id) ) z.RequestMove(*e, rnd(4) == 0 ? p : e->GetPos(), p);
					break;
				case 5: case 6: case 7:
					if( auto* e = z.FindEntity(id) ) z.RequestAttack(*e, other);
					break;
				case 8:
					if( auto* e = z.FindEntity(id) ) z.DealDamage(*e, rnd(2) ? z.FindEntity(other) : nullptr, rnd(50) - 5);
					break;
				case 9:
					if( rnd(2) ) z.Teleport(id, p); else z.Respawn(id);
					break;
				}
				CheckInvariants(z, ids);
			});
			// Post된 작업이 끝나길 기다리는 대신 짧게 양보하며 틱이 진행되게 한다.
			if( step % 7 == 0 ) SleepMs(1);
		}
		// 마무리: 남은 작업/타이머가 끝나도록 대기 후 최종 점검
		SleepMs(100);
		std::promise<void> pr; auto fu = pr.get_future();
		z.Post([&](CZone&) { CheckInvariants(z, ids); pr.set_value(); });
		fu.wait();
		z.Stop();
		std::printf("  deaths=%d respawns=%d damages=%d\n", z.deaths, z.respawns, z.damages);
	}
	std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
	return g_failed ? 1 : 0;
}
}

int RunCombatFuzz(int seeds)
{
	return Main(seeds);
}
