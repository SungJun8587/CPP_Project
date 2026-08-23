// MySQLAsync.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include <iostream>
#include <conio.h>

// 큐 최대 허용 크기 정의(Back-pressure를 위한 임계값)
const size_t MAX_QUEUE_CAPACITY = 10000;

static std::atomic<int> g_nProducerIndex(1);
std::atomic<bool>	g_bStopProducerThread(false);		// Producer 스레드 종료 플래그

// 동기화 객체 선언(condition_variable을 통한 즉시 응답형 슬립용)
std::mutex				g_producerMutex;
std::condition_variable g_producerCv;

// 생산자 DB 커넥션풀 선언
CMySQLConnPool* g_pProducerConnPool = nullptr;

// 멀티 생산자 환경에서 중복 없는 페이징을 위한 공유 Atomic 변수
std::atomic<int32>	g_sharedLastNo(0);

// 전역 완료 카운터 추가 (생산 완료된 총 건수 vs 처리 완료된 총 건수 추적용)
std::atomic<int64> g_totalProducedRows(0);
std::atomic<int64> g_totalConsumedRows(0);
std::atomic<bool> g_bProducerFinished(false); // 생산자 스레드 작업 완료 플래그

// 팩토리 함수 정의(메모리 안전성 및 가독성 향상)
template <typename T>
void PushAsyncRequest(int cmd, std::function<void(T*)> initializer)
{
	// 백프레셔 대기
	CMySQLAsyncSrv::Instance()->WaitPushCapacity(MAX_QUEUE_CAPACITY);

	// std::unique_ptr로 동적 생성하여 예외 안전성 확보
	auto pDBAsync = std::make_unique<T>();
	pDBAsync->callIdent = cmd;

	// 호출자가 전달한 람다식을 통해 데이터 초기화 수행
	initializer(pDBAsync.get());

	CMySQLAsyncSrv::Instance()->AddOutstandingRequest();

	// 비동기 서비스 큐에 unique_ptr 소유권을 이동(move)하여 푸시
	CMySQLAsyncSrv::Instance()->Push(std::move(pDBAsync));
}

void ProducerThread()
{
	while( !g_bStopProducerThread.load() )
	{
		for( int i = 0; i < MAX_ROWS; i += BATCH_SIZE )
		{
			if( g_bStopProducerThread.load() ) break;

			int currentBatchSize = BATCH_SIZE < (MAX_ROWS - i) ? BATCH_SIZE : MAX_ROWS - i;

			// 총 생산된 데이터 건수 누적
			g_totalProducedRows.fetch_add(currentBatchSize);

			PushAsyncRequest<PRODUCER_DATA_BATCH_REQ>(DBASYNC_BULKADD_PRODUCER_REQ, [i, currentBatchSize](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
				for( int j = 0; j < currentBatchSize; j++ )
				{
					int rowIdx = i + j + 1;

					pDBAsync->_producers[j].nNo = rowIdx;
					_stprintf_s(pDBAsync->_producers[j].tszName1, _countof(pDBAsync->_producers[j].tszName1), _T("이름_%d"), rowIdx);
					_stprintf_s(pDBAsync->_producers[j].tszName2, _countof(pDBAsync->_producers[j].tszName2), _T("LastName_%d"), rowIdx);
					pDBAsync->_producers[j].bFlag = (rowIdx % 2) != 0;
					pDBAsync->_producers[j].nAge = 20 + (rowIdx % 50);
				}
				pDBAsync->_dataCount = currentBatchSize;
			});
		}

		int currentIndex = g_nProducerIndex.fetch_add(1);
		LOG_INFO(_T("[%d] %s data have been entered(Total : %s)"), currentIndex, addCommas(MAX_ROWS).c_str(), addCommas(currentIndex * MAX_ROWS).c_str());

		// condition_variable을 적용한 고응답성 슬립 로직 (ESC 입력 시 즉시 탈출)
		{
			std::unique_lock<std::mutex> lock(g_producerMutex);
			g_producerCv.wait_for(lock, std::chrono::seconds(INTERVAL_SEC), []() {
				return g_bStopProducerThread.load();
			});
		}
	}
}

