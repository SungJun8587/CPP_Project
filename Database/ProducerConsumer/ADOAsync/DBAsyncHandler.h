
//***************************************************************************
// DBAsyncHandler.h : interface for the command##_handler class.
//
//***************************************************************************

#ifndef __DBASYNCHANDLER_H__
#define __DBASYNCHANDLER_H__

#ifndef __DBASYNCSRV__H__
#include <DBAsyncSrv.h>
#endif

#ifndef __ADOASYNCSRV__H__
#include <AdoAsyncSrv.h>
#endif

#ifndef __DBASYNCSTRUCT_H__
#include "DBAsyncStruct.h"
#endif

// 프로토콜 생성 및 바인더에 추가
#define DECLARE_DBASYNC_HANDLER(command) \
class command##_handler : public CDBAsyncSrvHandler \
{\
public:\
	command##_handler(){} \
	virtual ~command##_handler(){} \
	virtual EDBReturnType ProcessAsyncCall(st_DBAsyncRq* pStAsync); \
	\
	static std::shared_ptr<CDBAsyncSrvHandler> asyncHandler; \
}; \
	shared_ptr<CDBAsyncSrvHandler> command##_handler::asyncHandler = CAdoAsyncSrv::Instance()->Regist(command, std::make_shared<command##_handler>()); \
	EDBReturnType command##_handler::ProcessAsyncCall(st_DBAsyncRq* pStAsync)

#endif // ndef __DBASYNCHANDLER_H__