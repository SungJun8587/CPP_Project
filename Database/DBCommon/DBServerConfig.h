
//***************************************************************************
// DBServerConfig.h : interface for the CDBServerConfig class.
//
//***************************************************************************

#ifndef UC_DBSERVERCONFIG_H
#define UC_DBSERVERCONFIG_H

#include <ServerConfig.h>
#include <Memory/Singleton.h>

//***************************************************************************
// @class CDBServerConfig
// @brief DB 서버 설정 정보 관리 클래스.
// @detail CServerConfig는 필드와 게터만 제공하고, Init()/Save()/PrintServerSettingInfo()는
//         이 클래스가 구현한다. 베이스 필드(ServiceName/DisplayName/Name/GroupId/ChannelId/
//         IP/Port/KeepAliveSec/MaxSessionCount/WorkerThreadCnt/ServerNode/DBNode/RedisNode)를
//         JSON 파일 하나에서 한 번에 읽고(CJSONConfigFieldReader), 같은 형식으로 저장한다(CJSONConfigFieldWriter).
//         키가 없으면 기본값을 유지하고, 타입/범위 오류는 로그로 남기며 Init이 false를 돌려준다.
//***************************************************************************
class CDBServerConfig : public CServerConfig, public CSingleton<CDBServerConfig>
{
public:
	CDBServerConfig() {}
	virtual ~CDBServerConfig();

	bool Init(const TCHAR* tszServerInfo);
	bool Save(const TCHAR* tszServerInfo) const;

	void PrintServerSettingInfo();
};

#endif // ndef UC_DBSERVERCONFIG_H