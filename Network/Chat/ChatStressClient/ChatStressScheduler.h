
//***************************************************************************
// ChatStressScheduler.h : interface for the CChatStressScheduler class.
//
//***************************************************************************

#ifndef UC_CHATSTRESSSCHEDULER_H
#define UC_CHATSTRESSSCHEDULER_H

#include <Util/DateTimeUtil.h>
#include "ChatStressSession.h"

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

//***************************************************************************
// @brief 세션 레지스트리 + 주기 송신 스케줄러
// @details IOCP 워커 스레드는 "수신 완료 처리"만 하므로, 클라이언트가 먼저 보내는
//          주기 요청(채팅 등)은 별도 스케줄러 스레드가 구동한다. 스레드 i는
//          (세션 번호 % 스레드 수 == i)인 세션만 담당해 락 경합 없이 Tick()한다.
//***************************************************************************
class CChatStressScheduler
{
public:
	//***************************************************************************
	// @brief 소멸 시 스케줄러 스레드를 정지하고 합류(join)한다.
	//***************************************************************************
	~CChatStressScheduler() { Stop(); }

	void	Start(int threadCount);
	void	Stop();

	void	Register(const std::shared_ptr<CChatStressSession>& session);

private:
	void	ThreadMain(int tid, int threadCount);

private:
	std::mutex											_mutex;		// _sessions 보호
	std::vector<std::shared_ptr<CChatStressSession>>	_sessions;	// 스케줄 대상 세션 레지스트리
	std::vector<std::thread>							_threads;	// 스케줄러 스레드
};

#endif // ndef UC_CHATSTRESSSCHEDULER_H