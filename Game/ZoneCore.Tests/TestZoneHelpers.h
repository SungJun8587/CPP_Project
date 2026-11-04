// 존 테스트 공용 도우미: 존 스레드에서 실행, 테스트용 플레이어 추가
#pragma once

#include "FieldZone.h"

#include <future>
#include <memory>

// 테스트 파일마다 쓰는 존 타입이 달라 템플릿으로 둔다 (존은 CFieldZone<> 파생이어야 한다).
namespace TestZone
{
	//***************************************************************************
	// @brief 테스트용 플레이어 엔티티.
	//***************************************************************************
	struct Player : CZoneEntity
	{
		explicit Player(uint64 id) : CZoneEntity(id, EntityType::Player) {}
	};

	//***************************************************************************
	// @brief 존 스레드에서 fn을 실행하고 끝날 때까지 기다립니다. 존이 Start()된 뒤에만 쓴다.
	//***************************************************************************
	template <typename TZone, typename F>
	void RunOnZone(TZone& z, F fn)
	{
		std::promise<void> p;
		auto f = p.get_future();
		z.Post([&](CZone&) { fn(); p.set_value(); });
		f.wait();
	}

	//***************************************************************************
	// @brief 테스트용 플레이어(사거리 2, 공격 간격 100ms, 속도 40)를 추가합니다.
	//        존이 돌고 있으면 존 스레드에서 추가한다.
	//***************************************************************************
	template <typename TZone>
	CZoneEntity* AddTestPlayer(TZone& z, uint64 id, Vec2 pos, int hp, int atk)
	{
		if( z.IsRunning() && !z.IsZoneThread() )
		{
			CZoneEntity* r = nullptr;
			RunOnZone(z, [&] { r = AddTestPlayer(z, id, pos, hp, atk); });
			return r;
		}

		auto e = std::make_unique<Player>(id);
		e->SetSpawnPos(pos);
		SCombatStats s;
		s.maxHp = hp; s.attack = atk; s.defense = 0; s.attackRange = 2.0f; s.attackIntervalMs = 100;
		e->SetCombatStats(s);
		e->SetSpeed(40.0f);
		return z.AddEntity(std::move(e));
	}
}
