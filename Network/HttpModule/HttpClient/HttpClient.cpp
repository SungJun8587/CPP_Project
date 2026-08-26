// HttpClient.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include "HttpClientSession.h"

#include <atomic>
#include <iostream>
#include <future>
#include <thread>

//***************************************************************************
// @brief GET 요청을 보내고 응답이 올 때까지 동기적으로 대기한 뒤 결과를 출력하는 헬퍼 함수입니다.
// @param client   요청을 보낼 HTTP 클라이언트
// @param url      요청 URL
// @param label    로그 출력에 사용할 리소스 이름(예: "index.html")
// @param isText   true면 본문을 텍스트로 출력, false면 크기만 출력
//***************************************************************************
void SyncGet(const CHttpClientRef& client, const std::string& url, const char* label, bool isText)
{
	std::promise<void> promise;
	auto future = promise.get_future();

	client->Get(url,
		[&promise, label, isText](HttpResponse resp) {
			if( resp.success && resp.statusCode == 200 )
			{
				std::cout << "[GET " << label << "] Success! Body Size: " << resp.body.size() << " bytes\n";
				if( isText )
					std::cout << "Content:\n" << resp.body << "\n\n";
				else
					std::cout << "\n";
			}
			else
			{
				std::cout << "[GET " << label << "] Failed. Status Code: " << resp.statusCode << "\n";
			}
			promise.set_value(); // 응답 수신 완료 알림
		});

	future.wait();
	PauseConsole();
	ClearConsoleScreen();
}

int main()
{
#ifdef _MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 1. 시스템 및 네트워크 환경 초기화
	// 1-1. 콘솔 UTF-8 입출력 인코딩 설정
	InitUtf8Console();

	// 1-2. 전역 BaseGlobal 시스템 모듈 초기화
	BaseGlobal::Init();
	std::cout << "[System] BaseGlobal::Init()...\n";

	// 1-3. Winsock 소켓 라이브러리 초기화
	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";

	// 2. 네트워크 코어 및 HTTP 클라이언트 초기화
	// 2-1. IOCP 코어 객체 생성
	auto iocpCore = std::make_shared<CIocpCore>();

	// 2-2. 워커 스레드 제어를 위한 플래그 및 워커 스레드 생성
	std::atomic<bool> isRunning{ true };

	// 워커 스레드 생성 (detach 대신 안전하게 loop 관리)
	std::thread workerThread([iocpCore]() {
		// Dispatch가 false(QUIT_KEY 수신)를 반환할 때까지 루프 수행
		while( iocpCore->Dispatch(INFINITE) )
		{
			// Dispatch 내부에서 일반 네트워크 I/O 패킷 처리
		}
		std::cout << "[Worker] Quit signal received. Thread exiting...\n";
		});

	// 2-3. HTTP 클라이언트 인스턴스 생성 
	// 커스텀한 CHttpClientSession을 템플릿 인자로 전달합니다.
	auto client = CHttpClient::CreateIocp<CHttpClientSession>(iocpCore, nullptr);

	std::cout << "========================================\n";
	std::cout << " [HTTP CLIENT] Requesting Static Files...\n";
	std::cout << "========================================\n";

	const std::string baseUrl = "http://127.0.0.1:7777/";

	// 3. index.html 정적 파일 요청 및 수신 처리
	SyncGet(client, baseUrl + "index.html", "index.html", /*isText*/ true);

	// 4. sample.jpg 바이너리 이미지 파일 요청 및 수신 처리
	SyncGet(client, baseUrl + "sample.jpg", "sample.jpg", /*isText*/ false);

	// 5. sample.json 데이터 파일 요청 및 수신 처리
	SyncGet(client, baseUrl + "sample.json", "sample.json", /*isText*/ true);

	// 6. sample.xml 데이터 파일 요청 및 수신 처리
	SyncGet(client, baseUrl + "sample.xml", "sample.xml", /*isText*/ true);

	// 7. 커넥션 풀 비동기 종료 처리
	client->CloseAll();

	// 8. 네트워크 코어 및 워커 스레드 종료 처리
	// 8-1. 워커 스레드에게 종료 신호 전송 (CloseAll 패킷들 "뒤"에 종료 패킷이 쌓임)
	iocpCore->PostQuit();

	// 8-2. 워커 스레드가 안전하게 종료될 때까지 대기
	if( workerThread.joinable() )
	{
		workerThread.join();
		std::cout << "[System] Worker thread joined successfully.\n";
	}

	// 9. 시스템 자원 해제 및 프로그램 종료
	// 9-1. 전역 시스템 모듈 해제
	BaseGlobal::Destroy();
	std::cout << "[System] BaseGlobal::Destroy()...\n";

	// 9-2. Winsock 자원 해제
	CSocketUtils::Clear();
	std::cout << "[System] CSocketUtils::Clear()...\n";

	// 9-3. 콘솔 정리 및 종료
	CloseConsole();

	return 0;
}