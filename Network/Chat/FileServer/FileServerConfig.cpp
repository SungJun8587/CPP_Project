
//***************************************************************************
// FileServerConfig.cpp: implementation of the CFileServerConfig class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerConfig.h"

//***************************************************************************
// @brief CFileServerConfig 클래스의 생성자
//***************************************************************************
CFileServerConfig::CFileServerConfig()
	: _nServerGroupId(0), _nServerChannelId(0), _nServerPort(0), _nMaxSessionCount(0), _nWorkerThreadCnt(0)
	, _nRedisPoolSize(0), _nMaxUploadBytes(0)
{
	memset(_tszServiceName, 0, sizeof(_tszServiceName));
	memset(_tszDisplayName, 0, sizeof(_tszDisplayName));
	memset(_tszServerName, 0, sizeof(_tszServerName));
	memset(_tszIP, 0, sizeof(_tszIP));
	memset(_tszStorageDir, 0, sizeof(_tszStorageDir));
	memset(_tszPublicBaseUrl, 0, sizeof(_tszPublicBaseUrl));

	Clear();
}

//***************************************************************************
// @brief CFileServerConfig 클래스의 소멸자
//***************************************************************************
CFileServerConfig::~CFileServerConfig()
{
	Clear();
}

//***************************************************************************
// @brief JSON 설정 파일로부터 파일 서버 구성 정보를 읽어와 초기화합니다.
//***************************************************************************
bool CFileServerConfig::Init(const TCHAR* tszServerInfo)
{
	CRapidJSONUtil jsonUtil;
	jsonUtil.LoadFromFile(tszServerInfo);

	_tcsncpy_s(_tszServiceName, _countof(_tszServiceName), jsonUtil[_T("ServiceName")], _TRUNCATE);
	_tcsncpy_s(_tszDisplayName, _countof(_tszDisplayName), jsonUtil[_T("DisplayName")], _TRUNCATE);
	_tcsncpy_s(_tszServerName, _countof(_tszServerName), jsonUtil[_T("Name")], _TRUNCATE);
	_nServerGroupId = jsonUtil[_T("GroupId")];
	_nServerChannelId = jsonUtil[_T("ChannelId")];

	_tcsncpy_s(_tszIP, _countof(_tszIP), jsonUtil[_T("IP")], _TRUNCATE);
	_nServerPort = jsonUtil[_T("Port")];
	_nMaxSessionCount = jsonUtil[_T("MaxSessionCount")];
	_nWorkerThreadCnt = jsonUtil[_T("WorkerThreadCnt")];

	_nRedisPoolSize = jsonUtil[_T("RedisPoolSize")];

	_tcsncpy_s(_tszStorageDir, _countof(_tszStorageDir), jsonUtil[_T("StorageDir")], _TRUNCATE);
	_tcsncpy_s(_tszPublicBaseUrl, _countof(_tszPublicBaseUrl), jsonUtil[_T("PublicBaseUrl")], _TRUNCATE);
	_nMaxUploadBytes = jsonUtil[_T("MaxUploadBytes")];

	if( _nMaxUploadBytes <= 0 )
	{
		LOG_ERROR(_T("CFileServerConfig::Init: invalid MaxUploadBytes(%d) — must be > 0"), _nMaxUploadBytes);
		return false;
	}

	_redisNodeVec = jsonUtil.Deserialize<CVector<CRedisNode>>(_T("RedisNode"));

	return true;
}

//***************************************************************************
// @brief 내부 동적 컨테이너 데이터를 소거하여 초기화합니다.
//***************************************************************************
void CFileServerConfig::Clear()
{
	_redisNodeVec.clear();
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CFileServerConfig::PrintServerSettingInfo()
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : File Server Setting Info] ---------------"));
	LOG_INFO(_T("ServiceName : %s"), _tszServiceName);
	LOG_INFO(_T("DisplayName : %s"), _tszDisplayName);
	LOG_INFO(_T("ServerName : %s"), _tszServerName);
	LOG_INFO(_T("ServerGroupId : %d"), _nServerGroupId);
	LOG_INFO(_T("ServerChannelId : %d"), _nServerChannelId);
	LOG_INFO(_T("IP : %s"), _tszIP);
	LOG_INFO(_T("Port : %d"), _nServerPort);
	LOG_INFO(_T("MaxSessionCount : %d"), _nMaxSessionCount);
	LOG_INFO(_T("WorkerThreadCnt : %d"), _nWorkerThreadCnt);
	LOG_INFO(_T("RedisPoolSize : %d"), _nRedisPoolSize);
	LOG_INFO(_T("StorageDir : %s"), _tszStorageDir);
	LOG_INFO(_T("PublicBaseUrl : %s"), _tszPublicBaseUrl);
	LOG_INFO(_T("MaxUploadBytes : %d"), _nMaxUploadBytes);

	LOG_INFO(_T("--------------- Connect RedisNode size : %d ---------------"), static_cast<int>(_redisNodeVec.size()));
	for( uint32 i = 0; i < _redisNodeVec.size(); i++ )
	{
		LOG_INFO(_T("ID : %d"), _redisNodeVec[i]._nID);
		LOG_INFO(_T("DBHost : %s"), _redisNodeVec[i]._tszDBHost);
		LOG_INFO(_T("Port : %d"), _redisNodeVec[i]._nPort);
		LOG_INFO(_T("DBUserId : %s"), _redisNodeVec[i]._tszDBUserId);
		LOG_INFO(_T("DBPasswd : %s"), _redisNodeVec[i]._tszDBPasswd);
		LOG_INFO(_T("DBIndex : %d"), _redisNodeVec[i]._nDbIndex);
		LOG_INFO(_T("------------------------------"));
	}

	LOG_INFO(_T("--------------- [End Print] ---------------"));
	LOG_INFO(_T("###################################################################"));
}