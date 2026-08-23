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

	AdoConnGuard pConn(CAdoAsyncSrv::Instance()->GetAccountAdoConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_ADD_PRODUCER_REQ: No available ADO connection in pool."));
		return EDBReturnType::INVALID;
	}

	TCHAR szQuery[512];
	_stprintf_s(szQuery, _countof(szQuery),
		_T("INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES('%s', '%s', %d, %d)"),
		pDBParam->_producers[0].tszName1,
		pDBParam->_producers[0].tszName2,
		pDBParam->_producers[0].bFlag ? 1 : 0,
		pDBParam->_producers[0].nAge);

	if( !pConn->Execute(szQuery) )
	{
		LOG_ERROR(_T("DBASYNC_ADD_PRODUCER_REQ: Failed to execute insert query."));
		return EDBReturnType::INVALID;
	}

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_GET_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	AdoConnGuard pConn(CAdoAsyncSrv::Instance()->GetAccountAdoConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_GET_PRODUCER_REQ: No available ADO connection in pool."));
		return EDBReturnType::INVALID;
	}

	if( !pConn->Open(_T("SELECT * FROM Producer WHERE No = 13")) )
	{
		LOG_ERROR(_T("DBASYNC_GET_PRODUCER_REQ: Failed to open recordset."));
		pConn->RSClose();
		return EDBReturnType::INVALID;
	}

	int nBufSize1 = _countof(pDBParam->_producers[0].tszName1);
	int nBufSize2 = _countof(pDBParam->_producers[0].tszName2);

	if( !pConn->IsEOF() )
	{
		long nNoVal = 0;
		long nAgeVal = 0;
		TCHAR szName1[256] = { 0, };
		TCHAR szName2[256] = { 0, };
		TCHAR szFlag[16] = { 0, };

		pConn->GetFieldByIndex(0L, nNoVal);
		pConn->GetFieldByIndex(1L, szName1, nBufSize1);
		pConn->GetFieldByIndex(2L, szName2, nBufSize2);
		pConn->GetFieldByIndex(3L, szFlag, _countof(szFlag));
		pConn->GetFieldByIndex(4L, nAgeVal);

		pDBParam->_producers[0].nNo = static_cast<int32>(nNoVal);
		_tcscpy_s(pDBParam->_producers[0].tszName1, nBufSize1, szName1);
		_tcscpy_s(pDBParam->_producers[0].tszName2, nBufSize2, szName2);
		pDBParam->_producers[0].bFlag = (_ttoi(szFlag) != 0);
		pDBParam->_producers[0].nAge = static_cast<int32>(nAgeVal);
	}
	pConn->RSClose();

	_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
		pDBParam->_producers[0].nNo, pDBParam->_producers[0].tszName1, pDBParam->_producers[0].tszName2, pDBParam->_producers[0].bFlag, pDBParam->_producers[0].nAge);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_LIST_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	AdoConnGuard pConn(CAdoAsyncSrv::Instance()->GetAccountAdoConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_LIST_PRODUCER_REQ: No available ADO connection in pool."));
		return EDBReturnType::INVALID;
	}

	int nPageSize = 1000;
	int nLastNo = 0;
	int nTotalIndex = 0;

	while( true )
	{
		TCHAR szQuery[256];
		_stprintf_s(szQuery, _countof(szQuery),
			_T("SELECT No, Name1, Name2, Flag, Age FROM Producer WHERE No > %d ORDER BY No ASC LIMIT %d"),
			nLastNo, nPageSize);

		if( !pConn->Open(szQuery) )
		{
			LOG_ERROR(_T("DBASYNC_LIST_PRODUCER_REQ: Failed to open recordset."));
			pConn->RSClose();
			break;
		}

		int fetchCount = 0;
		while( !pConn->IsEOF() )
		{
			int nBufSize1 = _countof(pDBParam->_producers[nTotalIndex].tszName1);
			int nBufSize2 = _countof(pDBParam->_producers[nTotalIndex].tszName2);

			long nNoVal = 0;
			long nAgeVal = 0;
			TCHAR szName1[256] = { 0, };
			TCHAR szName2[256] = { 0, };
			TCHAR szFlag[16] = { 0, };

			pConn->GetFieldByIndex(0L, nNoVal);
			pConn->GetFieldByIndex(1L, szName1, nBufSize1);
			pConn->GetFieldByIndex(2L, szName2, nBufSize2);
			pConn->GetFieldByIndex(3L, szFlag, _countof(szFlag));
			pConn->GetFieldByIndex(4L, nAgeVal);

			pDBParam->_producers[nTotalIndex].nNo = static_cast<int32>(nNoVal);
			_tcscpy_s(pDBParam->_producers[nTotalIndex].tszName1, nBufSize1, szName1);
			_tcscpy_s(pDBParam->_producers[nTotalIndex].tszName2, nBufSize2, szName2);
			pDBParam->_producers[nTotalIndex].bFlag = (_ttoi(szFlag) != 0);
			pDBParam->_producers[nTotalIndex].nAge = static_cast<int32>(nAgeVal);

			_tprintf(_T("%d, %s, %s, %d, %d\r\n"),
				pDBParam->_producers[nTotalIndex].nNo, pDBParam->_producers[nTotalIndex].tszName1, pDBParam->_producers[nTotalIndex].tszName2, pDBParam->_producers[nTotalIndex].bFlag, pDBParam->_producers[nTotalIndex].nAge);

			nLastNo = pDBParam->_producers[nTotalIndex].nNo;

			nTotalIndex++;
			fetchCount++;

			if( !pConn->Next() ) break;
		}
		pConn->RSClose();

		if( fetchCount < nPageSize )
		{
			break;
		}
	}

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_PRODUCER_REQ)
{
	PRODUCER_DATA_BATCH_REQ* pDBParam = (PRODUCER_DATA_BATCH_REQ*)pStAsync;

	AdoConnGuard pConn(CAdoAsyncSrv::Instance()->GetAccountAdoConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_PRODUCER_REQ: No available ADO connection in pool."));
		return EDBReturnType::INVALID;
	}

	size_t rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	const size_t batchCount = 100; // ADO 환경에서 쿼리 문자열 길이를 고려해 배치 크기 조절 권장
	pConn->ConBeginTrans();

	size_t processedRowsInThisTask = 0;

	for( size_t i = 0; i < rowCount; i += batchCount )
	{
		size_t currentBatchSize = (batchCount < (rowCount - i)) ? batchCount : (rowCount - i);

		std::ostringstream oss;
		oss << "INSERT INTO Producer(Name1, Name2, Flag, Age) VALUES ";
		for( size_t j = 0; j < currentBatchSize; j++ )
		{
			size_t dataIndex = i + j;
			if( j > 0 ) oss << ", ";

			// CW2A를 사용하여 wchar_t* (TCHAR*)를 char*로 안전하게 변환 후 스트림에 결합
			oss << "('" << CW2A(pDBParam->_producers[dataIndex].tszName1)
				<< "', '" << CW2A(pDBParam->_producers[dataIndex].tszName2)
				<< "', " << (pDBParam->_producers[dataIndex].bFlag ? 1 : 0)
				<< ", " << pDBParam->_producers[dataIndex].nAge << ")";
		}

		if( !pConn->Execute(CA2T(oss.str().c_str())) )
		{
			pConn->ConRollbackTrans();
			return EDBReturnType::INVALID;
		}

		processedRowsInThisTask += currentBatchSize;
	}

	pConn->ConCommitTrans();
	g_totalConsumedRows.fetch_add(processedRowsInThisTask);

	return EDBReturnType::OK;
}

