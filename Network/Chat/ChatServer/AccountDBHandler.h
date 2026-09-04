
//***************************************************************************
// AccountDBHandler.h : interface for the CAccountDBHandler class.
//
//***************************************************************************

#ifndef UC_ACCOUNTDBHANDLER_H
#define UC_ACCOUNTDBHANDLER_H

#include <Crypto/CryptoUtil.h>
#include <DB/DBAsyncSrv.h>
#include <DB/OdbcConnPool.h>

#include "DBSignupRequest.h"
#include "ChatSession.h"

#include <cstring>
#include <cctype>
#include <array>

//***************************************************************************
// @class CAccountDBHandler
// @brief kDbCallIdent_Signup 요청(회원가입/재접속 검증)을 처리하는 DB 핸들러.
//***************************************************************************
class CAccountDBHandler : public CDBAsyncSrvHandler
{
public:
	explicit CAccountDBHandler(COdbcConnPool* pool) : _pool(pool) {}

	//***************************************************************************
	// @brief 실행은 DB 비동기 워커 스레드에서 일어난다(블로킹 쿼리 포함).
	//        여기서 세션이나 IOCP 객체를 직접 건드리지 않고,
	//        req->onComplete(result, nickname, newToken)로 결과만 넘긴다.
	//***************************************************************************
	virtual EDBReturnType ProcessAsyncCall(st_DBAsyncRq* pStAsync) override;

private:
	COdbcConnPool* _pool = nullptr;	// GetAccountOdbcConnPool()로 얻은 회원 DB 전용 풀
};

#endif // ndef UC_ACCOUNTDBHANDLER_H
