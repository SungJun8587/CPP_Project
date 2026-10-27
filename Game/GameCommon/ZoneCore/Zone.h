//***************************************************************************
// Zone.h : interface for the CZone class.
//
//***************************************************************************

#ifndef ZONE_H
#define ZONE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <BaseRedefineDataType.h>
#include <Containers/Queue/DoubleBufferQueue.h>
#include <Memory/Containers.h>

#include <Job/MoveOnlyFunction.h>
#include <Job/TimerHeap.h>

class CZone;

//***************************************************************************
// @brief 존 스레드에서 실행되는 작업 단위. IOCP 워커가 패킷을 파싱한 뒤 Post()로 넘긴다.
// @details 이동 전용 + 작은 캡처 인라인 저장(48바이트)이라 패킷 한 건 규모의 작업은 힙 할당 없이 큐를 통과한다.
//***************************************************************************
using ZoneJob = CMoveOnlyFunction<void(CZone&), 48>;

//***************************************************************************
// @brief 존 생성 설정값.
//
// - tickIntervalMs  : 고정 틱 간격(ms). 필드 전투는 50~100ms 권장
// - maxCatchUpTicks : 한 번의 깨어남에서 밀린 틱을 연속 처리할 최대 횟수.
//                     초과분은 건너뛰어(skipped) 죽음의 나선(spiral of death) 방지
// - maxInboxSize    : 입력 큐 상한. 초과 시 Post()가 false를 반환(백프레셔)
// - maxJobsPerTick  : 틱 하나에서 실행할 입력 작업 상한. 0이면 무제한.
//                     상한을 넘은 작업은 다음 틱에 이어서 처리된다(순서 유지).
// - timerResolutionMs: Windows에서 존 스레드가 살아 있는 동안 시스템 타이머 해상도를 이 값(ms)으로 올린다.
//                     기본 해상도(약 15.6ms)에서는 50ms 틱에 큰 지터가 생기기 때문이다. 프로세스 전역 설정이므로
//                     호스트 애플리케이션이 이미 설정했다면 0으로 두어 건드리지 않게 한다. Windows 외 플랫폼에서는 무시된다.
//***************************************************************************
struct ZoneConfig
{
	uint32 zoneId = 0;				// 존 식별자
	uint32 tickIntervalMs = 50;		// 고정 틱 간격(ms)
	uint32 maxCatchUpTicks = 5;		// 연속 따라잡기 틱 상한
	uint32 maxInboxSize = 65536;	// 입력 큐 최대 적재 수
	uint32 maxJobsPerTick = 0;		// 틱당 입력 작업 처리 상한 (0 = 무제한)
	uint32 timerResolutionMs = 1;	// Windows 타이머 해상도(ms), 0이면 변경하지 않음
};

//***************************************************************************
// @brief 존 틱 루프 통계 스냅샷. 모니터링/부하 테스트용.
//***************************************************************************
struct ZoneStats
{
	uint64 tickCount = 0;		// 실행된 총 틱 수
	uint64 skippedTicks = 0;	// 따라잡기를 포기하고 건너뛴 틱 수
	uint64 overrunTicks = 0;	// 처리 시간이 틱 간격을 초과한 틱 수
	uint64 processedJobs = 0;	// 처리된 입력 작업 수
	uint64 failedJobs = 0;		// 예외로 중단된 작업 수
	uint64 rejectedJobs = 0;	// 큐 포화/정지로 거부된 Post 수
	uint64 lastTickUs = 0;		// 마지막 틱 처리 시간(us)
	uint64 maxTickUs = 0;		// 시작 후 최대 틱 처리 시간(us)
};

//***************************************************************************
// @class CZone
// @brief 필드(맵/인스턴스) 하나를 담당하는 단일 스레드 시뮬레이션 단위.
// @details
// 존 내부 상태(엔티티, 몬스터, 버프 등)는 오직 존 스레드에서만 접근하므로 별도의 락이 필요 없다.
// 외부 스레드(IOCP 워커 등)는 Post()로 작업을 넣고, 존 스레드가 매 틱 시작 시 입력 큐를 비우며 처리한다.
//
// 틱 순서: DrainInbox -> 타이머 -> OnUpdate(파생 클래스 확장 지점)
// 시간은 고정 타임스텝(논리 시간)으로 진행하여 결정적(deterministic)이다.
// 밀려서 건너뛴 틱은 논리 시간에도 반영되지 않는다(과부하 시 시뮬레이션이 느려질 뿐 점프하지 않는다).
//
// 생명주기: Start()는 한 번만 가능하다(정지 후 재시작 불가 — 입력 큐가 영구 정지되므로).
// 사용자 코드(OnStart/OnUpdate/OnStop/작업/타이머)에서 던진 예외는 격리되어 로그만 남기고 존은 계속 동작한다.
//***************************************************************************
class CZone
{
public:
	explicit CZone(const ZoneConfig& config);
	virtual ~CZone();