bool FetchMigrationData(int32 batchSize, std::unique_ptr<PRODUCER_DATA[]>& outBatchReq, int32& outFetchedCount)
{
	if( g_pProducerConnPool == nullptr )
	{
		LOG_ERROR(_T("FetchMigrationData: Producer connection pool is not initialized."));
		return false;
	}

	MySQLConnGuard pConn(g_pProducerConnPool);
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("FetchMigrationData: No available MySQL connection in producer pool."));
		return false;
	}

	// 공유 Atomic 변수로부터 현재 기준이 되는 lastNo를 안전하게 가져옴
	int32 currentLastNo = g_sharedLastNo.load();

	outBatchReq = std::unique_ptr<PRODUCER_DATA[]>(new PRODUCER_DATA[batchSize]);
	memset(outBatchReq.get(), 0, sizeof(PRODUCER_DATA) * batchSize);

	char szQuery[256];
	sprintf_s(szQuery, sizeof(szQuery),
		"SELECT No, Name1, Name2, Flag, Age FROM Producer WHERE No > %d ORDER BY No ASC LIMIT %d",
		currentLastNo, batchSize);

	MYSQL_RES* pRes = nullptr;
	if( !pConn->Query(szQuery, pRes) )
	{
		LOG_ERROR(_T("FetchMigrationData: Failed to execute query. Query: %s"), szQuery);
		return false;
	}

	int fetchedCount = 0;
	int32 maxFetchedNo = currentLastNo;

	while( MYSQL_ROW Row = mysql_fetch_row(pRes) )
	{
		if( fetchedCount >= batchSize )
		{

			break; // 배치 크기 초과 방지 안전장치
		}

		int i = 0;
		pConn->GetData(Row, i, outBatchReq[fetchedCount].nNo);
		pConn->GetData(Row, i + 1, outBatchReq[fetchedCount].tszName1, _countof(outBatchReq[fetchedCount].tszName1));
		pConn->GetData(Row, i + 2, outBatchReq[fetchedCount].tszName2, _countof(outBatchReq[fetchedCount].tszName2));
		pConn->GetData(Row, i + 3, outBatchReq[fetchedCount].bFlag);
		pConn->GetData(Row, i + 4, outBatchReq[fetchedCount].nAge);

		// 다음 루프/페이지를 위해 마지막으로 읽은 No 갱신
		maxFetchedNo = outBatchReq[fetchedCount].nNo;

		fetchedCount++;
	}
	pConn->FreeResult(pRes);

	if( fetchedCount == 0 )
	{
		return false; // 더 이상 가져올 데이터 없음
	}

	// 다른 스레드와 경합하여 더 큰 번호로 안전하게 갱신(CAS 기반 업데이트)
	int32 expectedNo = currentLastNo;
	while( maxFetchedNo > expectedNo && !g_sharedLastNo.compare_exchange_weak(expectedNo, maxFetchedNo) )
	{
		// 다른 스레드가 먼저 더 큰 번호로 업데이트 했다면 멈춤
		if( expectedNo >= maxFetchedNo ) break;
	}

	outFetchedCount = fetchedCount;

	return true;
}

void DBProducerThread()
{
	int32 batchSize = BATCH_SIZE;

	while( !g_bStopProducerThread.load() )
	{
		std::unique_ptr<PRODUCER_DATA[]> pBatchReq = nullptr;
		int32 fetchedCount = 0;

		// 분리한 함수 호출로 데이터 조회 및 버퍼 구성 위임
		bool bHasData = FetchMigrationData(batchSize, pBatchReq, fetchedCount);
		if( !bHasData || fetchedCount == 0 )
		{
			LOG_INFO(_T("Migration source data is empty or completed. Stopping migration producer."));
			break;
		}

		int32 lastNo = pBatchReq[fetchedCount - 1].nNo;

		// 총 생산된 데이터 건수 누적
		g_totalProducedRows.fetch_add(fetchedCount);

		PRODUCER_DATA* rawProducers = pBatchReq.release();

		PushAsyncRequest<CONSUMER_DATA_BATCH_REQ>(DBASYNC_BULKADD_CONSUMER_REQ, [rawProducers, fetchedCount](CONSUMER_DATA_BATCH_REQ* pDBAsync) {

			pDBAsync->_dataCount = fetchedCount;
			for( int j = 0; j < fetchedCount; j++ )
			{
				pDBAsync->_consumers[j].nNo = rawProducers[j].nNo;
				_tcscpy_s(pDBAsync->_consumers[j].tszName1, 50, rawProducers[j].tszName1);
				_tcscpy_s(pDBAsync->_consumers[j].tszName2, 50, rawProducers[j].tszName2);
				pDBAsync->_consumers[j].bFlag = rawProducers[j].bFlag;
				pDBAsync->_consumers[j].nAge = rawProducers[j].nAge;
			}

			delete[] rawProducers;
			});

		LOG_INFO(_T("Migrated rows queued. Last No: %d, Count: %d"), lastNo, fetchedCount);

		if( fetchedCount < batchSize )
		{
			LOG_INFO(_T("All rows have been successfully fetched and queued for migration."));
			break;
		}

		// 과도한 DB 부하 방지 및 ESC 즉시 탈출을 위한 슬립
		{
			std::unique_lock<std::mutex> lock(g_producerMutex);
			g_producerCv.wait_for(lock, std::chrono::milliseconds(50), []() {
				return g_bStopProducerThread.load();
				});
		}
	}

	g_bProducerFinished.store(true);
}

