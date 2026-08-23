
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

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_ADD_PRODUCER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	if( !pOdbcConn->PrepareQuery(_T("INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES(?, ?, ?, ?);")) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	pOdbcConn->BindParamInput(pDBParam->_producers[0].tszName1, &pDBParam->_producers[0].nName1Ind);
	pOdbcConn->BindParamInput(pDBParam->_producers[0].tszName2, &pDBParam->_producers[0].nName2Ind);
	pOdbcConn->BindParamInput(pDBParam->_producers[0].bFlag);
	pOdbcConn->BindParamInput(pDBParam->_producers[0].nAge);

	if( !pOdbcConn->Execute() )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	pOdbcConn->ClearStmt();

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_GET_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_GET_PRODUCER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	if( !pOdbcConn->ExecDirect(_T("SELECT * FROM Producer WHERE No = 12")) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	int32	iName1Size = _countof(pDBParam->_producers[0].tszName1);
	int32	iName2Size = _countof(pDBParam->_producers[0].tszName2);

	pOdbcConn->BindCol(pDBParam->_producers[0].nNo);
	pOdbcConn->BindCol(pDBParam->_producers[0].tszName1, iName1Size);
	pOdbcConn->BindCol(pDBParam->_producers[0].tszName2, iName2Size);
	pOdbcConn->BindCol(pDBParam->_producers[0].bFlag);
	pOdbcConn->BindCol(pDBParam->_producers[0].nAge);

	if( !pOdbcConn->Fetch() )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::FETCH_NOT_FIND;
	}

	pOdbcConn->ClearStmt();

	_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
		pDBParam->_producers[0].nNo, pDBParam->_producers[0].tszName1, pDBParam->_producers[0].tszName2, pDBParam->_producers[0].bFlag, pDBParam->_producers[0].nAge);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_LIST_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	_tstring query = _T("");

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_LIST_PRODUCER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	// 1. 전체 데이터를 가져오는 쿼리 실행 (단건 페치 시 COUNT(*) 멀티 쿼리 불필요)
	query = _T("SELECT No, Name1, Name2, Flag, Age FROM Producer LIMIT ") + to_tstring(MAX_ROWS);
	if( !pOdbcConn->ExecDirect(query.c_str()) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	// 2. 단건(1행) 단위로 컬럼 바인딩 설정
	// * 주의: 매 루프마다 바인딩이 유지되거나 덮어씌워지므로, 첫 번째 인덱스 기준으로 한 번만 바인딩합니다.
	int32	iName1Size = _countof(pDBParam->_producers[0].tszName1);
	int32	iName2Size = _countof(pDBParam->_producers[0].tszName2);

	pOdbcConn->BindCol(pDBParam->_producers[0].nNo);
	pOdbcConn->BindCol(pDBParam->_producers[0].tszName1, iName1Size);
	pOdbcConn->BindCol(pDBParam->_producers[0].tszName2, iName2Size);
	pOdbcConn->BindCol(pDBParam->_producers[0].bFlag);
	pOdbcConn->BindCol(pDBParam->_producers[0].nAge);

	int32 nTotalIndex = 0;

	// 3. 한 레코드씩 순차적으로 가져오기
	while( pOdbcConn->Fetch() )
	{
		iName1Size = _countof(pDBParam->_producers[nTotalIndex].tszName1);
		iName2Size = _countof(pDBParam->_producers[nTotalIndex].tszName2);

		// 구조체 배열 크기 초과 방지 (필요 시 방어 코드 추가)
		// if (nTotalIndex >= MAX_PRODUCER_COUNT) break;

		// 다음 레코드를 읽을 때 데이터가 제 위치에 쌓이도록 인덱스 이동 처리
		// (만약 BindCol이 매번 현재 구조체 포인터를 참조하는 방식이 아니라면, 
		//  루프 내에서 매번 바인딩을 재설정하거나 포인터를 옮겨야 할 수 있습니다.)
		_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
			pDBParam->_producers[nTotalIndex].nNo, pDBParam->_producers[nTotalIndex].tszName1, pDBParam->_producers[nTotalIndex].tszName2, pDBParam->_producers[nTotalIndex].bFlag, pDBParam->_producers[nTotalIndex].nAge);

		nTotalIndex++;

		if( nTotalIndex < MAX_ROWS ) // 적절한 최대 크기 제한
		{
			// 다음 데이터를 받을 수 있도록 바인딩 위치를 다음 구조체로 갱신
			pOdbcConn->UnBindColStmt();
			pOdbcConn->BindCol(pDBParam->_producers[nTotalIndex].nNo);
			pOdbcConn->BindCol(pDBParam->_producers[nTotalIndex].tszName1, iName1Size);
			pOdbcConn->BindCol(pDBParam->_producers[nTotalIndex].tszName2, iName2Size);
			pOdbcConn->BindCol(pDBParam->_producers[nTotalIndex].bFlag);
			pOdbcConn->BindCol(pDBParam->_producers[nTotalIndex].nAge);
		}
	}

	pOdbcConn->ClearStmt();

	if( nTotalIndex == 0 )
		return EDBReturnType::FETCH_NOT_FIND;

	// 총 가져온 데이터 개수 설정
	pDBParam->_dataCount = nTotalIndex;

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_LIST_ALLSETS_PRODUCER_REQ)
{
	_tstring query = _T("");
	int32 iCount = 0;

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_LIST_PRODUCER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	query = _T("SELECT COUNT(*) AS `count` FROM Producer;");
	query = query + _T("\r\n") + _T("SELECT * FROM Producer LIMIT ") + to_tstring(MAX_ROWS) + _T(";");

	if( pOdbcConn->ExecDirect(query.c_str()) == false )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	pOdbcConn->BindCol(iCount);

	if( pOdbcConn->Fetch() == false )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::FETCH_NOT_FIND;
	}

	if( iCount == 0 )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::FETCH_NOT_FIND;
	}

	if( iCount > MAX_ROWS ) iCount = MAX_ROWS;

	if( pOdbcConn->MoreResults() != SQL_SUCCESS )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	std::unique_ptr<PRODUCER_DATA[]> pProducerData = unique_ptr<PRODUCER_DATA[]>(new PRODUCER_DATA[iCount]);

	pOdbcConn->UnBindColStmt();

	// sizeof(PRODUCER_DATA_BATCH_REQ) : 구조체 전체 크기. ODBC의 SQL_ATTR_ROW_BIND_TYPE 속성을 설정하는 부분.
	// iCount : 가져올 데이터 총 개수. ODBC의 SQL_ATTR_ROW_ARRAY_SIZE 속성을 설정하는 부분.
	pOdbcConn->AllSets(sizeof(PRODUCER_DATA), iCount);

	int32	iName1Size = _countof(pProducerData[0].tszName1);
	int32	iName2Size = _countof(pProducerData[0].tszName2);

	pOdbcConn->BindCol(pProducerData[0].nNo);
	pOdbcConn->BindCol(pProducerData[0].tszName1, iName1Size);
	pOdbcConn->BindCol(pProducerData[0].tszName2, iName2Size);
	pOdbcConn->BindCol(pProducerData[0].bFlag);
	pOdbcConn->BindCol(pProducerData[0].nAge);

	if( !pOdbcConn->Fetch() )
	{
		pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_BIND_TYPE, SQL_BIND_BY_COLUMN, 0);
		pOdbcConn->ClearStmt();
		return EDBReturnType::FETCH_NOT_FIND;
	}

	pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_BIND_TYPE, SQL_BIND_BY_COLUMN, 0);
	pOdbcConn->ClearStmt();

	for( int i = 0; i < iCount; i++ )
	{
		_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
			pProducerData[i].nNo, pProducerData[i].tszName1, pProducerData[i].tszName2, pProducerData[i].bFlag, pProducerData[i].nAge);
	}

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_PRODUCER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	const uint64 rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	// 자동 커밋 모드 Off 설정
	pOdbcConn->SetAutoCommitMode((SQLPOINTER)SQL_AUTOCOMMIT_OFF);

	pOdbcConn->InitStmtHandle();

	// 사용할 커서의 유형을 지정(동적 커서를 활성화)
	pOdbcConn->SetStmtAttr(SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_DYNAMIC, 0);

	// 커서의 동시성(concurrency) 제어 방식을 설정(행 버전 기반 동시성 제어)
	pOdbcConn->SetStmtAttr(SQL_ATTR_CONCURRENCY, (SQLPOINTER)SQL_CONCUR_ROWVER, 0);

	// 행 단위 바인딩은 값을 결과 열이 바인딩될 버퍼의 길이 또는 버퍼의 길이로 설정
	pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_BIND_TYPE, (SQLPOINTER)sizeof(PRODUCER_DATA), SQL_IS_UINTEGER);

	// 대량 작업 행 크기 설정(한 번에 삽입할 행 수)
	pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE, reinterpret_cast<SQLPOINTER>(rowCount), 0);

	if( !pOdbcConn->ExecDirect(_T("SELECT Name1, Name2, Flag, Age FROM Producer WHERE 1=0")) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	int32 iName1Size = _countof(pDBParam->_producers[0].tszName1);		// tszName1 문자열 배열 버퍼 자체의 최대 크기(Capacity)
	int32 iName2Size = _countof(pDBParam->_producers[0].tszName2);		// tszName2 문자열 배열 버퍼 자체의 최대 크기(Capacity)

	pOdbcConn->BindCol(pDBParam->_producers[0].tszName1, iName1Size, &pDBParam->_producers[0].nName1Ind);
	pOdbcConn->BindCol(pDBParam->_producers[0].tszName2, iName2Size, &pDBParam->_producers[0].nName2Ind);
	pOdbcConn->BindCol(pDBParam->_producers[0].bFlag);
	pOdbcConn->BindCol(pDBParam->_producers[0].nAge);

	if( !pOdbcConn->BulkOperations(SQL_ADD) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	pOdbcConn->Commit();

	// 자동 커밋 모드 On 설정
	pOdbcConn->SetAutoCommitMode((SQLPOINTER)SQL_AUTOCOMMIT_ON);

	pOdbcConn->ClearStmt();

	// DB 반영이 최종 완료된 시점에 전역 소비 건수 누적
	g_totalConsumedRows.fetch_add(rowCount);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_CONSUMER_REQ)
{
	CONSUMER_DATA_BATCH_REQ* pDBParam = (CONSUMER_DATA_BATCH_REQ*)pStAsync;

	OdbcConnGuard pOdbcConn(COdbcAsyncSrv::Instance()->GetAccountOdbcConnPool());
	if( pOdbcConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_CONSUMER_REQ: No available ODBC connection in pool."));
		return EDBReturnType::INVALID;
	}

	const uint64 rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	// 자동 커밋 모드 Off 설정
	pOdbcConn->SetAutoCommitMode((SQLPOINTER)SQL_AUTOCOMMIT_OFF);

	pOdbcConn->InitStmtHandle();

	// 사용할 커서의 유형을 지정(동적 커서를 활성화)
	pOdbcConn->SetStmtAttr(SQL_ATTR_CURSOR_TYPE, (SQLPOINTER)SQL_CURSOR_DYNAMIC, 0);

	// 커서의 동시성(concurrency) 제어 방식을 설정(행 버전 기반 동시성 제어)
	pOdbcConn->SetStmtAttr(SQL_ATTR_CONCURRENCY, (SQLPOINTER)SQL_CONCUR_ROWVER, 0);

	// 행 단위 바인딩은 값을 결과 열이 바인딩될 버퍼의 길이 또는 버퍼의 길이로 설정
	pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_BIND_TYPE, (SQLPOINTER)sizeof(CONSUMER_DATA), SQL_IS_UINTEGER);

	// 대량 작업 행 크기 설정(한 번에 삽입할 행 수)
	pOdbcConn->SetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE, reinterpret_cast<SQLPOINTER>(rowCount), 0);

	if( !pOdbcConn->ExecDirect(_T("SELECT Name1, Name2, Flag, Age FROM Consumer WHERE 1=0")) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	int32 iName1Size = _countof(pDBParam->_consumers[0].tszName1);		// tszName1 문자열 배열 버퍼 자체의 최대 크기(Capacity)
	int32 iName2Size = _countof(pDBParam->_consumers[0].tszName2);		// tszName2 문자열 배열 버퍼 자체의 최대 크기(Capacity)

	pOdbcConn->BindCol(pDBParam->_consumers[0].tszName1, iName1Size, &pDBParam->_consumers[0].nName1Ind);
	pOdbcConn->BindCol(pDBParam->_consumers[0].tszName2, iName2Size, &pDBParam->_consumers[0].nName2Ind);
	pOdbcConn->BindCol(pDBParam->_consumers[0].bFlag);
	pOdbcConn->BindCol(pDBParam->_consumers[0].nAge);

	if( !pOdbcConn->BulkOperations(SQL_ADD) )
	{
		pOdbcConn->ClearStmt();
		return EDBReturnType::INVALID;
	}

	pOdbcConn->Commit();

	// 자동 커밋 모드 On 설정
	pOdbcConn->SetAutoCommitMode((SQLPOINTER)SQL_AUTOCOMMIT_ON);
	
	pOdbcConn->ClearStmt();

	// DB 반영이 최종 완료된 시점에 전역 소비 건수 누적
	g_totalConsumedRows.fetch_add(rowCount);

	return EDBReturnType::OK;
}