DECLARE_DBASYNC_HANDLER(DBASYNC_BULKADD_CONSUMER_REQ)
{
	CONSUMER_DATA_BATCH_REQ* pDBParam = (CONSUMER_DATA_BATCH_REQ*)pStAsync;

	AdoConnGuard pConn(CAdoAsyncSrv::Instance()->GetAccountAdoConnPool());
	if( pConn == nullptr )
	{
		LOG_ERROR(_T("DBASYNC_BULKADD_CONSUMER_REQ: No available ADO connection in pool."));
		return EDBReturnType::INVALID;
	}

	size_t rowCount = pDBParam->_dataCount;
	if( rowCount == 0 )
		return EDBReturnType::OK;

	const size_t batchCount = 100;
	pConn->ConBeginTrans();

	size_t processedRowsInThisTask = 0;

	for( size_t i = 0; i < rowCount; i += batchCount )
	{
		size_t currentBatchSize = (batchCount < (rowCount - i)) ? batchCount : (rowCount - i);

		std::ostringstream oss;
		oss << "INSERT INTO Consumer(Name1, Name2, Flag, Age) VALUES ";
		for( size_t j = 0; j < currentBatchSize; j++ )
		{
			size_t dataIndex = i + j;
			if( j > 0 ) oss << ", ";

			// CW2A를 사용하여 wchar_t* (TCHAR*)를 char*로 안전하게 변환 후 스트림에 결합
			oss << "('" << CW2A(pDBParam->_consumers[dataIndex].tszName1)
				<< "', '" << CW2A(pDBParam->_consumers[dataIndex].tszName2)
				<< "', " << (pDBParam->_consumers[dataIndex].bFlag ? 1 : 0)
				<< ", " << pDBParam->_consumers[dataIndex].nAge << ")";
		}

		if( !pConn->Execute(CA2T(oss.str().c_str())) )
		{
			pConn->ConRollbackTrans();
			return EDBReturnType::INVALID;
		}

		processedRowsInThisTask += currentBatchSize;
	}

	pConn->ConCommitTrans();
	g_totalConsumedRows.fetch_add(processedRowsInThisTask);

	return EDBReturnType::OK;
}