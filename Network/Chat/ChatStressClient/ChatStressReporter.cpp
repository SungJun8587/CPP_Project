
//***************************************************************************
// ChatStressReporter.cpp : implementation of the console report functions.
//
//***************************************************************************

#include "pch.h"
#include "ChatStressReporter.h"

using ull = unsigned long long;

//***************************************************************************
// @brief 1초마다 현재 상태를 한 줄로 출력한다.
// @details 호출 간 증감(초당 송수신량 등)을 계산하기 위해 이전 호출 시점의 값을
//          내부 static 변수에 보관한다.
// @param el 테스트 시작 후 경과 시간(초)
// @param dt 직전 호출 이후 경과 시간(초)
//***************************************************************************
void ReportSecond(double el, double dt)
{
    static uint64_t pTx = 0, pRx = 0, pChat = 0, pRxB = 0;
    uint64_t tx = S.txPkts, rx = S.rxPkts, ch = S.chatSent, rxb = S.rxBytes;
    std::printf("[%5.0fs] conn=%d login=%d dead=%d | tx %6.0f/s rx %7.0f/s (%.2f MB/s) chat %5.0f/s | rtt p50 %.1f p99 %.1f ms | disc=%llu err=%llu\n",
        el, S.connected.load(), S.loggedIn.load(), S.dead.load(), (tx - pTx) / dt, (rx - pRx) / dt, (rxb - pRxB) / dt / 1048576.0, (ch - pChat) / dt,
        S.chatRtt.Pct(0.5), S.chatRtt.Pct(0.99), (ull)S.disconnects, (ull)S.protoErrors);
    std::fflush(stdout);
    pTx = tx; pRx = rx; pChat = ch; pRxB = rxb;
}

//***************************************************************************
// @brief 히스토그램 1개의 통계(건수/평균/백분위/최대)를 한 줄로 출력한다.
// @param name 항목 이름
// @param h 출력할 히스토그램
//***************************************************************************
static void Row(const char* name, const Hist& h)
{
    std::printf("  %-14s n=%-9llu avg=%8.2f  p50=%8.2f  p90=%8.2f  p99=%8.2f  p99.9=%8.2f  max=%8.2f (ms)\n",
        name, (ull)h.cnt.load(), h.Avg(), h.Pct(0.50), h.Pct(0.90), h.Pct(0.99), h.Pct(0.999), h.Max());
}

//***************************************************************************
// @brief 테스트 종료 시 전체 결과 요약을 출력한다.
// @param el 테스트 전체 경과 시간(초)
//***************************************************************************
void PrintSummary(double el)
{
    std::printf("\n================ 결과 요약 (%.1fs) ================\n", el);
    std::printf("접속   : 시도 성공 %llu / 실패 %llu, 로그인 성공 %llu / 실패 %llu, 끊김 %llu, 프로토콜 오류 %llu, 송신 드롭 %llu\n",
        (ull)S.connectOk, (ull)S.connectFail, (ull)S.loginOk, (ull)S.loginFail, (ull)S.disconnects, (ull)S.protoErrors, (ull)S.sendDropped);
    std::printf("송신   : %llu pkts (%.2f MB, %.0f pkt/s)\n", (ull)S.txPkts, S.txBytes / 1048576.0, S.txPkts / el);
    std::printf("수신   : %llu pkts (%.2f MB, %.0f pkt/s)\n", (ull)S.rxPkts, S.rxBytes / 1048576.0, S.rxPkts / el);
    std::printf("채팅   : 보냄 %llu / 수신(전체) %llu / 내 메시지 되돌아옴 %llu\n", (ull)S.chatSent, (ull)S.chatRecv, (ull)S.chatOwnRecv);
    std::printf("방목록 : 요청 %llu / 완료 %llu    멤버목록: 요청 %llu / 완료 %llu    방입장: 성공 %llu / 실패 %llu\n",
        (ull)S.listSent, (ull)S.listDone, (ull)S.memberSent, (ull)S.memberDone, (ull)S.enterOk, (ull)S.enterFail);
    Row("connect", S.connectLat); Row("login", S.loginLat); Row("chat RTT", S.chatRtt);
    Row("ListRooms", S.listLat); Row("MemberList", S.memberLat);
    if( S.chatSent > 0 && S.chatOwnRecv == 0 )
        std::printf("  ※ 내 채팅이 한 번도 되돌아오지 않았음: 서버가 발신자에게 브로드캐스트하지 않거나 채팅이 막힌 상태일 수 있음(RTT 측정 불가).\n");
    std::printf("=====================================================\n");
}