	CZone(const CZone&) = delete;
	CZone& operator=(const CZone&) = delete;

	bool Start();
	void Stop();

	bool Post(ZoneJob job);

	bool ScheduleAfter(uint32 delayMs, ZoneJob job);
	uint64 ScheduleTimer(uint32 delayMs, ZoneJob job);
	bool CancelTimer(uint64 timerId);

	bool IsZoneThread() const;
	ZoneStats GetStats() const;
	uint64 ConsumeMaxTickUs();

	//***************************************************************************
	// @brief 틱 스레드가 동작 중인지 반환합니다.
	// @return 동작 중이면 true
	//***************************************************************************
	bool IsRunning() const { return _running.load(std::memory_order_acquire); }

	//***************************************************************************
	// @brief 존 식별자를 반환합니다.
	// @return 존 ID
	//***************************************************************************
	uint32 GetZoneId() const { return _config.zoneId; }

	//***************************************************************************
	// @brief 고정 틱 간격을 반환합니다.
	// @return 틱 간격(ms)
	//***************************************************************************
	uint32 GetTickIntervalMs() const { return _config.tickIntervalMs; }

	//***************************************************************************
	// @brief 존 논리 시간을 반환합니다. 틱마다 틱 간격만큼 증가합니다.
	// @return 존 시작 후 경과한 논리 시간(ms)
	//***************************************************************************
	uint64 GetNowMs() const { return _nowMs.load(std::memory_order_relaxed); }

protected:
	virtual void OnStart();
	virtual void OnUpdate(uint32 dtMs);
	virtual void OnStop();

private:
	template <typename F>
	void Guarded(const TCHAR* phase, F&& fn);

	void ThreadMain();
	void RunTick();
	void DrainInbox(uint32 budget);
	void RunJob(ZoneJob& job);
	bool CanTouchTimers() const;

	ZoneConfig _config;							// 생성 설정

	std::thread _thread;						// 존 틱 스레드
	std::atomic<std::thread::id> _threadId;		// 존 스레드 ID (IsZoneThread 판별용)
	std::atomic<bool> _started;					// Start() 호출 여부 (재시작 차단)
	std::atomic<bool> _running;					// 틱 루프 동작 여부
	std::atomic<bool> _accepting;				// Post 수락 여부

	std::mutex _sleepMutex;						// 틱 대기용 뮤텍스
	std::condition_variable _sleepCv;			// Stop 시 대기 중인 스레드를 깨우기 위한 CV

	CDoubleBufferQueue<ZoneJob> _inbox;			// 입력 큐 (MPSC: IOCP 워커 다수 -> 존 스레드 하나)
	CVector<ZoneJob> _drainBuf;					// 존 스레드 전용 스왑 버퍼 (할당 재사용)
	size_t _drainPos;							// _drainBuf에서 다음에 실행할 위치 (틱당 예산으로 끊긴 경우 이어서 처리)

	CTimerHeap<ZoneJob> _timers;				// 지연 작업 (존 스레드 전용)

	// 여러 스레드가 갱신하는 카운터는 서로, 그리고 다른 멤버와 캐시 라인을 공유하지 않도록 분리한다.
	alignas(64) std::atomic<uint32> _pendingJobs;	// 아직 실행되지 않은 입력 작업 수 (maxInboxSize 백프레셔용)
	alignas(64) std::atomic<uint32> _activePosters;	// Post() 실행 중인 호출 수 (종료 시 큐를 닫기 전 대기용)
	alignas(64) std::atomic<uint64> _rejectedJobs;	// 통계: 거부 작업 (Post 호출 스레드들이 갱신)

	alignas(64) std::atomic<uint64> _nowMs;			// 존 논리 시간(ms)

	// 아래 통계는 존 스레드만 갱신하고(원자 RMW 불필요) 다른 스레드는 읽기만 한다.
	std::atomic<uint64> _tickCount;				// 통계: 총 틱
	std::atomic<uint64> _skippedTicks;			// 통계: 건너뛴 틱
	std::atomic<uint64> _overrunTicks;			// 통계: 초과 틱
	std::atomic<uint64> _processedJobs;			// 통계: 처리 작업
	std::atomic<uint64> _failedJobs;			// 통계: 실패 작업
	std::atomic<uint64> _lastTickUs;			// 통계: 마지막 틱 시간
	std::atomic<uint64> _maxTickUs;				// 통계: 시작 후 최대 틱 시간
	std::atomic<uint64> _windowMaxTickUs;		// 통계: ConsumeMaxTickUs() 이후의 최대 틱 시간 (존 스레드가 갱신, 소비자가 리셋)
};

#endif // ndef ZONE_H
