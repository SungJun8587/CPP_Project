
//***************************************************************************
// ChatStressMain.cpp : implementation of the ChatStressClient entry point.
//
//***************************************************************************

#include "pch.h"

namespace
{
	//***************************************************************************
	// @brief Ctrl+C(SIGINT) 핸들러. g_stop을 세워 모든 루프가 종료 절차로 들어가게 한다.
	// @param signum 수신한 시그널 번호(사용하지 않음)
	//***************************************************************************
	void OnCtrlC(int) { g_stop = true; }
}

//***************************************************************************
// @brief 클라이언트 종료 시 등록된 클라이언트 설정, 글로벌 리소스를 해제합니다.
//***************************************************************************
void MainClose()
{
	// 1. BaseGlobal 전역 프레임워크 리소스 해제
	BaseGlobal::Destroy();

	// 2. Winsock 라이브러리 정리
	CSocketUtils::Clear();
}

//***************************************************************************
// @brief IOCP 스트레스 테스트 클라이언트 진입점.
// @details CChatClientMain::Connect()와 같은 절차(CIocpCore -> CNetworkFactory::
//          CreateClientService -> CIocpClientService)로 세션을 만들되, 세션 하나가
//          아니라 N개를 한 서비스/한 IOCP 위에서 돌린다.
//            - ramp 모드(기본): 서비스를 세션 0개로 Start()해 워커만 띄우고,
//              ConnectOneMoreSession()을 초당 rampPerSec 비율로 호출해 점진 접속.
//            - burst 모드(--ramp 0): 세션 수를 N으로 주고 Start() 한 번에 전부 접속.
//          --accounts 파일을 지정하면 실행 중 5초마다(변경이 있을 때) 계정을 저장하고
//          종료 시 한 번 더 저장한다.
// @param argc 명령행 인자 개수
// @param argv 명령행 인자(와이드 문자열)
// @return 프로세스 종료 코드(0=정상)
//***************************************************************************
int wmain(int argc, wchar_t** argv)
{
	// 1. 프로세스 초기화
	// 1-1. 콘솔 UTF-8 출력 설정
	InitUtf8Console();

	// 1-2. BaseGlobal 전역 프레임워크 초기화
	BaseGlobal::Init();

	// 1-3. Winsock 초기화
	CSocketUtils::Init();

	// 2. 실행 옵션 준비
	// 2-1. 명령행 인자를 g_cfg에 채운다(--help 또는 잘못된 옵션이면 여기서 종료)
	int exitCode = 0;
	if( !ParseArgs(argc, argv, exitCode) )
		return exitCode;

	// 2-2. Ctrl+C로 종료할 수 있게 핸들러 등록
	std::signal(SIGINT, OnCtrlC);

	// 3. 계정 파일과 실행 태그 준비
	// 3-1. 계정 파일이 지정돼 있으면 읽어 태그와 계정을 복원한다
	const bool useAccounts = !g_cfg.accountsFile.empty();
	if( useAccounts )
		CChatStressAccounts::Instance().Load(g_cfg.accountsFile);

	// 3-2. 태그가 비었으면(복원된 태그가 없으면) 새로 만든다
	GenerateRunTag();

	// 4. 스케줄러와 세션 팩토리 준비
	// 4-1. 주기 송신 스케줄러, 클라이언트 번호 발급기, 접속 모드(burst/ramp)
	CChatStressScheduler scheduler;
	std::atomic<int> nextIndex{ 0 };
	const bool burst = g_cfg.rampPerSec <= 0;

	// 4-2. 세션 팩토리: CChatClientMain::Connect()의 factory와 같은 역할.
	// 세션마다 고유 번호를 부여하고, 저장된 계정이 있으면 재접속(hasToken=1)으로 만든다.
	SessionFactory factory = [&]() -> CSessionRef
		{
			const int idx = nextIndex++;
			ChatStressAccount acc;
			const bool hasToken = useAccounts && CChatStressAccounts::Instance().Find(idx, acc);
			auto session = std::make_shared<CChatStressSession>(idx, hasToken, acc.publicId, acc.token);
			scheduler.Register(session);
			return session;
		};

	// 5. 클라이언트 서비스 생성
	// 5-1. IOCP 코어 생성
	CIocpCoreRef iocpCore = MakeShared<CIocpCore>();
	EngineCoreRef engineCore = iocpCore;

	// 5-2. 서비스 생성(ramp 모드는 세션 0개로 만들고 접속은 6-4에서 점진 게시)
	CNetServiceRef service = CNetworkFactory::CreateClientService(
		engineCore, CNetAddress(g_cfg.host, g_cfg.port), factory,
		burst ? g_cfg.clients : 0,	// ramp 모드: Start()는 워커만 구동, 접속은 아래에서 점진 게시
		static_cast<uint32>(g_cfg.workers));

	// 5-3. IOCP 클라이언트 서비스로 변환하고 생성 결과를 확인한다
	CIocpClientServiceRef clientService = std::static_pointer_cast<CIocpClientService>(service);
	if( clientService == nullptr )
	{
		std::printf("failed to create client service\n");
		return 1;
	}

	// 6. 시작
	// 6-1. 대상 서버와 실행 옵션 출력
	std::printf("target %ls:%d  clients %d  workers %d  sched %d  ramp %.0f/s  chat %dms  list %dms  room %d  tag %s\n",
		g_cfg.host.c_str(), g_cfg.port, g_cfg.clients, g_cfg.workers, g_cfg.schedThreads, g_cfg.rampPerSec,
		g_cfg.chatIntervalMs, g_cfg.listIntervalMs, g_cfg.roomId, g_runTag.c_str());
	std::printf("(stop: duration elapsed or Ctrl+C)\n\n");

	// 6-2. 스케줄러 스레드 시작
	const int64_t startUs = ptime::MonotonicNowMicros();
	scheduler.Start(g_cfg.schedThreads);

	// 6-3. 서비스 시작(IOCP 워커 구동, burst 모드는 여기서 전부 접속)
	if( !clientService->Start() )
	{
		std::printf("service start failed\n");
		scheduler.Stop();
		return 1;
	}

	// 6-4. ramp 모드: 경과 시간 x 비율만큼 누적해서 게시 (10ms 단위로 부족분을 채움)
	std::thread rampThread;
	if( !burst )
	{
		rampThread = std::thread([&]()
			{
				int posted = 0;
				while( !g_stop && posted < g_cfg.clients )
				{
					const double elapsed = (ptime::MonotonicNowMicros() - startUs) / 1e6;
					const int target = (std::min)(g_cfg.clients, static_cast<int>(elapsed * g_cfg.rampPerSec) + 1);
					while( posted < target && !g_stop )
					{
						// 반환값 nullptr = 게시 자체 실패(세션 생성/IOCP 등록/ConnectEx 게시 실패)
						if( clientService->ConnectOneMoreSession() == nullptr )
							S.connectFail++;
						++posted;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
			});
	}

	// 7. 모니터링 루프(Ctrl+C 또는 duration 경과까지)
	int64_t last = ptime::MonotonicNowMicros();
	int64_t lastAccountsSave = last;
	while( !g_stop )
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		const int64_t now = ptime::MonotonicNowMicros();
		const double el = (now - startUs) / 1e6, dt = (now - last) / 1e6;
		last = now;
		// 7-1. 1초마다 상태 한 줄 출력
		ReportSecond(el, dt);

		// 7-2. 비정상 종료돼도 로그인에 성공한 계정의 토큰이 남도록 주기적으로 저장한다(변경이 있을 때만)
		if( useAccounts && now - lastAccountsSave >= 5 * 1000 * 1000 )
		{
			lastAccountsSave = now;
			if( !CChatStressAccounts::Instance().SaveIfDirty(g_cfg.accountsFile) )
				std::printf("accounts save FAILED: %ls\n", g_cfg.accountsFile.c_str());
		}

		// 7-3. 지정한 테스트 시간이 지나면 루프 종료
		if( g_cfg.durationSec > 0 && el >= g_cfg.durationSec )
			break;
	}

	// 8. 종료 절차
	// 8-1. 새 접속 중단(ramp 스레드 종료 대기)
	g_stop = true;
	if( rampThread.joinable() )
		rampThread.join();

	// 8-2. 스케줄러 정지
	scheduler.Stop();

	// 8-3. 서비스 Close(세션 정리 + 워커 join)
	clientService->Close();

	// 8-4. 결과 요약 출력
	PrintSummary((ptime::MonotonicNowMicros() - startUs) / 1e6);

	// 8-5. 계정 파일 최종 저장
	if( useAccounts )
	{
		if( CChatStressAccounts::Instance().Save(g_cfg.accountsFile) )
			std::printf("accounts saved: %ls\n", g_cfg.accountsFile.c_str());
		else
			std::printf("accounts save FAILED: %ls\n", g_cfg.accountsFile.c_str());
	}

	// 8-6. 전역 리소스 해제
	MainClose();

	return 0;
}