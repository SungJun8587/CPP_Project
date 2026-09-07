// ADOAsync.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//
/*
========================================================================================
[CRT 메모리 누수 감지 경고(False Positive) 발생 원인 분석]
----------------------------------------------------------------------------------------
1. CRT Leak Dump 시점:
   - _CRTDBG_LEAK_CHECK_DF 플래그 설정 시, 자동 메모리 누수 덤프(_CrtDumpMemoryLeaks)는
	 main() 함수가 완전히 종료된 후 CRT Cleanup(exit() 수순) 과정에서 호출됩니다.
2. thread_local / TLS 소멸 타이밍 차이:
   - 스레드 로컬 객체(thread_local) 및 CRT 내부 TLS 자원은 스레드 해제 프로세스 또는
	 CRT 정적 객체 소멸 단계에서 해제됩니다.
   - 이 과정이 CRT Cleanup의 _CrtDumpMemoryLeaks() 호출 시점보다 '더 나중에' 실행될 수 있습니다.
3. 가짜 양성 (False Positive):
   - CRT 누수 감지기는 아직 해제되지 않은 TLS 힙 메모리를 '실제 누수'로 오인하여 보고서를 출력합니다.
   - 프로그램 종료 시 정상 해제되는 객체이므로 실질적인 메모리 누수가 아닌 CRT 스레드 정리 시점 차이로
	 발생하는 False Positive 현상입니다.
========================================================================================
*/
#include "pch.h"
#include <iostream>
#include <conio.h>
#include "DbServiceManager.h"

// 전역 변수 및 상수 정의
const size_t MAX_QUEUE_CAPACITY = 10000;				// 큐 최대 허용 크기 (Back-pressure 제어용 임계값)

static std::atomic<int> g_nProducerIndex(1);
std::atomic<bool>		g_bStopProducerThread(false);	// Producer 스레드 종료 플래그
std::atomic<int32>		g_sharedLastNo(0);				// 중복 없는 페이징을 위한 공유 PK 변수
std::atomic<int64>		g_totalProducedRows(0);			// 총 생산 건수
std::atomic<int64>		g_totalConsumedRows(0);			// 총 소비 처리 건수
std::atomic<bool>		g_bProducerFinished(false);		// 생산자 스레드 작업 완료 플래그

std::mutex				g_producerMutex;				// 고응답성 슬립용 뮤텍스
std::condition_variable g_producerCv;					// 고응답성 슬립용 조건 변수

CAdoConnPool*			g_pProducerConnPool = nullptr;		// 생산자 DB 커넥션풀

//***************************************************************************
// @brief 메모리 상에서 가상 더미 데이터를 생성하여 Async Queue에 푸시하는 함수
//***************************************************************************
void ProducerThread()
{
	// 1. 종료 플래그(g_bStopProducerThread)가 설정될 때까지 데이터 생성 루프 수행
	while( !g_bStopProducerThread.load() )
	{
		// 1-1. 전체 생성 건수(MAX_ROWS)를 배치 단위(BATCH_SIZE)로 나누어 루프 처리
		for( int i = 0; i < MAX_ROWS; i += BATCH_SIZE )
		{
			int currentBatchSize = BATCH_SIZE < (MAX_ROWS - i) ? BATCH_SIZE : MAX_ROWS - i;

			// 1-2. 생산된 총 데이터 건수를 원자적으로 누적 카운트
			g_totalProducedRows.fetch_add(currentBatchSize);

			// 1-3. 생성된 더미 데이터를 비동기 DB 작업 요청 큐에 푸시
			TryPushDBAsyncRequest<CAdoAsyncSrv, PRODUCER_DATA_BATCH_REQ>(MEMBER_DB_ASYNC, DBASYNC_BULKADD_PRODUCER_REQ, [i, currentBatchSize](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
				for( int j = 0; j < currentBatchSize; j++ )
				{
					int rowIdx = i + j + 1;
					pDBAsync->_producers[j].nNo = rowIdx;
					_stprintf_s(pDBAsync->_producers[j].tszName1, _countof(pDBAsync->_producers[j].tszName1), _T("이름_%d"), rowIdx);
					pDBAsync->_producers[j].nName1Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_producers[j].tszName1) * sizeof(TCHAR));;
					_stprintf_s(pDBAsync->_producers[j].tszName2, _countof(pDBAsync->_producers[j].tszName2), _T("LastName_%d"), rowIdx);
					pDBAsync->_producers[j].nName2Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_producers[j].tszName2) * sizeof(TCHAR));;
					pDBAsync->_producers[j].bFlag = (rowIdx % 2) != 0;
					pDBAsync->_producers[j].nAge = 20 + (rowIdx % 50);
				}
				pDBAsync->_dataCount = currentBatchSize;
				}, MAX_QUEUE_CAPACITY);
		}

		// 1-4. 배치 생산 완료 로깅 수행
		int currentIndex = g_nProducerIndex.fetch_add(1);
		LOG_INFO(_T("[%d] %s data have been entered(Total : %s)"), currentIndex, addCommas(MAX_ROWS).c_str(), addCommas(currentIndex * MAX_ROWS).c_str());

		// 1-5. condition_variable을 사용해 대기 중 반응성을 보장하는 슬립 수행
		{
			std::unique_lock<std::mutex> lock(g_producerMutex);
			g_producerCv.wait_for(lock, std::chrono::seconds(INTERVAL_SEC), []() {
				return g_bStopProducerThread.load();
				});
		}
	}
}

