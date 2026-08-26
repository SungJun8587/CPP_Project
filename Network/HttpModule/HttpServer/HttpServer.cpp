// HttpServer.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "HttpServerSession.h"

constexpr ENetworkEngineType kTestEngineType = ENetworkEngineType::IOCP;

int main()
{
#ifdef _MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	//_CrtSetBreakAlloc(676);

	// 1. 콘솔 및 시스템 환경 초기화
	// 1-1. UTF-8 콘솔 출력 입출력 인코딩 설정
	InitUtf8Console();

	// 1-2. BaseGlobal 시스템 전역 모듈 초기화
	BaseGlobal::Init();
	std::cout << "[System] BaseGlobal::Init()...\n";

	// 1-3. Winsock 소켓 라이브러리 유틸리티 초기화
	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";

	// 2. 네트워크 엔드포인트 및 네트워크 코어 객체 준비
	// 2-1. 서버 바인딩 IP 주소 및 포트 번호 구성 (127.0.0.1:7777)
	CNetAddress serverAddress(L"127.0.0.1", 7777);
	CNetServiceRef serverService = nullptr;
	CIocpCoreRef iocpCore = nullptr;

	std::cout << "========================================\n";
	std::cout << " Network Engine: " << (kTestEngineType == ENetworkEngineType::RIO ? "RIO (Registered I/O)" : "IOCP") << "\n";
	std::cout << "========================================\n";

	// 3. 네트워크 엔진 및 서버 서비스 생성
	// 3-1. 워커 스레드 개수 결정 (0 입력 시 하드웨어 코어 수 기반 자동 산정)
	uint32_t serverThreadWorkerCount = 2;

	// 3-2. IOCP 핵심 처리 객체 생성
	iocpCore = std::make_shared<CIocpCore>();

	// 3-3. IOCP 서버 서비스 객체 생성 (인자: 엔진타입, 주소, 세션 팩토리, 최대 세션 수, 워커 스레드 수, 코어 참조)
	//      CHttpServerSession 생성자에 정적 파일 서빙 루트 디렉토리("Statics") 명시 주입
	serverService = CNetworkFactory::CreateServerService(
		ENetworkEngineType::IOCP, serverAddress,
		[]() -> std::shared_ptr<CSession> {
			auto session = std::make_shared<CHttpServerSession>("Statics");
			// 필요 시 추가 MIME 타입 등록 예시
			// session->SetMimeType(".webp", "image/webp");
			return session; // CSession 기반 shared_ptr로 안전하게 다형성 반환
		},
		10, serverThreadWorkerCount, &iocpCore
	);

	// 4. 서버 서비스 구동 시작
	// 4-1. 소켓 바인딩/리스닝 및 워커 스레드 루프 시작 검증
	if( serverService && serverService->Start() )
	{
		std::cout << "[Server] HTTP Server started successfully on port 7777 (CThreadManager active).\n";
		std::cout << "[Server] Serving static files from directory: './Statics/'\n";
	}
	else
	{
		std::cout << "[Error] HTTP Server Start failed!\n";
		BaseGlobal::Destroy();
		return 1;
	}

	// 5. 서버 메인 대기 루프 (ESC 키 입력 시 종료)
	// 5-1. 사용자 종료 키 입력 폴링 대기 (ESC 키 입력 검출)
	std::cout << "[System] Press ESC to stop the server and exit...\n";
	while( 1 )
	{
		if( _kbhit() ) // 키 입력 여부 확인
		{
			char key = _getch(); // 입력 키 취득
			if( key == 27 )      // ESC 키의 ASCII 코드: 27
			{
				std::cout << "ESC pressed. Exiting..." << std::endl;
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // CPU 대기 낭비 방지
	}

	// 6. 안전한 프로그램 종료 및 자원 해제
	// 6-1. 서버 네트워크 서비스 중지 및 세션 정리
	std::cout << "[System] Closing HTTP Server Service...\n\n";
	if( serverService ) serverService->Close();

	// 6-2. 전역 시스템 자원 해제 (ThreadManager의 스레드 Join 및 메모리 풀/TLS 정리)
	BaseGlobal::Destroy();
	std::cout << "[System] BaseGlobal::Destroy()...\n";

	// 6-3. Winsock 라이브러리 정리
	CSocketUtils::Clear();
	std::cout << "[System] CSocketUtils::Clear()...\n";

	std::cout << "[System] Server terminated safely.\n";

	// 6-4. 콘솔 리소스 닫기 및 함수 종료
	CloseConsole();
	return 0;
}