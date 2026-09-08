
//***************************************************************************
// DBAsyncStruct.h : Definitions of request structures and data used for DB asynchronous operations
//
// [트레이드오프] 이전에는 이 구조체 전체(요청 객체 + 내부 배열)가
// st_DBAsyncRq가 상속하는 BaseAllocator의 커스텀 operator new 한 번으로
// 통째로 할당됐다. vector로 바꾸면 요청 객체 자체는 여전히 BaseAllocator
// 경로로 할당되지만, vector의 원소 버퍼는 별도로 기본 힙(new[])에서
// 할당된다 — 요청 1건당 힙 할당이 1회에서 2회로 늘어난다. "매번 1000개
// zero-fill"의 낭비를 없애는 대신 "요청마다 별도 할당 1회"라는 비용을
// 새로 지불하는 셈이다. 초당 요청 수가 극단적으로 많고 이 두 번째
// 할당 자체가 병목이 되는 경우라면, 이 트레이드오프를 재검토할 필요가
// 있다.
//***************************************************************************

#ifndef __DBASYNCSTRUCT_H__
#define __DBASYNCSTRUCT_H__

#pragma once

#include <vector>

#ifndef __DBASYNCSRV__H__
#include <DB/DBAsyncSrv.h>
#endif

#define MAX_ROWS		10000	// 
#define BATCH_SIZE		1000	// 
#define INTERVAL_SEC	5		// 생산자 유휴 주기(초)

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
	// [수정] 기본 1개 슬롯을 미리 확보해 둔다 — DBASYNC_ADD_PRODUCER_REQ/
	// DBASYNC_GET_PRODUCER_REQ처럼 단건 조회 계열 핸들러가 별도 resize
	// 없이도 곧바로 _producers[0]에 안전하게 접근할 수 있도록 하기
	// 위함이다. 여러 건을 다루는 핸들러(BULKADD/LIST 등)는 실제로 쓰기
	// 전에 필요한 크기로 resize()를 다시 호출한다.
	_PRODUCER_DATA_BATCH_REQ()
		: _producers(1)
	{
		_dataCount = 0;
		_lastNo = 0;
	}

	std::vector<PRODUCER_DATA>	_producers;	// [수정] 고정 배열(PRODUCER_DATA[BATCH_SIZE]) 대신 벡터
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
	// [수정] PRODUCER_DATA_BATCH_REQ와 동일한 이유로 기본 1개 슬롯 확보.
	_CONSUMER_DATA_BATCH_REQ()
		: _consumers(1)
	{
		_dataCount = 0;
	}

	std::vector<CONSUMER_DATA>	_consumers;	// [수정] 고정 배열(CONSUMER_DATA[BATCH_SIZE]) 대신 벡터
	int				_dataCount;

} CONSUMER_DATA_BATCH_REQ;

#endif // ndef __DBASYNCSTRUCT_H__