
//***************************************************************************
// ChatStressScheduler.cpp : implementation of the CChatStressScheduler class.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressScheduler.h"

//***************************************************************************
// @brief 스케줄러 스레드를 시작한다.
// @param threadCount 스레드 수(1 미만이면 1로 보정)
//***************************************************************************
void CChatStressScheduler::Start(int threadCount)
{
	threadCount = (std::max)(1, threadCount);
	for( int i = 0; i < threadCount; ++i )
		_threads.emplace_back([this, i, threadCount]() { ThreadMain(i, threadCount); });
}

//***************************************************************************
// @brief g_stop을 세워 스케줄러 스레드를 종료시키고 모두 합류(join)한다.
//***************************************************************************
void CChatStressScheduler::Stop()
{
	g_stop = true;
	for( auto& t : _threads )
		if( t.joinable() )
			t.join();
	_threads.clear();
}

//***************************************************************************
// @brief 새 세션을 스케줄 대상으로 등록한다. 세션 팩토리에서 호출한다.
// @param session 등록할 세션
//***************************************************************************
void CChatStressScheduler::Register(const std::shared_ptr<CChatStressSession>& session)
{
	std::lock_guard<std::mutex> guard(_mutex);
	_sessions.push_back(session);
}

//***************************************************************************
// @brief 스케줄러 스레드 본체. 자기 몫의 세션에 주기적으로 Tick()을 호출한다.
// @param tid 이 스레드의 번호(0부터)
// @param threadCount 전체 스케줄러 스레드 수
//***************************************************************************
void CChatStressScheduler::ThreadMain(int tid, int threadCount)
{
	std::vector<std::shared_ptr<CChatStressSession>> mine;
	int64_t nextRefresh = 0;

	while( !g_stop )
	{
		const int64_t now = ptime::MonotonicNowMicros();

		// 100ms마다 레지스트리에서 내 몫을 다시 뽑는다(락은 이때만 잡음).
		// 닫힌 세션은 0번 스레드가 레지스트리에서 제거해 메모리를 회수한다.
		if( now >= nextRefresh )
		{
			nextRefresh = now + 100 * 1000;
			mine.clear();

			std::lock_guard<std::mutex> guard(_mutex);
			if( tid == 0 )
			{
				_sessions.erase(std::remove_if(_sessions.begin(), _sessions.end(),
					[](const std::shared_ptr<CChatStressSession>& s) { return s->IsClosed(); }), _sessions.end());
			}
			for( const auto& s : _sessions )
				if( s->GetIndex() % threadCount == tid )
					mine.push_back(s);
		}

		for( const auto& s : mine )
			s->Tick(now);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}