//***************************************************************************
// @brief DB로부터 배치 크기만큼 데이터를 페이징 조회해 오는 함수
//***************************************************************************
bool FetchMigrationData(int32 batchSize, std::unique_ptr<PRODUCER_DATA[]>& outBatchReq, int32& outFetchedCount)
{
	// 1. 커넥션 풀 초기화 상태 유효성 검사
	if( g_pProducerConnPool == nullptr )
	{
		LOG_ERROR(_T("FetchMigrationData: Producer connection pool is not initialized."));
		return false;
	}

	// 2. 커넥션 풀에서 사용 가능한 DB 연결 객체 획득 (RAII Guard)
	AdoConnGuard pConn(g_pProducerConnPool);
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("FetchMigrationData: No available ADO connection in producer pool."));
		return false;
	}

	// 3. 현재 조회 시작 기준이 되는 PK 값(lastNo)을 원자적으로 로드
	int32 currentLastNo = g_sharedLastNo.load();

	// 4. 인출 데이터를 담을 동적 배열 버퍼 생성 및 메모리 초기화
	outBatchReq = std::unique_ptr<PRODUCER_DATA[]>(new PRODUCER_DATA[batchSize]);
	memset(outBatchReq.get(), 0, sizeof(PRODUCER_DATA) * batchSize);

	// 5. Keyset Paging (No > currentLastNo) 기반 SQL 쿼리문 생성
	TCHAR szQuery[256];
	_stprintf_s(szQuery, _countof(szQuery),
		_T("SELECT No, Name1, Name2, Flag, Age FROM Producer WHERE No > %d ORDER BY No ASC LIMIT %d"),
		currentLastNo, batchSize);

	// 6. ADO 쿼리 실행 및 Recordset 오픈
	if( !pConn->Open(szQuery) )
	{
		LOG_ERROR(_T("FetchMigrationData: Failed to execute query. Query: %s"), szQuery);
		pConn->RSClose();
		return false;
	}
	int fetchedCount = 0;
	int32 maxFetchedNo = currentLastNo;

	// 7. Recordset을 순회하면서 배치 버퍼에 데이터 패킹
	while( !pConn->IsEOF() )
	{
		if( fetchedCount >= batchSize )
		{
			break; // 배치 크기 초과 방지
		}
		long nNoVal = 0;
		long nAgeVal = 0;
		TCHAR szName1[256] = { 0, };
		TCHAR szName2[256] = { 0, };
		TCHAR szFlag[16] = { 0, };

		// 7-1. Recordset 컬럼 값 인출
		pConn->GetFieldByIndex(0L, nNoVal);                      // No
		pConn->GetFieldByIndex(1L, szName1, _countof(szName1));   // Name1
		pConn->GetFieldByIndex(2L, szName2, _countof(szName2));   // Name2
		pConn->GetFieldByIndex(3L, szFlag, _countof(szFlag));     // Flag
		pConn->GetFieldByIndex(4L, nAgeVal);                      // Age

		// 7-2. 구조체 필드 변환 및 복사
		outBatchReq[fetchedCount].nNo = static_cast<int32>(nNoVal);
		_tcscpy_s(outBatchReq[fetchedCount].tszName1, _countof(outBatchReq[fetchedCount].tszName1), szName1);
		_tcscpy_s(outBatchReq[fetchedCount].tszName2, _countof(outBatchReq[fetchedCount].tszName2), szName2);
		outBatchReq[fetchedCount].bFlag = (_ttoi(szFlag) != 0);
		outBatchReq[fetchedCount].nAge = static_cast<int32>(nAgeVal);

		// 7-3. 이번 배치에서 인출한 가장 큰 PK 번호 추적
		maxFetchedNo = outBatchReq[fetchedCount].nNo;
		fetchedCount++;

		// 7-4. 다음 Recordset으로 이동
		if( !pConn->Next() )
		{
			break;
		}
	}

	// 8. Recordset 자원 해제
	pConn->RSClose();

	// 9. 인출된 데이터 존재 여부 검증
	if( fetchedCount == 0 )
	{
		return false;
	}

	// 10. CAS(Compare-And-Swap) 루프를 통해 공유 PK 변수(g_sharedLastNo)를 최신값으로 원자적 갱신
	int32 expectedNo = currentLastNo;
	while( maxFetchedNo > expectedNo && !g_sharedLastNo.compare_exchange_weak(expectedNo, maxFetchedNo) )
	{
		if( expectedNo >= maxFetchedNo ) break;
	}
	outFetchedCount = fetchedCount;
	return true;
}

