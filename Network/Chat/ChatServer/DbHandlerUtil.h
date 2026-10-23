
//***************************************************************************
// DbHandlerUtil.h : DBHandler/*.cpp에서 반복되는 ODBC 호출 보일러플레이트 헬퍼
//
// DB 핸들러는 "쿼리 준비 -> 파라미터 바인딩 -> 실행" 단계마다 실패 시
// ClearStmt()로 구문을 정리한 뒤 빠져나오는데, 그 두 줄을 한 곳으로 모은다.
// DB 핸들러 .cpp에서 DbServiceManager.h 뒤에 include 한다.
//***************************************************************************

#ifndef UC_DBHANDLERUTIL_H
#define UC_DBHANDLERUTIL_H

#include <DB/OdbcConnPool.h>

namespace DbUtil
{
	//***************************************************************************
	// @brief 쿼리를 준비하고, 실패하면 구문을 정리한다.
	// @details 실패 시 호출자는 ClearStmt()를 다시 부를 필요가 없다.
	// @param guard 커넥션 가드
	// @param ptszQuery 준비할 SQL 문
	// @return 준비 성공 여부
	//***************************************************************************
	inline bool PrepareOrClear(OdbcConnGuard& guard, const TCHAR* ptszQuery)
	{
		if( guard->PrepareQuery(ptszQuery) )
			return true;

		guard->ClearStmt();
		return false;
	}

	//***************************************************************************
	// @brief 준비된 쿼리를 실행하고, 실패하면 구문을 정리한다.
	// @details 실패 시 호출자는 ClearStmt()를 다시 부를 필요가 없다.
	// @param guard 커넥션 가드
	// @return 실행 성공 여부
	//***************************************************************************
	inline bool ExecuteOrClear(OdbcConnGuard& guard)
	{
		if( guard->Execute() )
			return true;

		guard->ClearStmt();
		return false;
	}

	//***************************************************************************
	// @brief NUL 종료 문자열 입력 파라미터를 바인딩한다.
	// @details CBaseODBC::BindParamInput은 길이 지표(SQLLEN)를 SQL_NTS로 쓰고 그
	//          주소를 Execute() 때까지 드라이버에 맡기므로 지표가 Execute() 이후까지
	//          살아있어야 한다. 입력 파라미터의 지표는 항상 SQL_NTS 하나이고
	//          드라이버가 쓰지 않으므로, DB 워커 스레드별 공용 지표 하나를 쓴다.
	//          value 문자열은 Execute()가 끝날 때까지 호출자가 유지해야 한다.
	// @param guard 커넥션 가드
	// @param iParamIndex 1부터 시작하는 파라미터 번호
	// @param value 바인딩할 문자열(Execute() 때까지 살아있어야 함)
	// @return 바인딩 성공 여부
	//***************************************************************************
	inline bool BindString(OdbcConnGuard& guard, int32 iParamIndex, const _tstring& value)
	{
		thread_local SQLLEN ntsIndicator = SQL_NTS;
		return guard->BindParamInput(iParamIndex, value.c_str(), ntsIndicator);
	}

	//***************************************************************************
	// @brief NUL 종료 문자열 버퍼를 입력 파라미터로 바인딩한다.
	// @param guard 커넥션 가드
	// @param iParamIndex 1부터 시작하는 파라미터 번호
	// @param ptszValue 바인딩할 버퍼(Execute() 때까지 살아있어야 함)
	// @return 바인딩 성공 여부
	//***************************************************************************
	inline bool BindString(OdbcConnGuard& guard, int32 iParamIndex, const TCHAR* ptszValue)
	{
		thread_local SQLLEN ntsIndicator = SQL_NTS;
		return guard->BindParamInput(iParamIndex, ptszValue, ntsIndicator);
	}
}

#endif // ndef UC_DBHANDLERUTIL_H