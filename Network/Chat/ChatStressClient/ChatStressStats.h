
//***************************************************************************
// ChatStressStats.h : interface for the Hist and Stats structs.
//
//***************************************************************************

#ifndef UC_CHATSTRESSSTATS_H
#define UC_CHATSTRESSSTATS_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

//***************************************************************************
// @brief 지연 시간 히스토그램
// @details 0.1ms 해상도, 최대 10s(초과는 마지막 버킷). 락 없이 여러 스레드에서
//          Add()를 호출할 수 있다. 입력은 마이크로초, 출력(Pct/Avg/Max)은 밀리초.
//***************************************************************************
struct Hist
{
    static constexpr int N = 100001;        // 버킷 수(버킷 1개 = 0.1ms)
    std::vector<std::atomic<uint32_t>> b;   // 버킷별 건수
    std::atomic<uint64_t> cnt{ 0 };         // 전체 건수
    std::atomic<uint64_t> sumUs{ 0 };       // 전체 합계(마이크로초)
    std::atomic<int64_t> maxUs{ 0 };        // 최대값(마이크로초)

    //***************************************************************************
    // @brief 모든 버킷을 0으로 초기화한다.
    //***************************************************************************
    Hist() : b(N) { for( auto& x : b ) x.store(0); }

    //***************************************************************************
    // @brief 지연 시간 1건을 기록한다.
    // @param us 지연 시간(마이크로초). 음수는 0으로 보정한다.
    //***************************************************************************
    void Add(int64_t us)
    {
        if( us < 0 ) us = 0;
        size_t i = (size_t)std::min<int64_t>(us / 100, N - 1);
        b[i].fetch_add(1, std::memory_order_relaxed);
        cnt.fetch_add(1, std::memory_order_relaxed);
        sumUs.fetch_add((uint64_t)us, std::memory_order_relaxed);
        int64_t m = maxUs.load(std::memory_order_relaxed);
        while( us > m && !maxUs.compare_exchange_weak(m, us) ) {}
    }

    //***************************************************************************
    // @brief 백분위수 값을 계산한다.
    // @param p 백분위(0.0~1.0, 예: 0.99=p99)
    // @return 해당 백분위의 지연 시간(밀리초). 기록이 없으면 0
    //***************************************************************************
    double Pct(double p) const
    {
        uint64_t total = cnt.load(); if( !total ) return 0;
        uint64_t target = (uint64_t)(total * p); if( target >= total ) target = total - 1;
        uint64_t acc = 0;
        for( int i = 0; i < N; ++i ) { acc += b[i].load(); if( acc > target ) return i * 0.1; }
        return (N - 1) * 0.1;
    }

    //***************************************************************************
    // @brief 평균 지연 시간을 계산한다.
    // @return 평균 지연 시간(밀리초). 기록이 없으면 0
    //***************************************************************************
    double Avg() const { uint64_t c = cnt.load(); return c ? (sumUs.load() / (double)c) / 1000.0 : 0; }

    //***************************************************************************
    // @brief 최대 지연 시간을 반환한다.
    // @return 최대 지연 시간(밀리초)
    //***************************************************************************
    double Max() const { return maxUs.load() / 1000.0; }
};

//***************************************************************************
// @brief 스트레스 테스트 전체 통계 카운터
// @details 모든 값은 atomic이라 IOCP 워커 스레드, 스케줄러 스레드, 리포터 스레드가
//          락 없이 동시에 갱신/조회할 수 있다. 전역 인스턴스 S 하나만 사용한다.
//***************************************************************************
struct Stats
{
    std::atomic<int> connecting{ 0 };           // 연결 시도 중인 세션 수
    std::atomic<int> connected{ 0 };            // TCP 연결이 완료된 세션 수
    std::atomic<int> loggedIn{ 0 };             // 로그인까지 끝난 세션 수
    std::atomic<int> dead{ 0 };                 // 연결 실패 또는 끊겨서 닫힌 세션 수

    std::atomic<uint64_t> connectOk{ 0 };       // 연결 성공 횟수
    std::atomic<uint64_t> connectFail{ 0 };     // 연결 실패 횟수
    std::atomic<uint64_t> loginOk{ 0 };         // 로그인 성공 횟수
    std::atomic<uint64_t> loginFail{ 0 };       // 로그인 실패 횟수

    std::atomic<uint64_t> disconnects{ 0 };     // 테스트 도중 서버/네트워크로 끊긴 횟수
    std::atomic<uint64_t> protoErrors{ 0 };     // 프로토콜 오류(잘못된 크기/상태) 횟수
    std::atomic<uint64_t> sendDropped{ 0 };     // Send() 실패로 버려진 송신 횟수

    std::atomic<uint64_t> txPkts{ 0 };          // 송신 패킷 수
    std::atomic<uint64_t> txBytes{ 0 };         // 송신 바이트 수
    std::atomic<uint64_t> rxPkts{ 0 };          // 수신 패킷 수
    std::atomic<uint64_t> rxBytes{ 0 };         // 수신 바이트 수

    std::atomic<uint64_t> chatSent{ 0 };        // 보낸 채팅 수
    std::atomic<uint64_t> chatRecv{ 0 };        // 받은 채팅 수(전체)
    std::atomic<uint64_t> chatOwnRecv{ 0 };     // 내가 보낸 채팅이 되돌아온 수(RTT 측정 대상)

    std::atomic<uint64_t> listSent{ 0 };        // 방 목록 요청 수
    std::atomic<uint64_t> listDone{ 0 };        // 방 목록 응답 완료 수
    std::atomic<uint64_t> memberSent{ 0 };      // 방 멤버 목록 요청 수
    std::atomic<uint64_t> memberDone{ 0 };      // 방 멤버 목록 응답 완료 수
    std::atomic<uint64_t> enterOk{ 0 };         // 방 입장 성공 수
    std::atomic<uint64_t> enterFail{ 0 };       // 방 입장 실패 수

    Hist connectLat;                            // 연결 지연
    Hist loginLat;                              // 로그인 응답 지연
    Hist chatRtt;                               // 채팅 왕복 시간(RTT)
    Hist listLat;                               // 방 목록 응답 지연
    Hist memberLat;                             // 방 멤버 목록 응답 지연
};

extern Stats S;     // 프로세스 전역 통계

#endif // ndef UC_CHATSTRESSSTATS_H