//***************************************************************************
// @brief DB에서 마이그레이션 대상 데이터를 조회하여 Consumer 큐로 푸시하는 스레드
//***************************************************************************
void DBProducerThread()
{
	int32 batchSize = BATCH_SIZE;

	// 1. 중단 플래그가 켜질 때까지 메인 작업 루프 실행
	while( !g_bStopProducerThread.load() )
	{
		std::unique_ptr<PRODUCER_DATA[]> pBatchReq = nullptr;
		int32 fetchedCount = 0;

		// 1-1. DB에서 배치 단위 데이터 가져오기
		bool bHasData = FetchMigrationData(batchSize, pBatchReq, fetchedCount);
		if( !bHasData || fetchedCount == 0 )
		{
			LOG_INFO(_T("Migration source data is empty or completed. Stopping migration producer."));
			break;
		}
		int32 lastNo = pBatchReq[fetchedCount - 1].nNo;

		// 1-2. 생산된 데이터 건수 누적 카운트
		g_totalProducedRows.fetch_add(fetchedCount);

		// 1-3. 람다 캡처 및 전달을 위해 smart pointer 소유권 이관
		PRODUCER_DATA* rawProducers = pBatchReq.release();

		// 1-4. Consumer가 처리할 비동기 DB 작업 요청 큐에 푸시
		TryPushDBAsyncRequest<CAdoAsyncSrv, CONSUMER_DATA_BATCH_REQ>(MEMBER_DB_ASYNC, DBASYNC_BULKADD_CONSUMER_REQ, [rawProducers, fetchedCount](CONSUMER_DATA_BATCH_REQ* pDBAsync) {
			pDBAsync->_dataCount = fetchedCount;
			for( int j = 0; j < fetchedCount; j++ )
			{
				pDBAsync->_consumers[j].nNo = rawProducers[j].nNo;
				_tcscpy_s(pDBAsync->_consumers[j].tszName1, _countof(pDBAsync->_consumers[j].tszName1), rawProducers[j].tszName1);
				pDBAsync->_consumers[j].nName1Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_consumers[j].tszName1) * sizeof(TCHAR));;
				_tcscpy_s(pDBAsync->_consumers[j].tszName2, _countof(pDBAsync->_consumers[j].tszName2), rawProducers[j].tszName2);
				pDBAsync->_consumers[j].nName2Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_consumers[j].tszName2) * sizeof(TCHAR));;
				pDBAsync->_consumers[j].bFlag = rawProducers[j].bFlag;
				pDBAsync->_consumers[j].nAge = rawProducers[j].nAge;
			}
			// 작업 완료 후 데이터 동적 할당 메모리 해제
			delete[] rawProducers;
			}, MAX_QUEUE_CAPACITY);
		LOG_INFO(_T("Migrated rows queued. Last No: %d, Count: %d"), lastNo, fetchedCount);

		// 1-5. 인출된 개수가 배치 크기보다 적으면 전체 데이터 처리 완료로 판단 후 종료
		if( fetchedCount < batchSize )
		{
			LOG_INFO(_T("All rows have been successfully fetched and queued for migration."));
			break;
		}

		// 1-6. DB 부하 방지 및 종료 요청 즉시 반응을 위한 미세 대기
		{
			std::unique_lock<std::mutex> lock(g_producerMutex);
			g_producerCv.wait_for(lock, std::chrono::milliseconds(50), []() {
				return g_bStopProducerThread.load();
				});
		}
	}

	// 2. 생산자 스레드 작업 완료 플래그 설정
	g_bProducerFinished.store(true);
}

