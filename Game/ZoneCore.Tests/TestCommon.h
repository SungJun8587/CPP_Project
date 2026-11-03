// 테스트 공용 헬퍼: 실패 카운터, CHECK 매크로, 대기 함수
#pragma once

#include <chrono>
#include <cstdio>
#include <thread>

inline int g_failed = 0;

#define CHECK(cond) do { if( !(cond) ) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while(0)

inline void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
