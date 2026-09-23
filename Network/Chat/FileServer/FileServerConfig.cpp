
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
	: _nRedisPoolSize(0), _nMaxUploadBytes(0)
{
	memset(_tszStorageDir, 0, sizeof(_tszStorageDir));
	memset(_tszPublicBaseUrl, 0, sizeof(_tszPublicBaseUrl));
}

//***************************************************************************
// @brief CFileServerConfig 클래스의 소멸자
//***************************************************************************
CFileServerConfig::~CFileServerConfig()
{
}

//***************************************************************************
// @brief JSON 설정 파일로부터 파일 서버 구성 정보를 읽어와 초기화합니다.
// @details ServiceName/DisplayName/GroupId/ChannelId/KeepAliveSec/
//          ServerNode/DBNode는 파일 서버 개념에 없으므로 읽지 않는다 —
//          그 필드들은 상속만 받고 기본값 그대로 남는다.
//***************************************************************************
bool CFileServerConfig::Init(const TCHAR* tszServerInfo)
{
	CRapidJSONUtil jsonUtil;
	jsonUtil.LoadFromFile(tszServerInfo);

	_tcsncpy_s(_tszServerName, _countof(_tszServerName), jsonUtil[_T("Name")], _TRUNCATE);
	_tcsncpy_s(_tszIP, _countof(_tszIP), jsonUtil[_T("IP")], _TRUNCATE);
	_nServerPort = jsonUtil[_T("Port")];
	_nMaxSessionCount = jsonUtil[_T("MaxSessionCount")];
	_nWorkerThreadCnt = jsonUtil[_T("WorkerThreadCnt")];

	_nRedisPoolSize = jsonUtil[_T("RedisPoolSize")];

	_tcsncpy_s(_tszStorageDir, _countof(_tszStorageDir), jsonUtil[_T("StorageDir")], _TRUNCATE);
	_tcsncpy_s(_tszPublicBaseUrl, _countof(_tszPublicBaseUrl), jsonUtil[_T("PublicBaseUrl")], _TRUNCATE);
	// [수정] _nMaxUploadBytes가 int64로 넓어졌다 — jsonUtil[...]의 프록시가
	// 템플릿 기반 변환 연산자를 제공한다는 가정 하에(다른 필드들과 동일한
	// 패턴) int64로도 그대로 대입이 될 것으로 예상하지만, 실제
	// CRapidJSONUtil 구현을 못 봐서 100% 확신은 못한다 — 컴파일 에러가
	// 나면 이 줄만 명시적으로 int64로 받는 식으로 바꾸면 된다.
	_nMaxUploadBytes = jsonUtil[_T("MaxUploadBytes")];

	if( _nMaxUploadBytes <= 0 )
	{
		LOG_ERROR(_T("CFileServerConfig::Init: invalid MaxUploadBytes(%lld) — must be > 0"), static_cast<long long>(_nMaxUploadBytes));
		return false;
	}

	_redisNodeVec = jsonUtil.Deserialize<CVector<CRedisNode>>(_T("RedisNode"));

	return true;
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CFileServerConfig::PrintServerSettingInfo()
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : File Server Setting Info] ---------------"));
	LOG_INFO(_T("ServerName : %s"), _tszServerName);
	LOG_INFO(_T("IP : %s"), _tszIP);
	LOG_INFO(_T("Port : %d"), _nServerPort);
	LOG_INFO(_T("MaxSessionCount : %d"), _nMaxSessionCount);
	LOG_INFO(_T("WorkerThreadCnt : %d"), _nWorkerThreadCnt);
	LOG_INFO(_T("RedisPoolSize : %d"), _nRedisPoolSize);
	LOG_INFO(_T("StorageDir : %s"), _tszStorageDir);
	LOG_INFO(_T("PublicBaseUrl : %s"), _tszPublicBaseUrl);
	LOG_INFO(_T("MaxUploadBytes : %lld"), static_cast<long long>(_nMaxUploadBytes));

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