void SubAddTest()
{
	for( int i = 1; i <= 20; i++ )
	{
		TryPushDBAsyncRequest<CAdoAsyncSrv, PRODUCER_DATA_BATCH_REQ>(MEMBER_DB_ASYNC, DBASYNC_ADD_PRODUCER_REQ, [c_i = i](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
			pDBAsync->_producers[0].nNo = c_i;
			_stprintf_s(pDBAsync->_producers[0].tszName1, _countof(pDBAsync->_producers[0].tszName1), _T("이름_%d"), c_i);
			pDBAsync->_producers[0].nName1Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_producers[0].tszName1) * sizeof(TCHAR));;
			_stprintf_s(pDBAsync->_producers[0].tszName2, _countof(pDBAsync->_producers[0].tszName2), _T("LastName_%d"), c_i);
			pDBAsync->_producers[0].nName2Ind = static_cast<SQLLEN>(_tcslen(pDBAsync->_producers[0].tszName2) * sizeof(TCHAR));;
			pDBAsync->_producers[0].bFlag = (c_i % 2) != 0;
			pDBAsync->_producers[0].nAge = 20 + (c_i % 50);
			}, MAX_QUEUE_CAPACITY);
	}
}

void SubGetTest()
{
	TryPushDBAsyncRequest<CAdoAsyncSrv, PRODUCER_DATA_BATCH_REQ>(MEMBER_DB_ASYNC, DBASYNC_GET_PRODUCER_REQ, [](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
		}, MAX_QUEUE_CAPACITY);
}

void SubListTest()
{
	TryPushDBAsyncRequest<CAdoAsyncSrv, PRODUCER_DATA_BATCH_REQ>(MEMBER_DB_ASYNC, DBASYNC_LIST_PRODUCER_REQ, [](PRODUCER_DATA_BATCH_REQ* pDBAsync) {
		}, MAX_QUEUE_CAPACITY);
}

//***************************************************************************
// @brief 프로그램 종료 시 인프라 자원 및 동적 할당 객체를 정해진 순서대로 해제하는 함수
// @details
// ShutdownAll()은 Stop()/Join()에 더해 실제 파괴(reset)까지 수행하며,
// 반드시 BaseGlobal::Destroy()보다 먼저 호출되어야 한다. 
// 그래야 CAdoConnPool의 헬스체크/재연결 워커 스레드가 gpMemory/gpThreadManager가
// 살아있는 동안 안전하게 정리된다(DbServiceManager.h의 [소멸 순서] 설명 참고).
//***************************************************************************
void MainClose()
{
	// 1. Producer 전용 커넥션 풀 동적 메모리 해제
	if( g_pProducerConnPool != nullptr )
	{
		delete g_pProducerConnPool;
		g_pProducerConnPool = nullptr;
	}

	// 2. DB 비동기 서비스 정리 — 반드시 BaseGlobal::Destroy()보다 먼저 호출
	CDbServiceManager::Instance().ShutdownAll();
	SERVER_CONFIG->ReleaseInstance();
	BaseGlobal::Destroy();
}

