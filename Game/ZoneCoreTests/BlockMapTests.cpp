// 장애물 맵(CBlockMap)과 존 이동 충돌 단위 테스트 - 스텁 환경
#include "pch.h"
#include "FieldZone.h"
#include "BlockMap.h"
#include "TestCommon.h"

#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <random>
#include <utility>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{

	struct BlockEntity : CZoneEntity
	{
		BlockEntity(uint64 id, EntityType t) : CZoneEntity(id, t) {}
	};

	struct BlockZone : CFieldZone<>
	{
		explicit BlockZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
		~BlockZone() override { Stop(); }

		int moveRejected = 0;
		MoveResult lastMoveReject = MoveResult::Ok;

	protected:
		void OnMoveRejected(CZoneEntity&, MoveResult r) override { ++moveRejected; lastMoveReject = r; }
	};

	template <typename F>
	static void Run(BlockZone& z, F fn)
	{
		std::promise<void> p;
		auto f = p.get_future();
		z.Post([&](CZone&) { fn(); p.set_value(); });
		f.wait();
	}

	// 셀 크기 1.0: 선분을 아주 잘게 샘플링해 시작 셀 이후에 차단 셀이 없는지 확인한다.
	static bool SampledClear(const CBlockMap& m, Vec2 a, Vec2 b)
	{
		const int n = 4000;
		const int sx = (int)std::floor(a.x), sy = (int)std::floor(a.y);
		for( int i = 0; i <= n; ++i )
		{
			const double t = (double)i / n;
			Vec2 p{ (float)(a.x + (b.x - a.x) * t), (float)(a.y + (b.y - a.y) * t) };
			if( (int)std::floor(p.x) == sx && (int)std::floor(p.y) == sy ) continue;
			if( m.IsBlocked(p) ) return false;
		}
		return true;
	}

	static void TestMap()
	{
		CBlockMap m;
		CHECK(!m.Init(0, 0, 100, 100, 0.1f));						// 셀 크기 하한
		CHECK(!m.Init(0, 0, 0, 100, 1));
		CHECK(!m.Init(0, 0, 1e6f, 1e6f, 1));						// 셀 수 상한
		CHECK(m.Init(0, 0, 100, 100, 1));
		CHECK(m.GetCellsX() == 100 && m.GetCellsY() == 100);

		m.AddRect(40, 40, 10, 10);									// 셀 경계에 정확히 맞는 사각형 -> 정확히 100셀
		CHECK(m.GetBlockedCellCount() == 100);
		CHECK(m.IsBlocked({ 40, 40 }) && m.IsBlocked({ 49.9f, 49.9f }) && !m.IsBlocked({ 50.0f, 50.0f }) && !m.IsBlocked({ 39.9f, 45 }));

		// 정면 충돌: 왼쪽에서 오른쪽으로
		bool hit = false;
		Vec2 r = m.ClampMove({ 10, 45 }, { 90, 45 }, &hit);
		CHECK(hit && r.x < 40.0f && r.x > 39.9f && r.y == 45.0f);
		CHECK(!m.IsBlocked(r));
		// 막힘 없음: 위쪽을 지남
		r = m.ClampMove({ 10, 60 }, { 90, 60 }, &hit);
		CHECK(!hit && r.x == 90.0f);
		// 대각선 충돌
		r = m.ClampMove({ 20, 20 }, { 70, 70 }, &hit);
		CHECK(hit && r.x < 41 && r.y < 41 && !m.IsBlocked(r));
		// 시작 셀만 검사에서 제외한다. 차단 영역 안쪽에서 시작하면 다음 차단 셀에서 막히고, 가장자리 셀에서 바깥으로 나가는 이동은 허용된다.
		r = m.ClampMove({ 45, 45 }, { 20, 45 }, &hit);
		CHECK(hit && m.IsBlocked(r));
		CHECK(m.IsSegmentClear({ 40.5f, 45.5f }, { 20, 45.5f }));			// 가장자리 셀에서 바깥으로 빠져나감
		// 같은 셀 안 이동, 영점 이동
		CHECK(m.IsSegmentClear({ 10.2f, 10.2f }, { 10.8f, 10.8f }) && m.IsSegmentClear({ 10, 10 }, { 10, 10 }));
		// NaN/영역 밖
		CHECK(!m.IsBlocked({ NAN, 1 }) && !m.IsBlocked({ -5, 1 }) && !m.IsBlocked({ 500, 1 }));

		// 원: 중심 (20,20) 반경 3
		CBlockMap c; CHECK(c.Init(0, 0, 100, 100, 1));
		c.AddCircle(20, 20, 3);
		CHECK(c.IsBlocked({ 20, 20 }) && c.IsBlocked({ 22.5f, 20 }) && !c.IsBlocked({ 24.0f, 20 }) && !c.IsBlocked({ 20, 16.5f }));
		CHECK(c.GetBlockedCellCount() >= 28 && c.GetBlockedCellCount() <= 40);

		// 퍼즈: 샘플링으로 "막힘 없음"/"클램프 지점까지 막힘 없음"을 교차 검증
		CBlockMap f; CHECK(f.Init(0, 0, 64, 64, 1));
		std::mt19937 rng(12345);
		std::uniform_real_distribution<float> U(0.0f, 64.0f);
		for( int i = 0; i < 12; ++i ) f.AddRect(U(rng), U(rng), 1 + U(rng) * 0.1f, 1 + U(rng) * 0.1f);
		for( int i = 0; i < 8; ++i ) f.AddCircle(U(rng), U(rng), 1 + U(rng) * 0.05f);
		int clearCount = 0, hitCount = 0, bad = 0;
		for( int i = 0; i < 20000; ++i )
		{
			Vec2 a{ U(rng), U(rng) }, b{ U(rng), U(rng) };
			if( f.IsBlocked(a) ) continue;
			bool h = false;
			Vec2 e = f.ClampMove(a, b, &h);
			if( !h ) { ++clearCount; if( !SampledClear(f, a, b) ) ++bad; }
			else { ++hitCount; if( f.IsBlocked(e) || !SampledClear(f, a, e) ) ++bad; }
			if( f.IsSegmentClear(a, b) != !h ) ++bad;
		}
		std::printf("  fuzz: clear=%d hit=%d bad=%d\n", clearCount, hitCount, bad);
		CHECK(bad == 0 && clearCount > 1000 && hitCount > 1000);

		// 큰 좌표(float ulp > kBackoff)에서도 ClampMove 결과가 차단 셀 안에 들어가지 않는다
		{
			CBlockMap m;
			CHECK(m.Init(200000.f, 200000.f, 200100.f, 200100.f, 1.0f));
			m.AddRect(200050.f, 200000.f, 3.f, 100.f);
			m.AddCircle(200020.f, 200030.f, 7.f);
			unsigned bad = 0, hits = 0;
			unsigned s = 12345;
			auto rnd = [&] { s = s * 1664525u + 1013904223u; return (s >> 8) / float(1 << 24); };
			for( int i = 0; i < 20000; ++i )
			{
				Vec2 a{ 200000.f + rnd() * 100.f, 200000.f + rnd() * 100.f };
				Vec2 b{ 200000.f + rnd() * 100.f, 200000.f + rnd() * 100.f };
				if( m.IsBlocked(a) ) continue;
				bool hit = false;
				Vec2 r = m.ClampMove(a, b, &hit);
				if( hit ) { ++hits; if( m.IsBlocked(r) ) ++bad; }
			}
			std::printf("  large-coord clamp: hits=%u bad=%u\n", hits, bad);
			CHECK(hits > 1000 && bad == 0);
		}
	}

	// 독립 구현(BFS)으로 두 셀이 8방향(모서리 가로지르기 금지)으로 이어지는지 확인한다.
	static bool BfsReachable(const CBlockMap& m, int sx, int sy, int gx, int gy)
	{
		const int W = m.GetCellsX(), H = m.GetCellsY();
		std::vector<char> seen(static_cast<size_t>(W) * H, 0);
		std::vector<std::pair<int, int>> q;
		q.push_back({ sx, sy });
		seen[static_cast<size_t>(sy) * W + sx] = 1;
		for( size_t h = 0; h < q.size(); ++h )
		{
			const int cx = q[h].first, cy = q[h].second;
			if( cx == gx && cy == gy ) return true;
			for( int dy = -1; dy <= 1; ++dy )
				for( int dx = -1; dx <= 1; ++dx )
				{
					if( !dx && !dy ) continue;
					const int nx = cx + dx, ny = cy + dy;
					if( nx < 0 || ny < 0 || nx >= W || ny >= H || m.IsCellBlocked(nx, ny) ) continue;
					if( dx && dy && (m.IsCellBlocked(cx + dx, cy) || m.IsCellBlocked(cx, cy + dy)) ) continue;
					if( seen[static_cast<size_t>(ny) * W + nx] ) continue;
					seen[static_cast<size_t>(ny) * W + nx] = 1;
					q.push_back({ nx, ny });
				}
		}
		return false;
	}

	static float PathLength(Vec2 from, const std::vector<Vec2>& path)
	{
		float len = 0;
		for( const Vec2& p : path ) { len += std::sqrt((p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y)); from = p; }
		return len;
	}

	static void TestPath()
	{
		std::printf("[Block pathfinding]\n");
		CBlockMap m;
		CHECK(m.Init(0, 0, 100, 100, 1.0f));
		m.AddRect(50, 0, 2, 60);					// 위쪽(y 60..100)이 열린 벽
		std::vector<Vec2> path;

		// 직선이 비어 있으면 목적지 한 점
		CHECK(m.FindPath({ 10, 10 }, { 30, 30 }, path) && path.size() == 1 && path[0].x == 30.0f);

		// 벽 너머: 틈으로 돌아간다. 모든 구간이 직선으로 통과 가능하고 마지막은 목적지다.
		const Vec2 a{ 40, 30 }, b{ 60, 30 };
		CHECK(!m.IsSegmentClear(a, b));
		CHECK(m.FindPath(a, b, path) && path.size() >= 2);
		if( !path.empty() )
		{
			Vec2 prev = a; bool allClear = true;
			for( const Vec2& p : path ) { if( !m.IsSegmentClear(prev, p) ) allClear = false; prev = p; }
			CHECK(allClear && path.back().x == b.x && path.back().y == b.y);
			const float direct = 20.0f, len = PathLength(a, path);
			std::printf("  detour waypoints=%zu length=%.1f (direct %.1f)\n", path.size(), len, direct);
			CHECK(len > direct && len < 100.0f);
		}

		// 목적지가 차단 셀 안이면 실패, 셀 수 상한이 작으면 실패
		CHECK(!m.FindPath(a, { 51, 30 }, path));
		CHECK(!m.FindPath(a, b, path, 5));

		// 완전히 막힌 벽이면 실패하고(맵 전체를 뒤지지 않고 상한에서 끝난다) 비용이 묶인다
		CBlockMap closed; CHECK(closed.Init(0, 0, 100, 100, 1.0f));
		closed.AddRect(50, 0, 2, 100);
		CHECK(!closed.FindPath({ 40, 30 }, { 60, 30 }, path));
		CHECK(!closed.FindPath({ 40, 30 }, { 60, 30 }, path, 300));

		// NaN 입력
		CHECK(!m.FindPath({ NAN, 1 }, b, path));

		// 퍼징: 도달 가능 여부를 독립 BFS와 비교하고, 경로의 모든 구간이 통과 가능한지 확인한다.
		CBlockMap f; CHECK(f.Init(0, 0, 64, 64, 1));
		std::mt19937 rng(777);
		std::uniform_real_distribution<float> U(0.0f, 64.0f);
		for( int i = 0; i < 14; ++i ) f.AddRect(U(rng), U(rng), 1 + U(rng) * 0.25f, 1 + U(rng) * 0.25f);
		for( int i = 0; i < 10; ++i ) f.AddCircle(U(rng), U(rng), 1 + U(rng) * 0.08f);
		int found = 0, notFound = 0, bad = 0, detours = 0;
		for( int i = 0; i < 3000; ++i )
		{
			Vec2 s{ U(rng), U(rng) }, g{ U(rng), U(rng) };
			const int sx = (int)std::floor(s.x), sy = (int)std::floor(s.y), gx = (int)std::floor(g.x), gy = (int)std::floor(g.y);
			if( f.IsCellBlocked(sx, sy) || f.IsCellBlocked(gx, gy) ) continue;
			const bool reachable = BfsReachable(f, sx, sy, gx, gy);
			const bool ok = f.FindPath(s, g, path, 100000);
			if( ok != reachable ) { ++bad; continue; }
			if( !ok ) { ++notFound; continue; }
			++found;
			if( path.empty() || path.back().x != g.x || path.back().y != g.y ) { ++bad; continue; }
			if( !f.IsSegmentClear(s, g) ) ++detours;
			Vec2 prev = s;
			for( const Vec2& p : path ) { if( !f.IsSegmentClear(prev, p) ) { ++bad; break; } prev = p; }
		}
		std::printf("  path fuzz: found=%d (detours=%d) unreachable=%d bad=%d\n", found, detours, notFound, bad);
		CHECK(bad == 0 && found > 500 && detours > 50);
	}

	static CZoneEntity* AddAt(BlockZone& z, uint64 id, Vec2 pos, float speed)
	{
		auto e = std::make_unique<BlockEntity>(id, EntityType::Player);
		e->SetSpawnPos(pos);
		e->SetSpeed(speed);
		return z.AddEntity(std::move(e));
	}

	static void TestZoneCollision()
	{
		std::printf("[Block zone movement]\n");
		auto bm = std::make_shared<CBlockMap>();
		CHECK(bm->Init(0, 0, 1000, 1000, 1.0f));
		bm->AddRect(150, 0, 10, 1000);			// x=150..160 세로 벽(전체 높이)

		FieldZoneConfig cfg;
		cfg.zone.zoneId = 1; cfg.zone.tickIntervalMs = 10;
		cfg.minX = 0; cfg.minY = 0; cfg.maxX = 1000; cfg.maxY = 1000;
		cfg.cellSize = 50; cfg.viewCells = 1; cfg.minMoveIntervalMs = 0;
		cfg.blockMap = bm;
		BlockZone z(cfg);
		CHECK(AddAt(z, 1, { 100, 100 }, 50.0f) != nullptr);
		CHECK(AddAt(z, 2, { 155, 100 }, 50.0f) == nullptr);		// 장애물 안에는 입장할 수 없다
		z.Start();

		MoveResult r{};
		Vec2 dest{};
		Run(z, [&]
			{
				// 벽 너머로 요청 -> 목적지가 벽 직전으로 줄어든다
				r = z.RequestMove(1, { 100, 100 }, { 250, 100 });
				dest = z.FindEntity(1)->GetDest();
			});
		CHECK(r == MoveResult::Ok && dest.x < 150.0f && dest.x > 149.9f && dest.y == 100.0f);
		CHECK(z.moveRejected == 0);

		// 벽에 붙어 있으면 줄어든 이동이 너무 짧아 거부된다
		Run(z, [&]
			{
				z.Teleport(1, { 149.995f, 100 });
				r = z.RequestMove(1, { 149.995f, 100 }, { 250, 100 });
			});
		CHECK(r == MoveResult::Blocked && z.moveRejected == 1 && z.lastMoveReject == MoveResult::Blocked);

		// Teleport: 장애물 안은 거부, 밖은 허용
		bool inWall = true, outside = false;
		Run(z, [&] { inWall = z.Teleport(1, { 155, 100 }); outside = z.Teleport(1, { 200, 100 }); });
		CHECK(!inWall && outside);

		// 서버 주도 이동도 같은 규칙: 벽 쪽으로 가게 하면 벽 직전에서 멈춘다
		bool moved = false;
		Run(z, [&]
			{
				moved = z.MoveEntity(*z.FindEntity(1), { 100, 100 });
				dest = z.FindEntity(1)->GetDest();
			});
		CHECK(moved && dest.x > 160.0f && dest.x < 160.1f);	// 벽(150..160) 오른쪽 면 직전

		z.Stop();
	}

	static CZoneEntity* AddCombat(BlockZone& z, uint64 id, EntityType type, Vec2 pos, float range)
	{
		auto e = std::make_unique<BlockEntity>(id, type);
		e->SetSpawnPos(pos);
		SCombatStats s; s.maxHp = 1000; s.attack = 10; s.defense = 0; s.attackRange = range; s.attackIntervalMs = 10;
		e->SetCombatStats(s);
		return z.AddEntity(std::move(e));
	}

	static void TestLineOfSight()
	{
		std::printf("[Block line of sight]\n");
		auto bm = std::make_shared<CBlockMap>();
		CHECK(bm->Init(0, 0, 1000, 1000, 1.0f));
		bm->AddRect(150, 0, 10, 1000);

		FieldZoneConfig cfg;
		cfg.zone.zoneId = 1; cfg.zone.tickIntervalMs = 10;
		cfg.minX = 0; cfg.minY = 0; cfg.maxX = 1000; cfg.maxY = 1000;
		cfg.cellSize = 50; cfg.viewCells = 1; cfg.minMoveIntervalMs = 0;
		cfg.blockMap = bm;
		BlockZone z(cfg);
		CHECK(AddCombat(z, 1, EntityType::Player, { 148, 100 }, 30.0f) != nullptr);
		CHECK(AddCombat(z, 2, EntityType::Monster, { 162, 100 }, 0.0f) != nullptr);		// 벽 건너편(사거리 안)
		CHECK(AddCombat(z, 3, EntityType::Monster, { 148, 115 }, 0.0f) != nullptr);		// 같은 쪽
		z.Start();

		AttackResult behindWall{}, sameSide{};
		Run(z, [&] { behindWall = z.RequestAttack(1, 2); });
		SleepMs(40);
		Run(z, [&] { sameSide = z.RequestAttack(1, 3); });
		CHECK(behindWall == AttackResult::NoLineOfSight);
		CHECK(sameSide == AttackResult::Ok);
		z.Stop();
	}

	static int Main()
	{
		TestMap();
		TestPath();
		TestZoneCollision();
		TestLineOfSight();
		std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
		return g_failed ? 1 : 0;
	}
}

int RunBlockMapTests()
{
	return Main();
}