int main()
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 한글 콘솔 출력 설정
	_tsetlocale(LC_ALL, _T("Korean"));

	mysql_library_init(0, NULL, NULL);		// MySQL 라이브러리 초기화(프로그램에서 단 한 번만 호출)

	TCHAR tszTempArgv[FULLPATH_STRLEN] = { 0, };

	BaseGlobal::Init();

	//_sntprintf_s(tszTempArgv, FULLPATH_STRLEN, _TRUNCATE, _T("Config\\server_config_mssql.json"));
	_sntprintf_s(tszTempArgv, FULLPATH_STRLEN, _TRUNCATE, _T("..\\Config\\server_config_mysql.json"));

	if( false == SERVER_CONFIG->Init(tszTempArgv) )
	{
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail."));
		SERVER_CONFIG->ReleaseInstance();
		return -1;
	}

	/*
	for( int i = 1; i <= 20; i++ )
	{
		PushAsyncRequest<PRODUCER_DATA_BATCH_REQ>(DBASYNC_ADD_PRODUCER_REQ, [c_i = i](PRODUCER_DATA_BATCH_REQ* pDBAsync) {

			pDBAsync->_producers[0].nNo = c_i;
			_stprintf_s(pDBAsync->_producers[0].tszName1, 50, _T("이름_%d"), c_i);
			_stprintf_s(pDBAsync->_producers[0].tszName2, 50, _T("LastName_%d"), c_i);
			pDBAsync->_producers[0].bFlag = (c_i % 2) != 0;
			pDBAsync->_producers[0].nAge = 20 + (c_i % 50);
			});
	}
	*/

	/*
	PushAsyncRequest<PRODUCER_DATA_BATCH_REQ>(DBASYNC_GET_PRODUCER_REQ, [](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
		});
	*/

	/*
	PushAsyncRequest<PRODUCER_DATA_BATCH_REQ>(DBASYNC_LIST_PRODUCER_REQ, [](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
		});
	*/

	// 단일 생산자 구조로 변경하여 데이터 중복 조회 원천 차단
	// 생산자를 여러 개 둔다고 해서 마이그레이션 전체 속도가 빨라지지도 않으며 오히려 데이터 중복/누락(오인식) 문제가 발생
	int32 producerThreadCnt = 1;
	//int32 consumerThreadCnt = 1;
	int32 consumerThreadCnt = static_cast<int32>(SYSTEM::CoreCount());

	// 생산자 스레드 전용 ODBC 커넥션 풀 초기화 및 설정
	const auto& dbNodeVec = SERVER_CONFIG->GetDBNodeVec();
	if( !dbNodeVec.empty() )
	{
		const auto& dbNode = dbNodeVec[0];
		g_pProducerConnPool = new CMySQLConnPool(producerThreadCnt);
		CMySQLConnPool::TReconnectConfig reconnectCfg;
		reconnectCfg.nWorkerCount = producerThreadCnt;

		if( !g_pProducerConnPool->Init(dbNode._tszDBHost, dbNode._tszDBUserId, dbNode._tszDBPasswd,
			dbNode._tszDBName, dbNode._nPort, reconnectCfg) )
		{
			LOG_ERROR(_T("Failed to initialize Producer dedicated DB connection pool."));
			delete g_pProducerConnPool;
			g_pProducerConnPool = nullptr;
			SERVER_CONFIG->ReleaseInstance();
			return -1;
		}
	}

	if( false == CMySQLAsyncSrv::Instance()->StartService(SERVER_CONFIG->GetDBNodeVec(), consumerThreadCnt) )	// DB Async Call Service 시작
	{
		LOG_ERROR(_T("Failed to CMySQLAsyncSrv initialize."));
		SERVER_CONFIG->ReleaseInstance();
		return -1;
	}

	CMySQLAsyncSrv::Instance()->StartIoThreads();
	std::cout << "Consumer " << CMySQLAsyncSrv::Instance()->_nMaxThreadCnt << " threads started." << std::endl;

	for( int i = 0; i < producerThreadCnt; ++i )
	{
		gpThreadManager->CreateThread([=]() {
			try {
				//ProducerThread();
				DBProducerThread();
				//g_bProducerFinished.store(true);
			}
			catch( const std::exception& e ) {
				LOG_ERROR(_T("Unhandled exception in producer thread: %S"), e.what());
				throw;
			}
			catch( ... ) {
				LOG_ERROR(_T("Unhandled unknown exception in producer thread"));
				throw;
			}
		});
	}
	std::cout << "Producer " << producerThreadCnt << " thread started." << std::endl;

	std::cout << "It is processing. Please wait a moment." << std::endl;
	std::cout << "Press ESC to exit." << std::endl;

	while( 1 )
	{
		// 생산자 작업이 끝났고, 큐에 남아있는 대기/처리 중인 요청이 없다면 자동 종료
		if( g_bProducerFinished.load() && CMySQLAsyncSrv::Instance()->GetOutstandingRequests() == 0 )
		{
			std::cout << "\n[SUCCESS] All migration data has been successfully produced and consumed!" << std::endl;

			if( g_totalProducedRows.load() > 0 )
				std::cout << "[통계] 총 생산(Producer) 건수: " << g_totalProducedRows.load()
					<< " 건 / 총 처리(Consumer) 건수: " << g_totalConsumedRows.load() << " 건" << std::endl;
			break;
		}

		if( _kbhit() )	// 키 입력이 있을 경우
		{
			char key = _getch();	// 입력된 키를 가져옴
			if( key == 27 )			// ESC 키의 ASCII 코드 : 27
			{
				std::cout << "ESC pressed. Exiting..." << std::endl;
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // CPU 낭비 방지
	}

	// 생산자 스레드 종료 신호 전달 및 즉시 깨우기
	g_bStopProducerThread = true;
	{
		std::lock_guard<std::mutex> lock(g_producerMutex);
		g_producerCv.notify_all();
	}

	// 생산자 스레드가 완전히 종료될 때까지 대기
	std::cout << "Waiting for producer thread to stop..." << std::endl;
	gpThreadManager->JoinLastThreads(producerThreadCnt);
	std::cout << "Producer thread stopped safely." << std::endl;

	std::cout << "Waiting for worker threads to process all remaining requests..." << std::endl;
	while( CMySQLAsyncSrv::Instance()->GetOutstandingRequests() > 0 )
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	
	std::cout << "All queue tasks and in-flight DB operations fully processed." << std::endl;

	if( g_totalProducedRows.load() > 0 )
		std::cout << " 최종 검증 -> 생산된 총 데이터: " << g_totalProducedRows.load()
			<< " 건, 처리된 총 데이터: " << g_totalConsumedRows.load() << " 건" << std::endl;

	// 비동기 DB 서비스 정지
	CMySQLAsyncSrv::Instance()->StopThread();

	// Wait Main thread & Check DEADLOCK
	gpThreadManager->JoinThreads();

	std::cout << "Thread processing completed." << std::endl;

	if( g_pProducerConnPool != nullptr )
	{
		delete g_pProducerConnPool;
		g_pProducerConnPool = nullptr;
	}

	SERVER_CONFIG->ReleaseInstance();

	BaseGlobal::Destroy();

	mysql_library_end();					// MySQL 라이브러리 메모리 정리(프로그램 종료 시 한 번만 호출)

	system("pause");

	return 0;
}