//***************************************************************************
// @brief 프로그램의 메인 진입점 및 스레드 구동/종료 제어 함수
//***************************************************************************
int main()
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 및 기본 프레임워크 초기화
	InitUtf8Console();

	TCHAR tszTempArgv[FULLPATH_STRLEN] = { 0, };
	
	BaseGlobal::Init();

	// 3. 서버 DB 설정 파일 로드
	_sntprintf_s(tszTempArgv, FULLPATH_STRLEN, _TRUNCATE, _T("..\\Config\\server_config_mysql.json"));
	if( false == SERVER_CONFIG->Init(tszTempArgv) )
	{
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail. (Path: %s)"), tszTempArgv);
		
		MainClose();
		return -1;
	}

	// 4. 설정 파일 내 DB 노드 정보 존재 여부 검증
	const auto& dbNodeVec = SERVER_CONFIG->GetDBNodeVec();
	if( dbNodeVec.empty() )
	{
		LOG_ERROR(_T("DBNode configuration is empty. (Path: %s)"), tszTempArgv);
		
		MainClose();
		return -1;
	}

	// 5. Producer 및 Consumer 스레드 개수 설정 (데이터 정합성을 위해 Producer는 1개 사용)
	int32 producerThreadCnt = 1;
	//int32 consumerThreadCnt = 1;
	int32 consumerThreadCnt = static_cast<int32>(SYSTEM::CoreCount());

	// 6. Producer 전용 DB 커넥션 풀 생성 및 초기화
	if( producerThreadCnt > 0 )
	{
		const auto& dbNode = dbNodeVec[0];
		g_pProducerConnPool = new CAdoConnPool(producerThreadCnt);
		CAdoConnPool::TReconnectConfig reconnectCfg;
		reconnectCfg.nWorkerCount = producerThreadCnt;
		if( !g_pProducerConnPool->Init(dbNode._dbClass, dbNode._tszDSN, 5, reconnectCfg) )
		{
			LOG_ERROR(_T("Failed to initialize Producer dedicated ADO connection pool."));
			
			MainClose();
			return -1;
		}
	}

	// 7. 비동기 DB 처리 서버(Consumer 서비스) 구동
	if( false == MEMBER_DB_ASYNC.StartService(dbNodeVec, consumerThreadCnt) )
	{
		LOG_ERROR(_T("Failed to CAdoAsyncSrv initialize."));
		
		MainClose();
		return -1;
	}
	std::cout << "Consumer " << consumerThreadCnt << " threads started." << std::endl;

	// 8. Producer 스레드 생성 및 구동
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

	// 9. 메인 모니터링 루프 (완료 상태 감시 및 키보드 ESC 입력 대기)
	while( 1 )
	{
		// 9-1. 생산이 완료되고 Consumer 큐의 모든 작업이 소진되었는지 확인 후 정상 완료 처리
		if( g_bProducerFinished.load() && MEMBER_DB_ASYNC.GetOutstandingRequests() == 0 )
		{
			std::cout << "\n[SUCCESS] All migration data has been successfully produced and consumed!" << std::endl;
			if( g_totalProducedRows.load() > 0 )
				std::cout << "[통계] 총 생산(Producer) 건수: " << g_totalProducedRows.load()
				<< " 건 / 총 처리(Consumer) 건수: " << g_totalConsumedRows.load() << " 건" << std::endl;
			break;
		}

		// 9-2. 키보드 입력 체크 (ESC 눌림 시 중단)
		if( _kbhit() )
		{
			char key = _getch();
			if( key == 27 ) // ESC 키
			{
				std::cout << "ESC pressed. Exiting..." << std::endl;
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // CPU 과점유 방지
	}

	// 10. 스레드 안전 종료 시퀀스 수행
	g_bStopProducerThread = true;

	// 10-1. 대기 중인 Producer 스레드의 condition_variable 깨우기
	{
		std::lock_guard<std::mutex> lock(g_producerMutex);
		g_producerCv.notify_all();
	}

	// 10-2. Producer 스레드 Join 종료 대기
	std::cout << "Waiting for producer thread to stop..." << std::endl;
	gpThreadManager->JoinLastThreads(producerThreadCnt);
	std::cout << "Producer thread stopped safely." << std::endl;

	// 10-3. Consumer 큐에 남아 있는 잔여 DB 처리 완전 소진 대기
	std::cout << "Waiting for worker threads to process all remaining requests..." << std::endl;
	while( MEMBER_DB_ASYNC.GetOutstandingRequests() > 0 )
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	std::cout << "All queue tasks and in-flight DB operations fully processed." << std::endl;

	// 10-4. 최종 처리 건수 통계 검증 출력
	if( g_totalProducedRows.load() > 0 )
		std::cout << " 최종 검증 -> 생산된 총 데이터: " << g_totalProducedRows.load()
		<< " 건, 처리된 총 데이터: " << g_totalConsumedRows.load() << " 건" << std::endl;

	// 11. DB 비동기 서버 정지
	// 실제 정지/파괴는 뒤이은 MainClose()의 ShutdownAll()이 담당한다
	// (Stop()/Join()은 멱등이라 이 지점에서 미리 한 번 더 불러도 안전하다).
	MEMBER_DB_ASYNC.Stop();
	MEMBER_DB_ASYNC.Join();
	std::cout << "Thread processing completed." << std::endl;

	// 12. 전체 시스템 종료 자원 해제 처리
	MainClose();
	CloseConsole();

	return 0;
}