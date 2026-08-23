// DBSynchronization.cpp : 이 파일에는 'main' 함수가 포함됩니다. 거기서 프로그램 실행이 시작되고 종료됩니다.
//

#include "pch.h"
#include <iostream>
#include "TestFunction.h"

// 금지 문자 필터링 함수 (TCHAR 버전)
std::basic_string<TCHAR> RemoveInvalidXmlChars(const std::basic_string<TCHAR>& input) {
	std::basic_string<TCHAR> output;
	for( TCHAR c : input ) {
		// 유효한 XML 문자 범위
		if( (c >= 0x20 && c <= 0xD7FF) ||  // 기본 유니코드 범위
			(c >= 0xE000 && c <= 0xFFFD) || // 추가 유니코드 범위
			c == 0x09 || c == 0x0A || c == 0x0D ) { // 공백 및 개행
			output += c;
		}
	}
	return output;
}


int main()
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
	 
	// 한글 콘솔 출력 설정
	_tsetlocale(LC_ALL, _T("Korean"));

	TCHAR tszTempArgv[FULLPATH_STRLEN] = { 0, };

	BaseGlobal::Init();

	_sntprintf_s(tszTempArgv, FULLPATH_STRLEN, _TRUNCATE, _T("config\\server_config.json"));

	if( false == SERVER_CONFIG->Init(tszTempArgv) )
	{
		LOG_ERROR(_T("SERVER_CONFIG->Init Fail."));
		SERVER_CONFIG->ReleaseInstance();
		return -1;
	}

	TCHAR tszServerName[DATABASE_BUFFER_SIZE];
	TCHAR tszDBMSName[DATABASE_BUFFER_SIZE];
	TCHAR tszDBMSVersion[DATABASE_BUFFER_SIZE];

	CVector<CDBNode> dbNodes = SERVER_CONFIG->GetDBNodeVec();

	//EDBClass dbClass = EDBClass::MSSQL;
	EDBClass dbClass = EDBClass::MYSQL;
	//EDBClass dbClass = EDBClass::ORACLE;
	
	auto findDBNode = std::find_if(dbNodes.begin(), dbNodes.end(), [=](const CDBNode& dbNode) { return dbNode._dbClass == dbClass; });
	
	CBaseODBC BaseODBC(findDBNode->_dbClass, findDBNode->_tszDSN);
	BaseODBC.Connect();
	BaseODBC.InitStmtHandle();

	BaseODBC.GetServerName(tszServerName, _countof(tszServerName));
	BaseODBC.GetDBMSName(tszDBMSName, _countof(tszDBMSName));
	BaseODBC.GetDBMSVersion(tszDBMSVersion, _countof(tszDBMSVersion));

	CDBSchema dbSchema(BaseODBC);
	dbSchema.GatherDBSchema();
	//dbSchema.PrintDBSchema();

	//CDBSchemaToExcel dbSchemaToExcel(dbSchema.GetDBModelTable());
	//dbSchemaToExcel.SaveExcelFile(findDBNode->_dbClass, _tstring(findDBNode->_tszDBName) + _tstring(_T(".xlsx")));

	CDBSchemaToXML dbSchemaToXML(dbSchema.GetDBModelTable(), dbSchema.GetDBModelProcedure(), dbSchema.GetDBModelFunction());
	dbSchemaToXML.DBToCreateXml(_T("K:\\GitHub\\CPP\\DBSynchronization\\GameDB2.xml"));

	/*
	CDBSchemaToSync dbSchemaToSync(BaseODBC, dbSchema.GetDBModelTable(), dbSchema.GetDBModelProcedure(), dbSchema.GetDBModelFunction(),
		dbSchemaToXML.GetXMLTable(), dbSchemaToXML.GetXMLProcedure(), dbSchemaToXML.GetXMLFunction(), dbSchemaToXML.GetXMLRemovedTable());
	dbSchemaToSync.Synchronize();
	*/

	//CDBQueryProcess dbProcess(BaseODBC);
	//TestDBInfo(dbProcess);
	//TestMSSQLTableIndexFragmentationCheck(dbProcess);
	//TestMSSQLIndexOptionProcess(dbProcess);
	//TestMYSQLCharacterSetCollationEngine(dbProcess);
	//TestMYSQLTableFragmentationCheck(dbProcess);
	//TestRenameObject(dbProcess);
	//TestORACLEIndexFragmentationCheck(dbProcess);

	SERVER_CONFIG->ReleaseInstance();

	BaseGlobal::Destroy();

	system("pause");

	return 0;
}

// 프로그램 실행: <Ctrl+F5> 또는 [디버그] > [디버깅하지 않고 시작] 메뉴
// 프로그램 디버그: <F5> 키 또는 [디버그] > [디버깅 시작] 메뉴

// 시작을 위한 팁: 
//   1. [솔루션 탐색기] 창을 사용하여 파일을 추가/관리합니다.
//   2. [팀 탐색기] 창을 사용하여 소스 제어에 연결합니다.
//   3. [출력] 창을 사용하여 빌드 출력 및 기타 메시지를 확인합니다.
//   4. [오류 목록] 창을 사용하여 오류를 봅니다.
//   5. [프로젝트] > [새 항목 추가]로 이동하여 새 코드 파일을 만들거나, [프로젝트] > [기존 항목 추가]로 이동하여 기존 코드 파일을 프로젝트에 추가합니다.
//   6. 나중에 이 프로젝트를 다시 열려면 [파일] > [열기] > [프로젝트]로 이동하고 .sln 파일을 선택합니다.
