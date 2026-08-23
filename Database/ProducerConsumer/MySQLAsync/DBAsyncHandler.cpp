
//***************************************************************************
// DBAsyncHandler.cpp : implementation of the DBAsyncHandler class.
//
//***************************************************************************

#include "pch.h"
#include "DBAsyncHandler.h"

extern std::atomic<int64> g_totalConsumedRows;

DECLARE_DBASYNC_HANDLER(DBASYNC_ADD_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	MySQLConnGuard pConn(CMySQLAsyncSrv::Instance()->GetAccountConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_ADD_PRODUCER_REQ: No available MySQL connection in pool."));
		return EDBReturnType::INVALID;
	}

	pConn->Prepare("INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES(?, ?, ?, ?)");

	ulong ulNameLen1 = (ulong)_tcslen(pDBParam->_producers[0].tszName1);
	ulong ulNameLen2 = (ulong)_tcslen(pDBParam->_producers[0].tszName2);

	CVector<MYSQL_BIND> bindParams = {
		CBaseMySQL::BindParam(pDBParam->_producers[0].tszName1, ulNameLen1),
		CBaseMySQL::BindParam(pDBParam->_producers[0].tszName2, ulNameLen2),
		CBaseMySQL::BindParam(pDBParam->_producers[0].bFlag),
		CBaseMySQL::BindParam(pDBParam->_producers[0].nAge)
	};

	if( !pConn->PrepareBindParam(bindParams) )
	{
		pConn->StmtClose();
		return EDBReturnType::INVALID;
	}

	if( !pConn->PrepareExecute() )
	{
		pConn->StmtClose();
		return EDBReturnType::INVALID;
	}

	for( const MYSQL_BIND& bindParam : bindParams )
		pConn->ClearBindParam(bindParam);

	pConn->StmtClose();

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_GET_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	MySQLConnGuard pConn(CMySQLAsyncSrv::Instance()->GetAccountConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_GET_PRODUCER_REQ: No available MySQL connection in pool."));
		return EDBReturnType::INVALID;
	}

	MYSQL_RES* pRes = nullptr;
	pConn->Query("SELECT * FROM Producer WHERE No = 13", pRes);

	int nBufSize1 = _countof(pDBParam->_producers[0].tszName1);
	int nBufSize2 = _countof(pDBParam->_producers[0].tszName2);

	int i = 0;
	while( MYSQL_ROW Row = mysql_fetch_row(pRes) )
	{
		pConn->GetData(Row, i, pDBParam->_producers[0].nNo);
		pConn->GetData(Row, i + 1, pDBParam->_producers[0].tszName1, nBufSize1);
		pConn->GetData(Row, i + 2, pDBParam->_producers[0].tszName2, nBufSize2);
		pConn->GetData(Row, i + 3, pDBParam->_producers[0].bFlag);
		pConn->GetData(Row, i + 4, pDBParam->_producers[0].nAge);
	}
	pConn->FreeResult(pRes);

	_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
		pDBParam->_producers[0].nNo, pDBParam->_producers[0].tszName1, pDBParam->_producers[0].tszName2, pDBParam->_producers[0].bFlag, pDBParam->_producers[0].nAge);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_LIST_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	MySQLConnGuard pConn(CMySQLAsyncSrv::Instance()->GetAccountConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_LIST_PRODUCER_REQ: No available MySQL connection in pool."));
		return EDBReturnType::INVALID;
	}

	int nPageSize = 1000;
	int nLastNo = 0;			// 처음에 0부터 시작하여 전체를 순회
	int nTotalIndex = 0;		// 전체 데이터를 담을 누적 인덱스 (구조체 크기 한도 내에서 관리 필요)

	while( true )
	{
		char szQuery[256];
		sprintf_s(szQuery, sizeof(szQuery),
			"SELECT * FROM Producer WHERE No > %d ORDER BY No ASC LIMIT %d",
			nLastNo, nPageSize);

		MYSQL_RES* pRes = nullptr;
		pConn->Query(szQuery, pRes);

		int fetchCount = 0;
		while( MYSQL_ROW Row = mysql_fetch_row(pRes) )
		{
			int nBufSize1 = _countof(pDBParam->_producers[nTotalIndex].tszName1);
			int nBufSize2 = _countof(pDBParam->_producers[nTotalIndex].tszName2);

			// 전체 누적 배열 크기 초과 방지 (예: MAX_TOTAL_CONSUMER 등과 비교 필요)
			// if (nTotalIndex >= MAX_TOTAL_CONSUMER) break;

			int i = 0;
			pConn->GetData(Row, i, pDBParam->_producers[nTotalIndex].nNo);
			pConn->GetData(Row, i + 1, pDBParam->_producers[nTotalIndex].tszName1, nBufSize1);
			pConn->GetData(Row, i + 2, pDBParam->_producers[nTotalIndex].tszName2, nBufSize2);
			pConn->GetData(Row, i + 3, pDBParam->_producers[nTotalIndex].bFlag);
			pConn->GetData(Row, i + 4, pDBParam->_producers[nTotalIndex].nAge);

			_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
				pDBParam->_producers[nTotalIndex].nNo, pDBParam->_producers[nTotalIndex].tszName1, pDBParam->_producers[nTotalIndex].tszName2, pDBParam->_producers[nTotalIndex].bFlag, pDBParam->_producers[nTotalIndex].nAge);

			// 다음 루프를 위해 이번에 읽은 마지막 No를 갱신
			nLastNo = pDBParam->_producers[nTotalIndex].nNo;

			nTotalIndex++;
			fetchCount++;
		}
		pConn->FreeResult(pRes);

		// 이번 조회에서 가져온 데이터가 0개이거나, 요청한 1,000개보다 적게 들어왔다면 마지막 페이지이므로 탈출
		if( fetchCount < nPageSize )
		{
			break;
		}
	}

	// 총 가져온 데이터 개수 설정
	int nTotalCount = nTotalIndex;

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	MySQLConnGuard pConn(CMySQLAsyncSrv::Instance()->GetAccountConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_PRODUCER_REQ: No available MySQL connection in pool."));
		return EDBReturnType::INVALID;
	}

	// 구조체에 담겨 넘어온 실제 데이터 총 개수를 사용합니다.
	size_t rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	const size_t batchCount = 1000;

	// 최대 배치 크기에 대한 고정 쿼리를 미리 만들어 둡니다 (메모리 재할당 방지)
	static std::string s_fullBatchQuery;
	if( s_fullBatchQuery.empty() )
	{
		std::ostringstream oss;
		oss << "INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES ";
		for( size_t j = 0; j < batchCount; j++ )
		{
			if( j > 0 ) oss << ", ";
			oss << "(?, ?, ?, ?)";
		}
		s_fullBatchQuery = oss.str();
	}

	pConn->AutoCommit(false);

	size_t processedRowsInThisTask = 0;

	for( size_t i = 0; i < rowCount; i += batchCount )
	{
		size_t currentBatchSize = (batchCount < (rowCount - i)) ? batchCount : (rowCount - i);

		// 마지막 배치가 완전한 1000개가 아닐 때만 동적으로 쿼리 생성, 1000개일 때는 미리 만들어둔 문자열 재사용
		std::string queryStr;
		if( currentBatchSize == batchCount )
		{
			queryStr = s_fullBatchQuery;
		}
		else
		{
			std::ostringstream oss;
			oss << "INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES ";
			for( size_t j = 0; j < currentBatchSize; j++ )
			{
				if( j > 0 ) oss << ", ";
				oss << "(?, ?, ?, ?)";
			}
			queryStr = oss.str();
		}

		pConn->Prepare(queryStr.c_str());

		size_t colCount = 4;
		size_t paramCount = currentBatchSize * colCount;
		CVector<MYSQL_BIND> bindParams(paramCount);

		for( size_t j = 0; j < currentBatchSize; j++ )
		{
			// 현재 배치 내의 실제 데이터 인덱스 계산
			size_t dataIndex = i + j;

			ulong ulNameLen1 = (ulong)_tcslen(pDBParam->_producers[dataIndex].tszName1);
			ulong ulNameLen2 = (ulong)_tcslen(pDBParam->_producers[dataIndex].tszName2);

			// pDBParam에서 넘겨받은 실제 데이터를 바인딩 변수에 연결합니다.
			bindParams[j * colCount] = CBaseMySQL::BindParam(pDBParam->_producers[dataIndex].tszName1, ulNameLen1);
			bindParams[j * colCount + 1] = CBaseMySQL::BindParam(pDBParam->_producers[dataIndex].tszName2, ulNameLen2);
			bindParams[j * colCount + 2] = CBaseMySQL::BindParam(pDBParam->_producers[dataIndex].bFlag);
			bindParams[j * colCount + 3] = CBaseMySQL::BindParam(pDBParam->_producers[dataIndex].nAge);
		}

		if( !pConn->PrepareBindParam(bindParams) )
		{
			pConn->Rollback(); // 에러 발생 시 롤백 추가 권장
			pConn->StmtClose();
			return EDBReturnType::INVALID;
		}

		if( !pConn->PrepareExecute() )
		{
			pConn->Rollback(); // 에러 발생 시 롤백 추가 권장
			pConn->StmtClose();
			return EDBReturnType::INVALID;
		}

		for( const MYSQL_BIND& bindParam : bindParams )
			pConn->ClearBindParam(bindParam);

		// 이 배치에서 성공적으로 처리된 개수 누적
		processedRowsInThisTask += currentBatchSize;
	}

	pConn->StmtClose();
	pConn->Commit();

	// DB 반영이 최종 완료된 시점에 전역 소비 건수 누적
	g_totalConsumedRows.fetch_add(processedRowsInThisTask);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_CONSUMER_REQ)
{
	CONSUMER_DATA_BATCH_REQ* pDBParam = (CONSUMER_DATA_BATCH_REQ*)pStAsync;

	MySQLConnGuard pConn(CMySQLAsyncSrv::Instance()->GetAccountConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_CONSUMER_REQ: No available MySQL connection in pool."));
		return EDBReturnType::INVALID;
	}

	// 구조체에 담겨 넘어온 실제 데이터 총 개수를 사용합니다.
	size_t rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	const size_t batchCount = 1000;

	// 최대 배치 크기에 대한 고정 쿼리를 미리 만들어 둡니다 (메모리 재할당 방지)
	static std::string s_fullBatchQuery;
	if( s_fullBatchQuery.empty() )
	{
		std::ostringstream oss;
		oss << "INSERT INTO Consumer(Name1, Name2, Flag, Age) VALUES ";
		for( size_t j = 0; j < batchCount; j++ )
		{
			if( j > 0 ) oss << ", ";
			oss << "(?, ?, ?, ?)";
		}
		s_fullBatchQuery = oss.str();
	}

	pConn->AutoCommit(false);

	size_t processedRowsInThisTask = 0;

	for( size_t i = 0; i < rowCount; i += batchCount )
	{
		size_t currentBatchSize = (batchCount < (rowCount - i)) ? batchCount : (rowCount - i);
		
		// 마지막 배치가 완전한 1000개가 아닐 때만 동적으로 쿼리 생성, 1000개일 때는 미리 만들어둔 문자열 재사용
		std::string queryStr;
		if (currentBatchSize == batchCount)
		{
			queryStr = s_fullBatchQuery;
		}
		else
		{
			std::ostringstream oss;
			oss << "INSERT INTO Consumer(Name1, Name2, Flag, Age) VALUES ";
			for (size_t j = 0; j < currentBatchSize; j++)
			{
				if (j > 0) oss << ", ";
				oss << "(?, ?, ?, ?)";
			}
			queryStr = oss.str();
		}

		pConn->Prepare(queryStr.c_str());

		size_t colCount = 4;
		size_t paramCount = currentBatchSize * colCount;
		CVector<MYSQL_BIND> bindParams(paramCount);

		for( size_t j = 0; j < currentBatchSize; j++ )
		{
			// 현재 배치 내의 실제 데이터 인덱스 계산
			size_t dataIndex = i + j;

			ulong ulNameLen1 = (ulong)_tcslen(pDBParam->_consumers[dataIndex].tszName1);
			ulong ulNameLen2 = (ulong)_tcslen(pDBParam->_consumers[dataIndex].tszName2);

			// pDBParam에서 넘겨받은 실제 데이터를 바인딩 변수에 연결합니다.
			bindParams[j * colCount] = CBaseMySQL::BindParam(pDBParam->_consumers[dataIndex].tszName1, ulNameLen1);
			bindParams[j * colCount + 1] = CBaseMySQL::BindParam(pDBParam->_consumers[dataIndex].tszName2, ulNameLen2);
			bindParams[j * colCount + 2] = CBaseMySQL::BindParam(pDBParam->_consumers[dataIndex].bFlag);
			bindParams[j * colCount + 3] = CBaseMySQL::BindParam(pDBParam->_consumers[dataIndex].nAge);
		}

		if( !pConn->PrepareBindParam(bindParams) )
		{
			pConn->Rollback(); // 에러 발생 시 롤백 추가 권장
			pConn->StmtClose();
			return EDBReturnType::INVALID;
		}

		if( !pConn->PrepareExecute() )
		{
			pConn->Rollback(); // 에러 발생 시 롤백 추가 권장
			pConn->StmtClose();
			return EDBReturnType::INVALID;
		}

		for( const MYSQL_BIND& bindParam : bindParams )
			pConn->ClearBindParam(bindParam);

		// 이 배치에서 성공적으로 처리된 개수 누적
		processedRowsInThisTask += currentBatchSize;
	}

	pConn->StmtClose();
	pConn->Commit();

	// DB 반영이 최종 완료된 시점에 전역 소비 건수 누적
	g_totalConsumedRows.fetch_add(processedRowsInThisTask);

	return EDBReturnType::OK;
}

