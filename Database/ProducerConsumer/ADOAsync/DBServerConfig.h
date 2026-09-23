
//***************************************************************************
// DBServerConfig.h : interface for the CDBServerConfig class.
//
//***************************************************************************

#ifndef UC_DBSERVERCONFIG_H
#define UC_DBSERVERCONFIG_H

#include <ServerConfig.h>
#include <Memory/Singleton.h>

//***************************************************************************
// @brief DB 서버 설정 정보 관리 클래스.
// @details [수정 — 리팩터링] CServerConfig는 이제 필드+게터만 제공하고
//          (ServerConfig.h 상단 설명 참고), Init()/ToJSON()/FromJSON()/
//          PrintServerSettingInfo()는 이 클래스가 전부 직접 구현한다 —
//          베이스 필드(ServiceName/DisplayName/ServerName/GroupId/
//          ChannelId/IP/Port/KeepAliveSec/MaxSessionCount/WorkerThreadCnt/
//          ServerNodeVec/DBNodeVec/RedisNodeVec)를 같은
//          JSON 파일 하나에서 한 번에 읽는다.
//
//          이전에 "DBSERVER_CONFIG" 같은 매크로/전역 접근자로
//          CServerConfig::Instance()를 가리키던 곳이 있다면, 그 정의를
//          CDBServerConfig::Instance()로 같이 고쳐야 한다(이 리팩터링만
//          으로는 자동으로 안 바뀜 — 그 매크로가 정의된 파일을 몰라서 이
//          커밋에 포함 못 시켰다).
//***************************************************************************
class CDBServerConfig : public CServerConfig, public CSingleton<CDBServerConfig>
{
public:
	CDBServerConfig() {}
	virtual ~CDBServerConfig();

	bool Init(const TCHAR* tszServerInfo);

	void PrintServerSettingInfo();

	//***************************************************************************
	// @brief 서버 설정 객체를 JSON 형태로 직렬화합니다(베이스 필드 +
	//        이 클래스 고유 필드 전부).
	//***************************************************************************
	void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
	{
		value.SetObject();
		value.AddMember(_T("ServiceName"), _tValue(_tszServiceName, allocator), allocator);
		value.AddMember(_T("DisplayName"), _tValue(_tszDisplayName, allocator), allocator);
		value.AddMember(_T("Name"), _tValue(_tszServerName, allocator), allocator);
		value.AddMember(_T("GroupId"), _nServerGroupId, allocator);
		value.AddMember(_T("ChannelId"), _nServerChannelId, allocator);
		value.AddMember(_T("IP"), _tValue(_tszIP, allocator), allocator);
		value.AddMember(_T("Port"), _nServerPort, allocator);
		value.AddMember(_T("KeepAliveSec"), _nKeepAliveSec, allocator);
		value.AddMember(_T("MaxSessionCount"), _nMaxSessionCount, allocator);
		value.AddMember(_T("WorkerThreadCnt"), _nWorkerThreadCnt, allocator);
	}

	//***************************************************************************
	// @brief JSON 객체로부터 서버 설정 정보를 역직렬화합니다.
	//***************************************************************************
	void FromJSON(const _tValue& value)
	{
		_tcsncpy_s(_tszServiceName, _countof(_tszServiceName), value[_T("ServiceName")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszDisplayName, _countof(_tszDisplayName), value[_T("DisplayName")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszServerName, _countof(_tszServerName), value[_T("Name")].GetString(), _TRUNCATE);
		_nServerGroupId = value[_T("GroupId")].GetInt();
		_nServerChannelId = value[_T("ChannelId")].GetInt();
		_tcsncpy_s(_tszIP, _countof(_tszIP), value[_T("IP")].GetString(), _TRUNCATE);
		_nServerPort = value[_T("Port")].GetInt();
		_nKeepAliveSec = value[_T("KeepAliveSec")].GetInt();
		_nMaxSessionCount = value[_T("MaxSessionCount")].GetInt();
		_nWorkerThreadCnt = value[_T("WorkerThreadCnt")].GetInt();
	}
};

#endif // ndef UC_DBSERVERCONFIG_H