
//***************************************************************************
// DBAsyncStruct.h : Definitions of request structures and data used for DB asynchronous operations
//
//***************************************************************************

#ifndef __DBASYNCSTRUCT_H__
#define __DBASYNCSTRUCT_H__

#pragma once

#pragma pack(push, 1)

#ifndef __DBASYNCSRV__H__
#include <DB/DBAsyncSrv.h>
#endif

#define MAX_ROWS		10000	// 
#define BATCH_SIZE		1000	// 
#define INTERVAL_SEC	5		// 데이터 생산 주기(초)

typedef struct _PRODUCER_DATA
{
	_PRODUCER_DATA()
	{
		nNo = 0;
		memset(tszName1, 0, sizeof(tszName1));
		memset(tszName2, 0, sizeof(tszName2));
		bFlag = false;
		nAge = 0;
	}

	uint32	nNo;
	TCHAR	tszName1[50];
	SQLLEN  nName1Ind;
	TCHAR	tszName2[50];
	SQLLEN  nName2Ind;
	bool	bFlag;
	int32   nAge;

} PRODUCER_DATA;

#define DBASYNC_ADD_PRODUCER_REQ 1
#define DBASYNC_GET_PRODUCER_REQ 2
#define DBASYNC_LIST_PRODUCER_REQ 3
#define DBASYNC_BULKADD_PRODUCER_REQ 4
typedef struct _PRODUCER_DATA_BATCH_REQ : public st_DBAsyncRq
{
	_PRODUCER_DATA_BATCH_REQ()
	{
		memset(_producers, 0, sizeof(_producers));
		_dataCount = 0;
		_lastNo = 0;
	}

	PRODUCER_DATA	_producers[BATCH_SIZE];
	int				_dataCount;
	uint32			_lastNo;

} PRODUCER_DATA_BATCH_REQ;


typedef struct _CONSUMER_DATA
{
	_CONSUMER_DATA()
	{
		nNo = 0;
		memset(tszName1, 0, sizeof(tszName1));
		memset(tszName2, 0, sizeof(tszName2));
		bFlag = false;
		nAge = 0;
	}

	uint32	nNo;
	TCHAR	tszName1[50];
	SQLLEN  nName1Ind;
	TCHAR	tszName2[50];
	SQLLEN  nName2Ind;
	bool	bFlag;
	int32   nAge;

} CONSUMER_DATA;

#define DBASYNC_BULKADD_CONSUMER_REQ 11
typedef struct _CONSUMER_DATA_BATCH_REQ : public st_DBAsyncRq
{
	_CONSUMER_DATA_BATCH_REQ()
	{
		memset(_consumers, 0, sizeof(_consumers));
		_dataCount = 0;
	}

	CONSUMER_DATA	_consumers[BATCH_SIZE];
	int				_dataCount;

} CONSUMER_DATA_BATCH_REQ;

#pragma pack(pop)

#endif // ndef __DBASYNCSTRUCT_H__
