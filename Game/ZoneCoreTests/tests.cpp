// ZoneCore 단위 테스트: CMoveOnlyFunction / CTimerHeap / CJob 호출 패턴 / CZone / CGridAOI / CFieldZone
#include "pch.h"
#include "FieldZone.h"
#include "test_common.h"
#include <functional>
#include <memory>
#include <string>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

// 한 실행 파일에 여러 테스트 파일을 묶으므로, 파일마다 헬퍼/타입 이름이 겹치지 않게 익명 네임스페이스로 감싼다.
namespace
{

	using namespace std::chrono_literals;


	// ───────────────────────── InlineFunction ─────────────────────────
	static int g_dtor = 0;
	struct Probe { int v; explicit Probe(int x) : v(x) {} Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; } ~Probe() { if( v >= 0 ) ++g_dtor; } };

	static void TestMoveOnlyFunction()
	{
		std::printf("[MoveOnlyFunction]\n");
		CMoveOnlyFunction<int(int), 48> f;
		CHECK(!f);

		int k = 5;
		f = [k](int x) { return x + k; };
		CHECK(f && f(3) == 8);

		// 이동 전용 캡처
		auto up = std::make_unique<int>(41);
		CMoveOnlyFunction<int(), 48> g([p = std::move(up)]() { return *p + 1; });
		CMoveOnlyFunction<int(), 48> h(std::move(g));
		CHECK(!g && h && h() == 42);

		// 큰 캡처는 힙으로 가되 동작/파괴가 정확해야 한다
		g_dtor = 0;
		{
			char big[200] = {};
			big[0] = 7;
			CMoveOnlyFunction<int(), 48> hf([big, pr = Probe(1)]() { return big[0] + pr.v; });
			CHECK(hf() == 8);
			CMoveOnlyFunction<int(), 48> moved(std::move(hf));
			CHECK(moved() == 8);
		}
		CHECK(g_dtor == 1);

		// 인라인 캡처 파괴 횟수
		g_dtor = 0;
		{
			CMoveOnlyFunction<void(), 48> a([pr = Probe(3)]() { (void)pr; });
			CMoveOnlyFunction<void(), 48> b(std::move(a));
			b = nullptr;
			CHECK(g_dtor == 1);
		}
		CHECK(g_dtor == 1);
		static_assert(sizeof(ZoneJob) <= 64, "ZoneJob size");
	}

	// ───────────────────────── TimerHeap ─────────────────────────
	static void TestTimerHeap()
	{
		std::printf("[TimerHeap]\n");
		CTimerHeap<CMoveOnlyFunction<void(), 48>> q;
		std::vector<int> order;

		q.Schedule(100, [&] { order.push_back(3); });
		q.Schedule(50, [&] { order.push_back(1); });
		q.Schedule(50, [&] { order.push_back(2); });          // 같은 시각 FIFO
		uint64 cancelMe = q.Schedule(60, [&] { order.push_back(99); });
		CHECK(q.Size() == 4);
		CHECK(q.Cancel(cancelMe));
		CHECK(!q.Cancel(cancelMe));
		CHECK(q.Size() == 3);

		CHECK(q.RunDue(49, [](auto& j) { j(); }) == 0);
		CHECK(q.RunDue(60, [](auto& j) { j(); }) == 2);
		CHECK((order == std::vector<int>{1, 2}));

		// 실행 중 재진입 Schedule/Cancel
		uint64 victim = q.Schedule(500, [&] { order.push_back(77); });
		q.Schedule(100, [&]
			{
				q.Cancel(victim);
				q.Schedule(100, [&] { order.push_back(4); });      // 이미 만료 시각 -> 같은 RunDue 안에서 실행됨
			});
		q.RunDue(100, [](auto& j) { j(); });
		CHECK((order == std::vector<int>{1, 2, 3, 4}));
		CHECK(q.Size() == 0);

		// 대량 취소 후 압축
		std::vector<uint64> ids;
		for( int i = 0; i < 1000; ++i )
			ids.push_back(q.Schedule(1000 + i, [&] { order.push_back(5); }));
		for( int i = 0; i < 990; ++i )
			q.Cancel(ids[i]);
		CHECK(q.Size() == 10);
		order.clear();
		CHECK(q.RunDue(5000, [](auto& j) { j(); }) == 10);
		CHECK(order.size() == 10);
	}

	// ───────────────────────── CJob 호출 패턴 (Job.h / JobQueue.inl 과 같은 형태) ─────────────────────────
	using CallbackType = CMoveOnlyFunction<void(), 48>;

	class CJobLike
	{
	public:
		CJobLike(CallbackType callback) : _callback(std::move(callback)) {}
		virtual ~CJobLike() = default;
		void Execute() { _callback(); }
		bool Inline() const { return _callback.IsStoredInline(); }
	private:
		CallbackType _callback;
	};

	struct JobOwner : std::enable_shared_from_this<JobOwner>
	{
		int sum = 0;
		std::string text;
		void AddInts(int a, int b) { sum += a + b; }
		void SetText(std::string s) { text = std::move(s); }
	};

	static void TestJobPattern()
	{
		std::printf("[CJob pattern]\n");
		auto owner = std::make_shared<JobOwner>();

		// DoAsync(람다)
		int hit = 0;
		CJobLike a(CallbackType([&hit] { ++hit; }));
		CHECK(a.Inline());
		a.Execute();
		CHECK(hit == 1);

		// DoAsync(멤버함수, 인자...) : JobQueue.inl 과 같은 std::bind + shared_ptr 소유자 캡처
		std::shared_ptr<JobOwner> ownerRef = owner;
		CJobLike b(CallbackType(std::bind(&JobOwner::AddInts, std::move(ownerRef), 3, 4)));
		b.Execute();
		CHECK(owner->sum == 7);
		std::printf("  bind(int,int) inline=%d\n", (int)b.Inline());

		// 문자열 인자: 크기에 따라 인라인이 아닐 수 있으나 동작은 같아야 한다
		std::shared_ptr<JobOwner> ownerRef2 = owner;
		CJobLike c(CallbackType(std::bind(&JobOwner::SetText, std::move(ownerRef2), std::string("hello world, a longer string than SSO"))));
		c.Execute();
		CHECK(owner->text == "hello world, a longer string than SSO");
		std::printf("  bind(string) inline=%d\n", (int)c.Inline());

		// 작업이 소유자 수명을 연장하는지(실행 전 외부 참조를 놓아도 안전한지)
		auto owner2 = std::make_shared<JobOwner>();
		std::weak_ptr<JobOwner> weak = owner2;
		{
			CJobLike d(CallbackType(std::bind(&JobOwner::AddInts, owner2, 1, 1)));
			owner2.reset();
			CHECK(!weak.expired());			// 작업이 소유자 수명을 붙들고 있다
			d.Execute();
		}
		CHECK(weak.expired());				// 작업이 파괴되면 소유자도 해제된다

		// std::function 에서의 변환(기존 코드가 std::function 을 넘기는 경우)도 컴파일/동작해야 한다
		std::function<void()> legacy = [&hit] { hit += 10; };
		CJobLike e{ CallbackType(legacy) };
		e.Execute();
		CHECK(hit == 11);
	}

	// ───────────────────────── CZone ─────────────────────────
	class CTestZone : public CZone
	{
	public:
		explicit CTestZone(const ZoneConfig& c) : CZone(c) {}
		~CTestZone() override { Stop(); }
		std::atomic<int> updates{ 0 };
		std::atomic<int> starts{ 0 };
		std::atomic<int> stops{ 0 };
		bool throwInUpdate = false;
		bool throwInStart = false;
	protected:
		void OnStart() override { ++starts; if( throwInStart ) throw std::runtime_error("start"); }
		void OnUpdate(uint32) override { ++updates; if( throwInUpdate && updates % 2 == 0 ) throw std::runtime_error("update"); }
		void OnStop() override { ++stops; }
	};

	static ZoneConfig MakeCfg(uint32 tick = 10)
	{
		ZoneConfig c;
		c.zoneId = 1;
		c.tickIntervalMs = tick;
		return c;
	}

	static void TestZoneBasics()
	{
		std::printf("[Zone basics]\n");
		CTestZone z(MakeCfg(10));
		CHECK(z.Start());
		CHECK(!z.Start());                       // 중복 시작 거부

		std::vector<int> order;
		for( int i = 0; i < 100; ++i )
			CHECK(z.Post([&order, i](CZone&) { order.push_back(i); }));
		SleepMs(200);
		z.Stop();
		CHECK(order.size() == 100);
		bool fifo = true;
		for( size_t i = 0; i < order.size(); ++i ) fifo &= (order[i] == static_cast<int>(i));
		CHECK(fifo);
		CHECK(z.starts == 1 && z.stops == 1);
		CHECK(z.GetStats().tickCount >= 5);
		CHECK(!z.Post([](CZone&) {}));           // 정지 후 거부
		CHECK(!z.Start());                       // 재시작 거부
		CHECK(z.GetStats().rejectedJobs >= 1);
	}

	static void TestZoneExceptions()
	{
		std::printf("[Zone exceptions]\n");
		CTestZone z(MakeCfg(5));
		z.throwInUpdate = true;
		z.throwInStart = true;
		CHECK(z.Start());
		std::atomic<int> ran{ 0 };
		z.Post([](CZone&) { throw std::runtime_error("job"); });
		z.Post([&](CZone&) { ++ran; });
		SleepMs(150);
		CHECK(ran == 1);                         // 앞 작업이 던져도 뒤 작업 실행
		CHECK(z.updates > 4);                    // OnUpdate/OnStart 예외에도 루프 지속
		CHECK(z.GetStats().failedJobs == 1);
		z.Stop();
		CHECK(z.stops == 1);
	}

	static void TestZoneBudget()
	{
		std::printf("[Zone budget]\n");
		ZoneConfig c = MakeCfg(10);
		c.maxJobsPerTick = 10;
		CTestZone z(c);
		std::vector<int> order;
		std::vector<uint64> tickAt;
		for( int i = 0; i < 100; ++i )
			z.Post([&, i](CZone& zone) { order.push_back(i); tickAt.push_back(zone.GetNowMs()); });
		// Start 전 Post는 거부되므로 먼저 시작
		CHECK(z.Start());
		for( int i = 100; i < 200; ++i )
			z.Post([&, i](CZone& zone) { order.push_back(i); tickAt.push_back(zone.GetNowMs()); });
		SleepMs(600);
		z.Stop();
		CHECK(order.size() == 100);              // Start 전 100개는 거부, 이후 100개 처리
		bool fifo = true;
		for( size_t i = 1; i < order.size(); ++i ) fifo &= (order[i] == order[i - 1] + 1);
		CHECK(fifo);
		std::map<uint64, int> perTick;
		for( uint64 t : tickAt ) ++perTick[t];
		int maxPerTick = 0;
		for( auto& kv : perTick ) maxPerTick = (std::max)(maxPerTick, kv.second);
		CHECK(maxPerTick <= 10);
		CHECK(perTick.size() >= 10);
	}

	// Stop과 경합하는 다수 프로듀서: true를 받은 작업은 반드시 실행되어야 한다
	static void TestZonePostStopRace()
	{
		std::printf("[Zone Post/Stop race]\n");
		for( int round = 0; round < 30; ++round )
		{
			ZoneConfig c = MakeCfg(1);
			c.maxInboxSize = 1u << 20;
			CTestZone z(c);
			z.Start();
			std::atomic<uint64> accepted{ 0 }, executed{ 0 };
			std::atomic<bool> go{ false };
			std::vector<std::thread> producers;
			for( int p = 0; p < 4; ++p )
				producers.emplace_back([&]
					{
						while( !go.load() ) {}
						for( int i = 0; i < 20000; ++i )
						{
							if( z.Post([&executed](CZone&) { executed.fetch_add(1); }) )
								accepted.fetch_add(1);
						}
					});
			go = true;
			SleepMs(round % 7);
			z.Stop();
			for( auto& t : producers ) t.join();
			if( accepted != executed )
			{
				std::printf("  round %d accepted=%llu executed=%llu\n", round, (unsigned long long)accepted.load(), (unsigned long long)executed.load());
				++g_failed;
			}
		}
	}

	//***************************************************************************
	// 종료 구간: Stop()이 IsRunning()을 내린 뒤에도 틱 스레드는 OnStop을 실행하므로, 그동안 외부 스레드가 타이머를 만지면 안 된다.
	// 여러 스레드가 동시에 Stop()해도 join은 한 번만 일어나야 한다.
	//***************************************************************************
	class CSlowStopZone : public CZone
	{
	public:
		explicit CSlowStopZone(const ZoneConfig& c) : CZone(c) {}
		~CSlowStopZone() override { Stop(); }
		std::atomic<bool> inStop{ false };
		std::atomic<bool> release{ false };
	protected:
		void OnStop() override { inStop = true; while( !release.load() ) SleepMs(1); }
	};

	static void TestZoneShutdownWindow()
	{
		std::printf("[Zone shutdown window / concurrent Stop]\n");
		CSlowStopZone z(MakeCfg(5));
		CHECK(z.Start());
		SleepMs(30);

		std::thread stopper([&] { z.Stop(); });
		while( !z.inStop.load() ) SleepMs(1);			// 틱 스레드가 OnStop 안에 있다. IsRunning()은 이미 false

		CHECK(!z.IsRunning());
		CHECK(z.ScheduleTimer(10, [](CZone&) {}) == 0);	// 존 스레드가 아직 살아 있으니 외부 스레드는 타이머를 만질 수 없다
		CHECK(!z.ScheduleAfter(10, [](CZone&) {}));		// Post 경로도 이미 닫혔다
		CHECK(!z.CancelTimer(1));

		std::thread s2([&] { z.Stop(); });				// 동시에 Stop()해도 같은 스레드를 두 번 join하지 않는다
		std::thread s3([&] { z.Stop(); });
		SleepMs(20);
		z.release = true;
		stopper.join(); s2.join(); s3.join();

		CHECK(z.ScheduleTimer(10, [](CZone&) {}) != 0);	// 완전히 끝난 뒤에는 다시 접근할 수 있다
	}

	static void TestZoneTimers()
	{
		std::printf("[Zone timers]\n");
		CTestZone z(MakeCfg(5));
		CHECK(z.ScheduleTimer(10, [](CZone&) {}) != 0);   // 시작 전에는 직접 등록 가능
		z.Start();
		CHECK(z.ScheduleTimer(10, [](CZone&) {}) == 0);   // 다른 스레드에서는 거부
		std::atomic<int> a{ 0 }, b{ 0 }, c{ 0 };
		CHECK(z.ScheduleAfter(30, [&](CZone&) { ++a; }));
		z.Post([&](CZone& zone)
			{
				uint64 id = zone.ScheduleTimer(40, [&](CZone&) { ++b; });
				CHECK(id != 0);
				CHECK(zone.CancelTimer(id));
				CHECK(!zone.CancelTimer(id));
				zone.ScheduleTimer(0, [&](CZone&) { ++c; });  // 0ms는 1ms로 보정, 같은 틱 재귀 실행 없음
			});
		SleepMs(200);
		CHECK(a == 1 && b == 0 && c == 1);
		z.Stop();

		uint64 w = 0;
		{
			CTestZone z2(MakeCfg(5));
			z2.Start();
			SleepMs(60);
			z2.Post([](CZone&) { SleepMs(30); });
			SleepMs(60);
			w = z2.ConsumeMaxTickUs();
			CHECK(w >= 25000);
			CHECK(z2.ConsumeMaxTickUs() < w);              // 윈도우 리셋
			CHECK(z2.GetStats().maxTickUs >= w);           // 누적 최대는 유지
			z2.Stop();
		}
	}

	// ───────────────────────── AOI / FieldZone ─────────────────────────
	class CTestEntity : public CZoneEntity
	{
	public:
		CTestEntity(uint64 id) : CZoneEntity(id, EntityType::Monster) {}
		std::set<uint64> seen;
		int enters = 0, leaves = 0;
	protected:
		void OnAoiEnter(CZoneEntity& o) override { ++enters; bool ins = seen.insert(o.GetId()).second; if( !ins ) { std::printf("  duplicate enter\n"); ++g_failed; } }
		void OnAoiLeave(CZoneEntity& o) override { ++leaves; if( seen.erase(o.GetId()) != 1 ) { std::printf("  leave without enter\n"); ++g_failed; } }
	};

	static FieldZoneConfig MakeField(float hyst, int view = 1)
	{
		FieldZoneConfig f;
		f.zone = MakeCfg(10);
		f.minX = 0; f.minY = 0; f.maxX = 1000; f.maxY = 1000;
		f.cellSize = 50;
		f.viewCells = view;
		f.aoiHysteresis = hyst;
		return f;
	}

	static void TestAoiRandom(float hyst, int view)
	{
		std::printf("[AOI random hyst=%.2f view=%d]\n", hyst, view);
		CFieldZone<> zone(MakeField(hyst, view));
		CHECK(zone.IsValid());
		std::mt19937 rng(1234);
		std::uniform_real_distribution<float> pos(0.0f, 400.0f);
		const int N = 150;
		std::vector<CTestEntity*> es;
		for( int i = 0; i < N; ++i )
		{
			auto e = std::make_unique<CTestEntity>(i + 1);
			e->SetSpawnPos({ pos(rng), pos(rng) });
			es.push_back(static_cast<CTestEntity*>(zone.AddEntity(std::move(e))));
			CHECK(es.back() != nullptr);
		}

		const float guaranteed = (static_cast<float>(view) - 2.0f * hyst) * 50.0f;
		auto verify = [&]
			{
				for( int i = 0; i < N; ++i )
				{
					// 대칭성
					for( uint64 id : es[i]->seen )
						if( es[id - 1]->seen.count(es[i]->GetId()) != 1 ) { std::printf("  asymmetric\n"); ++g_failed; return; }
					// ForEachInAoi 와 seen 일치
					std::set<uint64> viaIter;
					zone.ForEachInAoi(*es[i], [&](CZoneEntity& o) { viaIter.insert(o.GetId()); });
					if( viaIter != es[i]->seen ) { std::printf("  neighbor mismatch\n"); ++g_failed; return; }
					// 최소 보장 시야
					for( int j = 0; j < N; ++j )
					{
						if( i == j ) continue;
						if( DistanceSq(es[i]->GetPos(), es[j]->GetPos()) <= guaranteed * guaranteed && es[i]->seen.count(es[j]->GetId()) == 0 )
						{
							std::printf("  guaranteed pair not visible\n"); ++g_failed; return;
						}
					}
					// 너무 먼 엔티티는 보이면 안 됨: 셀 거리 상한 (view+1)*cell 이내
					for( uint64 id : es[i]->seen )
					{
						float d = std::sqrt(DistanceSq(es[i]->GetPos(), es[id - 1]->GetPos()));
						if( d > (view + 1) * 50.0f * 1.5f ) { std::printf("  too far visible %f\n", d); ++g_failed; return; }
					}
				}
			};
		verify();

		// 순간이동 + 작은 이동을 섞어 셀 변경을 많이 일으킨다
		std::uniform_int_distribution<int> pick(0, N - 1);
		std::uniform_real_distribution<float> smallStep(-7.0f, 7.0f);
		for( int step = 0; step < 4000; ++step )
		{
			CTestEntity* e = es[pick(rng)];
			Vec2 np;
			if( step % 17 == 0 ) np = { pos(rng), pos(rng) };
			else np = { (std::min)((std::max)(e->GetPos().x + smallStep(rng), 0.0f), 999.0f), (std::min)((std::max)(e->GetPos().y + smallStep(rng), 0.0f), 999.0f) };
			CHECK(zone.Teleport(e->GetId(), np));
			if( step % 500 == 0 ) verify();
		}
		verify();

		// ForEachInRadius vs 전수 비교
		for( int t = 0; t < 50; ++t )
		{
			Vec2 c{ pos(rng), pos(rng) };
			float r = 10.0f + (t % 9) * 11.0f;
			std::set<uint64> got, want;
			zone.ForEachInRadius(c, r, [&](CZoneEntity& o) { got.insert(o.GetId()); });
			for( auto* e : es ) if( DistanceSq(e->GetPos(), c) <= r * r ) want.insert(e->GetId());
			CHECK(got == want);
		}
		// 비정상 입력은 아무것도 순회하지 않고 크래시도 없어야 한다
		int cnt = 0;
		float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
		zone.ForEachInRadius({ 10, 10 }, nan, [&](CZoneEntity&) { ++cnt; });
		zone.ForEachInRadius({ nan, 10 }, 50, [&](CZoneEntity&) { ++cnt; });
		zone.ForEachInRadius({ 10, 10 }, -5, [&](CZoneEntity&) { ++cnt; });
		zone.ForEachInRadius({ inf, inf }, 50, [&](CZoneEntity&) { ++cnt; });
		CHECK(cnt == 0);
		zone.ForEachInRadius({ 500, 500 }, inf, [&](CZoneEntity&) { ++cnt; });   // 무한 반경: 전부 (클램프)
		CHECK(cnt == N);

		// 전원 제거 시 이탈 통지로 seen 이 모두 비어야 한다
		for( int i = 0; i < N; ++i ) CHECK(zone.RemoveEntity(es[i]->GetId()));
		CHECK(zone.GetEntityCount() == 0);
	}

	static void TestAoiHysteresis()
	{
		std::printf("[AOI hysteresis]\n");
		CFieldZone<> zone(MakeField(0.1f));
		auto a = std::make_unique<CTestEntity>(1); a->SetSpawnPos({ 49.0f, 25.0f });
		auto b = std::make_unique<CTestEntity>(2); b->SetSpawnPos({ 400.0f, 400.0f });
		auto* pa = static_cast<CTestEntity*>(zone.AddEntity(std::move(a)));
		auto* pb = static_cast<CTestEntity*>(zone.AddEntity(std::move(b)));
		// pb는 멀리 있다. pa가 x=50 경계를 왕복해도(마진 5 이내) 셀이 바뀌지 않으므로 통지가 없다.
		for( int i = 0; i < 100; ++i )
			zone.Teleport(1, { (i % 2) ? 49.0f : 52.0f, 25.0f });
		CHECK(pa->enters == 0 && pa->leaves == 0 && pb->enters == 0);

		// 이제 pb를 pa 옆 셀(경계 근처)로 두고 왕복 -> 플래핑 없음
		zone.Teleport(2, { 110.0f, 25.0f });   // 셀 2, pa(셀 0/1)와 거리 2셀 이상 -> 안 보임
		int before = pa->enters + pa->leaves;
		for( int i = 0; i < 100; ++i )
			zone.Teleport(1, { (i % 2) ? 99.0f : 102.0f, 25.0f });
		int flaps = pa->enters + pa->leaves - before;
		std::printf("  flap events: %d\n", flaps);
		CHECK(flaps <= 2);

		// 마진을 확실히 넘으면 셀이 바뀐다
		zone.Teleport(1, { 140.0f, 25.0f });
		CHECK(pa->seen.count(2) == 1);
	}

	class CMoveZone : public CFieldZone<>
	{
	public:
		explicit CMoveZone(const FieldZoneConfig& c) : CFieldZone<>(c) {}
		~CMoveZone() override { Stop(); }
		std::atomic<int> started{ 0 }, stopped{ 0 }, rejected{ 0 }, removed{ 0 };
		std::atomic<bool> lastArrived{ false }, removedStillRegistered{ false };
	protected:
		void OnEntityRemoved(CZoneEntity& e) override { ++removed; removedStillRegistered = (FindEntity(e.GetId()) == &e); }
		void OnEntityMoveStarted(CZoneEntity&) override { ++started; }
		void OnEntityMoveStopped(CZoneEntity&, bool arrived) override { ++stopped; lastArrived = arrived; }
		void OnMoveRejected(CZoneEntity&, MoveResult) override { ++rejected; }
	};

	static void TestMovementAndThrottle()
	{
		std::printf("[FieldZone movement/throttle]\n");
		FieldZoneConfig cfg = MakeField(0.1f);
		cfg.zone.tickIntervalMs = 20;
		cfg.minMoveIntervalMs = 40;
		CMoveZone zone(cfg);
		auto e = std::make_unique<CTestEntity>(7);
		e->SetSpawnPos({ 100, 100 });
		e->SetSpeed(100.0f);
		CZoneEntity* ent = zone.AddEntity(std::move(e));
		zone.Start();

		std::atomic<int> r1{ -1 }, r2{ -1 }, r3{ -1 }, r4{ -1 };
		zone.Post([&](CZone& z)
			{
				auto& fz = static_cast<CFieldZone<>&>(z);
				r1 = (int)fz.RequestMove(7, { 100, 100 }, { 150, 100 });
				r2 = (int)fz.RequestMove(7, { 100, 100 }, { 160, 100 });   // 같은 틱 -> Throttled
			});
		SleepMs(60);   // 몇 틱 경과
		zone.Post([&](CZone& z)
			{
				auto& fz = static_cast<CFieldZone<>&>(z);
				// 같은 목적지 재요청: 간격은 충족, 서버 위치는 이동 중이라 허용 오차(1+100*0.3=31) 이내, 알림 증가 없음
				r3 = (int)fz.RequestMove(7, fz.FindEntity(7)->GetPos(), { 150, 100 });
				r4 = (int)fz.RequestMove(7, { 900, 900 }, { 150, 100 });   // 간격 부족으로 Throttled가 먼저
			});
		SleepMs(900);
		zone.Stop();
		CHECK(r1 == (int)MoveResult::Ok);
		CHECK(r2 == (int)MoveResult::Throttled);
		CHECK(r3 == (int)MoveResult::Ok);
		CHECK(r4 == (int)MoveResult::Throttled);
		CHECK(zone.started == 1);                 // 중복 브로드캐스트 없음
		CHECK(zone.stopped == 1 && zone.lastArrived);
		CHECK(std::fabs(ent->GetPos().x - 150.0f) < 1e-3f && std::fabs(ent->GetPos().y - 100.0f) < 1e-3f);
		CHECK(zone.rejected == 0);                // Throttled는 보정 통지 없음
	}

	//***************************************************************************
	// 속도 방어: SetSpeed 보정, 이동 도중 속도가 0 이하가 되면 그 자리에 멈춘다
	//***************************************************************************
	static void TestSpeedGuard()
	{
		std::printf("[FieldZone speed guard]\n");
		FieldZoneConfig cfg = MakeField(0.1f);
		cfg.zone.tickIntervalMs = 20;
		cfg.minMoveIntervalMs = 0;
		CMoveZone zone(cfg);
		auto e = std::make_unique<CTestEntity>(7);
		e->SetSpawnPos({ 100, 100 });
		e->SetSpeed(50.0f);
		CZoneEntity* ent = zone.AddEntity(std::move(e));

		ent->SetSpeed(std::numeric_limits<float>::quiet_NaN());
		CHECK(ent->GetSpeed() == 0.0f);
		ent->SetSpeed(-3.0f);
		CHECK(ent->GetSpeed() == 0.0f);
		ent->SetSpeed(std::numeric_limits<float>::infinity());
		CHECK(ent->GetSpeed() == 0.0f);
		CHECK(zone.RequestMove(7, { 100, 100 }, { 200, 100 }) == MoveResult::Immobile);
		CHECK(!zone.MoveEntity(*ent, { 200, 100 }));
		ent->SetSpeed(50.0f);

		zone.Start();
		zone.Post([&](CZone& z) { static_cast<CFieldZone<>&>(z).MoveEntity(*ent, { 400, 100 }); });
		SleepMs(80);

		std::atomic<int> movingBefore{ -1 }, movingAfter{ -1 };
		std::atomic<float> x0{ 0.0f }, x1{ 0.0f }, x2{ 0.0f };
		zone.Post([&](CZone&) { movingBefore = ent->IsMoving() ? 1 : 0; x0 = ent->GetPos().x; ent->SetSpeed(0.0f); });	// 속박
		SleepMs(80);
		zone.Post([&](CZone&) { movingAfter = ent->IsMoving() ? 1 : 0; x1 = ent->GetPos().x; });
		SleepMs(80);
		zone.Post([&](CZone&) { x2 = ent->GetPos().x; });
		SleepMs(40);

		std::atomic<int> again{ -1 };
		zone.Post([&](CZone& z) { ent->SetSpeed(50.0f); again = static_cast<CFieldZone<>&>(z).MoveEntity(*ent, { 400, 100 }) ? 1 : 0; });
		SleepMs(60);
		zone.Stop();

		CHECK(movingBefore == 1 && x0 > 100.0f);
		CHECK(movingAfter == 0);						// 속도가 0이 된 틱에 멈춘다 (목록에 영원히 남지 않는다)
		CHECK(zone.stopped == 1 && !zone.lastArrived);
		CHECK(x2 == x1 && x1 < 400.0f);					// 멈춘 자리에 그대로 있다
		CHECK(again == 1 && zone.started == 2);			// 속도가 돌아오면 다시 이동할 수 있다
	}

	//***************************************************************************
	// 제거 훅: 엔티티가 테이블과 AOI에서 빠지기 직전에 한 번 호출된다
	//***************************************************************************
	static void TestRemoveHook()
	{
		std::printf("[FieldZone remove hook]\n");
		FieldZoneConfig cfg = MakeField(0.1f);
		cfg.zone.tickIntervalMs = 20;
		CMoveZone zone(cfg);
		for( uint64 id = 7; id <= 8; ++id )
		{
			auto e = std::make_unique<CTestEntity>(id);
			e->SetSpawnPos({ 100.0f + (float)id, 100 });
			e->SetSpeed(50.0f);
			zone.AddEntity(std::move(e));
		}
		zone.Start();

		std::atomic<int> r1{ -1 }, r2{ -1 }, r3{ -1 };
		zone.Post([&](CZone& z)
			{
				auto& fz = static_cast<CFieldZone<>&>(z);
				fz.MoveEntity(*fz.FindEntity(7), { 300, 100 });			// 이동 중인 엔티티도 문제없이 빠진다
				r1 = fz.RemoveEntity(7) ? 1 : 0;
				r2 = fz.RemoveEntity(7) ? 1 : 0;							// 이미 없음 -> 훅 호출 없음
				r3 = (fz.FindEntity(7) == nullptr && fz.FindEntity(8) != nullptr) ? 1 : 0;
			});
		SleepMs(60);
		zone.Stop();

		CHECK(r1 == 1 && r2 == 0 && r3 == 1);
		CHECK(zone.removed == 1);
		CHECK(zone.removedStillRegistered);							// 훅 시점에는 아직 테이블에 있다
	}

	//***************************************************************************
	// 타입 안전 CFieldZone<TEntity>: 훅/조회/순회가 캐스트 없이 TEntity&를 받는지 확인
	//***************************************************************************
	class CNpcEntity : public CZoneEntity
	{
	public:
		CNpcEntity(uint64 id, int hp) : CZoneEntity(id, EntityType::Npc), hp(hp) {}
		int hp;
	};

	class CNpcZone : public CFieldZone<CNpcEntity>
	{
	public:
		explicit CNpcZone(const FieldZoneConfig& c) : CFieldZone<CNpcEntity>(c) {}
		int started = 0, stopped = 0, rejected = 0, hpSum = 0;

	protected:
		void OnEntityMoveStarted(CNpcEntity& e) override { ++started; hpSum += e.hp; }
		void OnEntityMoveStopped(CNpcEntity& e, bool) override { ++stopped; hpSum += e.hp; }
		void OnMoveRejected(CNpcEntity&, MoveResult) override { ++rejected; }
	};

	static void TestTypedFieldZone()
	{
		std::printf("[CFieldZone<TEntity>]\n");
		CNpcZone zone(MakeField(0.1f));
		CHECK(zone.IsValid());

		auto a = std::make_unique<CNpcEntity>(1, 10);
		auto b = std::make_unique<CNpcEntity>(2, 20);
		a->SetSpawnPos({ 100, 100 }); a->SetSpeed(100.0f);
		b->SetSpawnPos({ 120, 100 });

		CNpcEntity* pa = zone.AddEntity(std::move(a));		// 캐스트 없이 CNpcEntity*
		CNpcEntity* pb = zone.AddEntity(std::move(b));
		CHECK(pa != nullptr && pb != nullptr);
		CHECK(zone.FindEntity(2) == pb && zone.FindEntity(2)->hp == 20);
		CHECK(zone.FindEntity(99) == nullptr);

		int hpSeen = 0;
		zone.ForEachInAoi(*pa, [&](CNpcEntity& o) { hpSeen += o.hp; });
		CHECK(hpSeen == 20);
		hpSeen = 0;
		zone.ForEachInRadius({ 110, 100 }, 30.0f, [&](CNpcEntity& o) { hpSeen += o.hp; });
		CHECK(hpSeen == 30);

		CHECK(zone.RequestMove(*pa, pa->GetPos(), { 200, 100 }) == MoveResult::Ok);
		CHECK(zone.started == 1 && zone.hpSum == 10);
		CHECK(zone.RequestMove(*pb, pb->GetPos(), { 130, 100 }) == MoveResult::Immobile && zone.rejected == 1);
		CHECK(zone.RemoveEntity(1) && zone.GetEntityCount() == 1);
	}

	int Main()
	{
		TestMoveOnlyFunction();
		TestTimerHeap();
		TestJobPattern();
		TestZoneBasics();
		TestZoneExceptions();
		TestZoneBudget();
		TestZoneTimers();
		TestZonePostStopRace();
		TestZoneShutdownWindow();
		TestTypedFieldZone();
		TestAoiRandom(0.0f, 1);
		TestAoiRandom(0.1f, 1);
		TestAoiRandom(0.2f, 2);
		TestAoiHysteresis();
		TestMovementAndThrottle();
		TestSpeedGuard();
		TestRemoveHook();
		std::printf(g_failed ? "\nFAILED: %d\n" : "\nALL PASSED\n", g_failed);
		return g_failed ? 1 : 0;
	}
}

int RunZoneTests()
{
	return Main();
}