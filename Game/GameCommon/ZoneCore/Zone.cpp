//***************************************************************************
// Zone.cpp : implementation of the CZone class.
//
//***************************************************************************

#include "pch.h"
#include "Zone.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace
{
	//***************************************************************************
	// @class CTimerResolutionScope
	// @brief 생존 기간 동안 Windows 시스템 타이머 해상도를 올리는 RAII 객체. (프로세스 전역, 참조 카운트)
	//        플랫폼 종속 코드를 이 클래스 하나에 가둬, 나머지 존 코드는 플랫폼과 무관하게 유지한다.
	//        Windows가 아니거나 ms가 0이면 아무 일도 하지 않는다.
	//***************************************************************************
	class CTimerResolutionScope
	{
	public:
		explicit CTimerResolutionScope(uint32 ms) noexcept
#ifdef _WIN32
			: _ms(ms)
		{
			if( _ms != 0 )
				timeBeginPeriod(_ms);
		}
		~CTimerResolutionScope()
		{
			if( _ms != 0 )
				timeEndPeriod(_ms);
		}
#else
		{
			(void)ms;
		}
#endif

		CTimerResolutionScope(const CTimerResolutionScope&) = delete;
		CTimerResolutionScope& operator=(const CTimerResolutionScope&) = delete;

#ifdef _WIN32
	private:
		uint32 _ms;		// 요청한 해상도(ms), 0이면 미적용
#endif
	};

	//***************************************************************************
	// @brief 단일 쓰기 스레드가 갱신하는 통계 카운터를 증가시킨다. (lock 접두사가 붙는 fetch_add를 피한다.)
	//***************************************************************************
	inline void BumpStat(std::atomic<uint64>& counter, uint64 delta = 1)
	{
		counter.store(counter.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
	}

	//***************************************************************************
	// @brief 반복되는 오류 로그가 폭주하지 않도록, 발생 횟수가 2의 거듭제곱일 때만 true를 반환한다.
	//***************************************************************************
	inline bool ShouldLog(uint64 occurrence)
	{
		return (occurrence & (occurrence - 1)) == 0;
	}
}

//***************************************************************************
// @brief 존을 생성합니다. 스레드는 Start() 호출 시 시작됩니다.
// @param config 존 설정
//***************************************************************************
CZone::CZone(const ZoneConfig& config)
	: _config(config)
	, _threadId(std::thread::id())
	, _started(false)
	, _running(false)
	, _accepting(false)
	, _drainPos(0)
	, _pendingJobs(0)
	, _activePosters(0)
	, _rejectedJobs(0)
	, _nowMs(0)
	, _tickCount(0)
	, _skippedTicks(0)
	, _overrunTicks(0)
	, _processedJobs(0)
	, _failedJobs(0)
	, _lastTickUs(0)
	, _maxTickUs(0)
	, _windowMaxTickUs(0)
{
	if( _config.tickIntervalMs == 0 )
		_config.tickIntervalMs = 1;
	if( _config.maxCatchUpTicks == 0 )
		_config.maxCatchUpTicks = 1;

	_drainBuf.reserve(1024);
}

//***************************************************************************
// @brief 소멸자. 동작 중이면 정지하고 스레드를 합류시킵니다.
//***************************************************************************
CZone::~CZone()
{
	Stop();
}

//***************************************************************************
// @brief 틱 스레드를 시작합니다. 존당 한 번만 호출할 수 있습니다.
// @return 시작했으면 true, 이미 시작했거나(정지 후 재시작 포함) 스레드 생성에 실패하면 false
//***************************************************************************
bool CZone::Start()
{
	// 정지된 존은 입력 큐가 영구히 닫혀 있으므로 재시작을 허용하지 않는다.
	if( _started.exchange(true) )
		return false;

	_running.store(true, std::memory_order_release);
	_accepting.store(true, std::memory_order_seq_cst);

	try
	{
		_thread = std::thread(&CZone::ThreadMain, this);
	}
	catch( ... )
	{
		_accepting.store(false, std::memory_order_seq_cst);
		_running.store(false, std::memory_order_release);
		return false;
	}

	return true;
}

//***************************************************************************
// @brief 틱 스레드를 정지합니다.
//        새 Post는 즉시 거부되며, 이미 큐에 들어간 작업은 종료 직전에 마저 처리합니다.
//        존 스레드 내부에서 호출하면 정지 요청만 하고 join은 외부(또는 소멸자)에서 수행됩니다.
//***************************************************************************
void CZone::Stop()
{
	_accepting.store(false, std::memory_order_seq_cst);

	{
		std::lock_guard<std::mutex> lock(_sleepMutex);
		_running.store(false, std::memory_order_release);
	}
	_sleepCv.notify_all();

	if( IsZoneThread() )
		return;

	if( _thread.joinable() )
		_thread.join();
}

//***************************************************************************
// @brief 외부 스레드(IOCP 워커 등)에서 존으로 작업을 넘깁니다. 스레드 안전.
//        작업은 다음 틱 시작 시 존 스레드에서 FIFO 순서로 실행됩니다.
// @details true를 반환한 작업은 Stop() 이후에도 반드시 실행된다(유실 없음).
//          Post 진입을 _activePosters로 등록한 뒤 수락 여부를 검사하고, 존 스레드는 종료 시 등록된 Post가
//          모두 끝나기를 기다린 뒤에 큐를 닫는다. 따라서 "수락 검사 통과 -> 큐 닫힘 -> Push 폐기" 경합이 없다.
// @param job 존 스레드에서 실행할 작업
// @return 적재 성공 시 true, 존이 정지 상태이거나 큐가 가득 찼으면 false
//***************************************************************************
bool CZone::Post(ZoneJob job)
{
	if( !job )
		return false;

	_activePosters.fetch_add(1, std::memory_order_seq_cst);

	bool posted = false;

	if( _accepting.load(std::memory_order_seq_cst) )
	{
		// CDoubleBufferQueue는 크기 상한이 없으므로 대기 작업 수를 별도 카운터로 제한한다.
		if( _pendingJobs.fetch_add(1, std::memory_order_relaxed) >= _config.maxInboxSize )
		{
			_pendingJobs.fetch_sub(1, std::memory_order_relaxed);
		}
		else
		{
			_inbox.Push(std::move(job));
			posted = true;
		}
	}

	_activePosters.fetch_sub(1, std::memory_order_seq_cst);

	if( !posted )
		_rejectedJobs.fetch_add(1, std::memory_order_relaxed);

	return posted;
}

//***************************************************************************
// @brief 타이머를 직접 만질 수 있는 상태인지 확인합니다.
//        존 스레드이거나, 아직 시작 전/정지 후라서 다른 스레드가 접근하지 않는 상태여야 합니다.
// @return 접근 가능하면 true
//***************************************************************************
bool CZone::CanTouchTimers() const
{
	return IsZoneThread() || !IsRunning();
}

//***************************************************************************
// @brief 지정한 논리 시간(ms) 뒤에 작업을 실행하도록 예약하고 취소용 ID를 돌려줍니다.
//        존 스레드(또는 시작 전)에서만 호출할 수 있습니다. 다른 스레드는 ScheduleAfter()를 사용하세요.
//        같은 틱에 재귀적으로 실행되지 않도록 최소 지연은 1ms로 보정합니다.
// @param delayMs 지연 시간(ms)
// @param job     실행할 작업
// @return 타이머 ID (1 이상), 호출 스레드가 부적절하거나 작업이 비어 있으면 0
//***************************************************************************
uint64 CZone::ScheduleTimer(uint32 delayMs, ZoneJob job)
{
	if( !job || !CanTouchTimers() )
		return 0;

	if( delayMs == 0 )
		delayMs = 1;

	return _timers.Schedule(GetNowMs() + delayMs, std::move(job));
}

//***************************************************************************
// @brief 예약한 타이머를 취소합니다. 존 스레드(또는 시작 전)에서만 호출할 수 있습니다.
// @param timerId ScheduleTimer()가 반환한 ID
// @return 아직 실행되지 않은 타이머를 취소했으면 true
//***************************************************************************
bool CZone::CancelTimer(uint64 timerId)
{
	if( timerId == 0 || !CanTouchTimers() )
		return false;

	return _timers.Cancel(timerId);
}

//***************************************************************************
// @brief 어느 스레드에서든 지연 작업을 예약합니다. (취소 ID가 필요 없을 때)
//        존 스레드가 아니면 Post()를 거쳐 존 스레드가 등록하며, 이 경우 지연은 등록 시점부터 계산됩니다.
// @param delayMs 지연 시간(ms)
// @param job     실행할 작업
// @return 예약(또는 예약 요청 적재)에 성공하면 true, 존이 정지/포화 상태이면 false
//***************************************************************************
bool CZone::ScheduleAfter(uint32 delayMs, ZoneJob job)
{
	if( !job )
		return false;

	if( CanTouchTimers() )
		return ScheduleTimer(delayMs, std::move(job)) != 0;

	return Post([delayMs, job = std::move(job)](CZone& zone) mutable
		{
			zone.ScheduleTimer(delayMs, std::move(job));
		});
}

//***************************************************************************
// @brief 현재 스레드가 이 존의 틱 스레드인지 확인합니다.
// @return 존 스레드이면 true
//***************************************************************************
bool CZone::IsZoneThread() const
{
	return _threadId.load(std::memory_order_acquire) == std::this_thread::get_id();
}

//***************************************************************************
// @brief 통계 스냅샷을 반환합니다.
// @return 현재 통계값
//***************************************************************************
ZoneStats CZone::GetStats() const
{
	ZoneStats s;
	s.tickCount = _tickCount.load(std::memory_order_relaxed);
	s.skippedTicks = _skippedTicks.load(std::memory_order_relaxed);
	s.overrunTicks = _overrunTicks.load(std::memory_order_relaxed);
	s.processedJobs = _processedJobs.load(std::memory_order_relaxed);
	s.failedJobs = _failedJobs.load(std::memory_order_relaxed);
	s.rejectedJobs = _rejectedJobs.load(std::memory_order_relaxed);
	s.lastTickUs = _lastTickUs.load(std::memory_order_relaxed);
	s.maxTickUs = _maxTickUs.load(std::memory_order_relaxed);
	return s;
}

//***************************************************************************
// @brief 직전 호출 이후의 최대 틱 처리 시간을 반환하고 0으로 리셋합니다.
//        하트비트처럼 주기적으로 구간 최대값을 보고하는 용도입니다. (시작 후 누적 최대는 GetStats().maxTickUs)
// @return 구간 내 최대 틱 처리 시간(us)
//***************************************************************************
uint64 CZone::ConsumeMaxTickUs()
{
	return _windowMaxTickUs.exchange(0, std::memory_order_relaxed);
}

//***************************************************************************
// @brief 존 스레드 시작 직후 한 번 호출됩니다. 스폰 포인트/몬스터 초기화 등에 사용합니다.
//***************************************************************************
void CZone::OnStart()
{
}

//***************************************************************************
// @brief 매 틱마다 호출되는 시뮬레이션 확장 지점.
//        이후 단계(몬스터 AI, 투사체, 전투 판정, AOI 갱신, 패킷 플러시)를 여기서 호출합니다.
// @param dtMs 고정 틱 간격(ms)
//***************************************************************************
void CZone::OnUpdate(uint32 dtMs)
{
	(void)dtMs;
}

//***************************************************************************
// @brief 존 스레드 종료 직전에 한 번 호출됩니다. 잔여 작업 처리 이후 실행됩니다.
//***************************************************************************
void CZone::OnStop()
{
}

//***************************************************************************
// @brief 사용자 코드(가상 함수 확장 지점)를 예외로부터 격리해 실행합니다.
//        예외가 틱 스레드 밖으로 나가면 프로세스가 종료되므로 반드시 여기서 흡수한다.
// @param phase 로그에 남길 단계 이름
// @param fn    실행할 함수
//***************************************************************************
template <typename F>
void CZone::Guarded(const TCHAR* phase, F&& fn)
{
	try
	{
		fn();
	}
	catch( const std::exception& e )
	{
		LOG_ERROR(_T("[CZone] exception in %s. zoneId=%u, what=%hs"), phase, _config.zoneId, e.what());
	}
	catch( ... )
	{
		LOG_ERROR(_T("[CZone] unknown exception in %s. zoneId=%u"), phase, _config.zoneId);
	}
}

//***************************************************************************
// @brief 존 스레드 메인 루프. 고정 타임스텝 + 누적 보정 방식.
//        한 번에 처리하는 따라잡기 틱을 maxCatchUpTicks로 제한하고,
//        그래도 밀려 있으면 남은 틱을 건너뛰어 지연이 누적되지 않게 합니다.
//***************************************************************************
void CZone::ThreadMain()
{
	using Clock = std::chrono::steady_clock;

	_threadId.store(std::this_thread::get_id(), std::memory_order_release);

	// 대기 해상도가 기본(약 15.6ms)이면 50ms 틱에 큰 지터가 생기므로 설정값만큼 올린다. 스레드 종료 시 자동 복원된다.
	CTimerResolutionScope timerResolution(_config.timerResolutionMs);

	Guarded(_T("OnStart"), [this]() { OnStart(); });

	const auto step = std::chrono::milliseconds(_config.tickIntervalMs);
	auto nextTick = Clock::now() + step;

	while( _running.load(std::memory_order_acquire) )
	{
		{
			std::unique_lock<std::mutex> lock(_sleepMutex);
			_sleepCv.wait_until(lock, nextTick, [this]
				{
					return !_running.load(std::memory_order_acquire);
				});
		}

		if( !_running.load(std::memory_order_acquire) )
			break;

		uint32 ran = 0;
		while( Clock::now() >= nextTick && ran < _config.maxCatchUpTicks )
		{
			RunTick();
			nextTick += step;
			++ran;
		}

		const auto now = Clock::now();
		if( now >= nextTick )
		{
			// 따라잡기 한도를 넘겨 여전히 밀려 있음: 남은 틱을 건너뛴다.
			const auto behind = std::chrono::duration_cast<std::chrono::milliseconds>(now - nextTick);
			const uint64 skipped = static_cast<uint64>(behind.count() / _config.tickIntervalMs) + 1;
			BumpStat(_skippedTicks, skipped);
			nextTick += step * skipped;
		}
	}

	// 종료 절차: (1) 이미 수락 검사를 통과한 Post가 Push를 마치기를 기다린다 -> (2) 큐를 닫는다 -> (3) 남은 작업을 처리한다.
	// Stop()이 _accepting을 먼저 내렸으므로 (1) 이후에는 새 Post가 큐에 도달하지 못한다.
	while( _activePosters.load(std::memory_order_seq_cst) != 0 )
		std::this_thread::yield();

	_inbox.Stop();

	// 예산으로 끊긴 잔여분을 먼저 비우고, 그 다음 큐에 남은 작업을 비운다.
	DrainInbox(0);
	DrainInbox(0);

	Guarded(_T("OnStop"), [this]() { OnStop(); });

	_timers.Clear();

	_threadId.store(std::thread::id(), std::memory_order_release);
}

//***************************************************************************
// @brief 한 틱을 실행합니다. 논리 시간을 먼저 전진시킨 뒤
//        입력 처리 -> 타이머 -> 시뮬레이션 순으로 진행합니다.
//***************************************************************************
void CZone::RunTick()
{
	using Clock = std::chrono::steady_clock;
	const auto begin = Clock::now();

	const uint64 nowMs = _nowMs.load(std::memory_order_relaxed) + _config.tickIntervalMs;
	_nowMs.store(nowMs, std::memory_order_relaxed);

	DrainInbox(_config.maxJobsPerTick);

	_timers.RunDue(nowMs, [this](ZoneJob& job) { RunJob(job); });

	Guarded(_T("OnUpdate"), [this]() { OnUpdate(_config.tickIntervalMs); });

	BumpStat(_tickCount);

	const uint64 elapsedUs = static_cast<uint64>(
		std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());

	_lastTickUs.store(elapsedUs, std::memory_order_relaxed);

	if( elapsedUs > _maxTickUs.load(std::memory_order_relaxed) )
		_maxTickUs.store(elapsedUs, std::memory_order_relaxed);

	// 구간 최대값은 다른 스레드가 exchange(0)로 리셋하므로 CAS로 갱신한다.
	uint64 prevMax = _windowMaxTickUs.load(std::memory_order_relaxed);
	while( elapsedUs > prevMax &&
		!_windowMaxTickUs.compare_exchange_weak(prevMax, elapsedUs, std::memory_order_relaxed) )
	{
	}

	if( elapsedUs > static_cast<uint64>(_config.tickIntervalMs) * 1000 )
		BumpStat(_overrunTicks);
}

//***************************************************************************
// @brief 입력 큐의 활성 버퍼를 스왑해 통째로 가져오고, 틱 스레드 전용 버퍼에서 FIFO로 실행합니다.
//        작업 실행 중 Post되는 새 작업은 다음 틱에 처리됩니다. 한 틱에 스왑은 최대 한 번입니다.
//        예산(budget)을 넘은 작업은 버퍼에 남겨 두었다가 다음 틱에 이어서 실행하며(순서 유지),
//        남은 작업이 있는 동안에는 새로 스왑하지 않습니다.
//        SwapInto()는 단일 Consumer 전용이므로 존 스레드에서만 호출해야 합니다.
// @param budget 이번 호출에서 실행할 최대 작업 수 (0 = 무제한)
//***************************************************************************
void CZone::DrainInbox(uint32 budget)
{
	if( _drainPos >= _drainBuf.size() )
	{
		_drainBuf.clear();
		_drainPos = 0;
		_inbox.SwapInto(_drainBuf);
	}

	uint32 ran = 0;

	while( _drainPos < _drainBuf.size() && (budget == 0 || ran < budget) )
	{
		ZoneJob& job = _drainBuf[_drainPos++];

		_pendingJobs.fetch_sub(1, std::memory_order_relaxed);
		RunJob(job);
		job.Reset();		// 캡처한 자원을 즉시 해제 (버퍼 clear까지 붙들고 있지 않는다)
		++ran;
	}
}

//***************************************************************************
// @brief 작업 하나를 실행합니다. 예외가 발생해도 존 루프가 죽지 않도록 격리합니다.
// @param job 실행할 작업
//***************************************************************************
void CZone::RunJob(ZoneJob& job)
{
	try
	{
		job(*this);
		BumpStat(_processedJobs);
	}
	catch( const std::exception& e )
	{
		BumpStat(_failedJobs);
		const uint64 failed = _failedJobs.load(std::memory_order_relaxed);
		if( ShouldLog(failed) )
			LOG_ERROR(_T("[CZone] job threw. zoneId=%u, failedJobs=%llu, what=%hs"), _config.zoneId, failed, e.what());
	}
	catch( ... )
	{
		BumpStat(_failedJobs);
		const uint64 failed = _failedJobs.load(std::memory_order_relaxed);
		if( ShouldLog(failed) )
			LOG_ERROR(_T("[CZone] job threw unknown exception. zoneId=%u, failedJobs=%llu"), _config.zoneId, failed);
	}
}
