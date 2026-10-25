
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
#include <Util/EncodingConvert.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

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

	using Row = std::vector<std::string>;

	// 한 컬럼을 읽는 버퍼(문자 수). 메시지 본문(최대 kConvMessageBytes 바이트)보다 넉넉하게
	constexpr size_t kColumnChars = 1100;

	// 십진 문자열을 정수로(빈 문자열은 0).
	inline int64 ToI64(const std::string& s)
	{
		return s.empty() ? 0 : static_cast<int64>(std::strtoll(s.c_str(), nullptr, 10));
	}

	//***************************************************************************
	// @brief 한 연결 위에서 문자열 파라미터 구문을 실행하는 도우미.
	// @details 모든 파라미터는 문자열이다(정수도 십진 문자열로 바인딩 — MySQL이 비교/삽입 시
	//          변환한다). SELECT는 모든 컬럼을 문자열로 읽는다. 어떤 경로에서도 구문을
	//          정리(ClearStmt)한다.
	//***************************************************************************
	class CSql
	{
	public:
		explicit CSql(OdbcConnGuard& guard) : _guard(guard) {}

		bool Query(const TCHAR* sql, const std::vector<std::string>& params, int columns, std::vector<Row>& rows)
		{
			rows.clear();
			std::vector<_tstring> bound;
			if( !Prepare(sql, params, bound) )
				return false;

			std::vector<TCHAR> buffer(kColumnChars);
			while( _guard->Fetch() )
			{
				Row row;
				row.reserve(columns);
				for( int column = 1; column <= columns; ++column )
				{
					std::fill(buffer.begin(), buffer.end(), TCHAR());
					int32 bufferBytes = static_cast<int32>(buffer.size() * sizeof(TCHAR));
					if( _guard->GetData(column, buffer.data(), bufferBytes) )
						row.push_back(TStringToUtf8(_tstring(buffer.data())));
					else
						row.push_back(std::string());	// NULL/빈 값
				}
				rows.push_back(std::move(row));
			}

			_guard->ClearStmt();
			return true;
		}

		bool Exec(const TCHAR* sql, const std::vector<std::string>& params, int64* affected = nullptr)
		{
			std::vector<_tstring> bound;
			if( !Prepare(sql, params, bound) )
				return false;

			if( affected != nullptr )
				*affected = _guard->RowCount();
			_guard->ClearStmt();
			return true;
		}

		bool LastInsertId(int64& id)
		{
			std::vector<Row> rows;
			if( !Query(_T("SELECT LAST_INSERT_ID()"), {}, 1, rows) || rows.empty() )
				return false;
			id = ToI64(rows.front()[0]);
			return true;
		}

		bool Exists(const TCHAR* sql, const std::vector<std::string>& params, bool& exists)
		{
			std::vector<Row> rows;
			if( !Query(sql, params, 1, rows) )
				return false;
			exists = !rows.empty();
			return true;
		}

	private:
		// 준비 + 바인딩 + 실행까지. 실패하면 구문을 정리하고 false.
		bool Prepare(const TCHAR* sql, const std::vector<std::string>& params, std::vector<_tstring>& bound)
		{
			bound.reserve(params.size());	// 바인딩한 문자열의 주소가 Execute()까지 변하면 안 된다
			for( const std::string& param : params )
				bound.push_back(Utf8ToTString(param));

			if( !DbUtil::PrepareOrClear(_guard, sql) )
				return false;
			for( size_t i = 0; i < bound.size(); ++i )
				DbUtil::BindString(_guard, static_cast<int32>(i + 1), bound[i]);
			return DbUtil::ExecuteOrClear(_guard);
		}

	private:
		OdbcConnGuard& _guard;
	};
}

#endif // ndef UC_DBHANDLERUTIL_H