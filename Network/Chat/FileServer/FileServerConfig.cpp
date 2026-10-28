
//***************************************************************************
// FileServerConfig.cpp: implementation of the CFileServerConfig class.
//
//***************************************************************************

#include "pch.h"
#include "FileServerConfig.h"

#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

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
// @return 성공 시 true, 실패 시 false
// @details ServiceName/DisplayName/GroupId/ChannelId/KeepAliveSec/
//          ServerNode/DBNode는 파일 서버 개념에 없으므로 읽지 않는다 —
//          그 필드들은 상속만 받고 기본값 그대로 남는다(파일에 있어도 경고하지 않음).
//          파일에 없는 키는 기본값을 유지하고, 있는데 타입이 맞지 않거나 범위를
//          벗어나면 모아서 로그로 남기고 false를 돌려준다.
//***************************************************************************
bool CFileServerConfig::Init(const TCHAR* tszServerInfo)
{
	CRapidJSONUtil json;
	if( !json.LoadFromFile(tszServerInfo) )
	{
		LOG_ERROR(_T("[Config] load failed. path=%s"), tszServerInfo);
		return false;
	}

	CJSONConfigFieldReader reader(json);
	reader.Ignore(_T("ServiceName"));
	reader.Ignore(_T("DisplayName"));
	reader.Ignore(_T("GroupId"));
	reader.Ignore(_T("ChannelId"));

	reader.Read(_T("Name"), _tszServerName);
	reader.Read(_T("IP"), _tszIP);
	reader.Read(_T("Port"), _nServerPort, static_cast<uint16>(1), static_cast<uint16>(65535));
	reader.Read(_T("MaxSessionCount"), _nMaxSessionCount, 1);
	reader.Read(_T("WorkerThreadCnt"), _nWorkerThreadCnt, 0);		// 0이면 서비스가 자동 결정

	reader.Read(_T("RedisPoolSize"), _nRedisPoolSize, 1);

	reader.Read(_T("StorageDir"), _tszStorageDir);
	reader.Read(_T("PublicBaseUrl"), _tszPublicBaseUrl);
	reader.Read(_T("MaxUploadBytes"), _nMaxUploadBytes, static_cast<int64>(1));	// 바이트, 0 이하는 허용하지 않음

	reader.ReadObjects(_T("RedisNode"), _redisNodeVec,
		{
			{ _T("ID"), EFieldType::Int, 1, 32767 },
			{ _T("IP"), EFieldType::String },
			{ _T("Port"), EFieldType::UInt, 1, 65535 },
			{ _T("UID"), EFieldType::String },
			{ _T("PWD"), EFieldType::String },
			{ _T("DBIndex"), EFieldType::Int, 0 },
		});

	return reader.Validate();
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @param tszServerInfo 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
//***************************************************************************
bool CFileServerConfig::Save(const TCHAR* tszServerInfo)
{
	// 기존 파일이 있으면 읽어 와서 모르는 키와 키 순서를 보존한다(없으면 빈 문서에서 시작).
	CRapidJSONUtil json;
	json.LoadFromFile(tszServerInfo);

	CJSONConfigFieldWriter writer(json);

	writer.Write(_T("Name"), _tszServerName);
	writer.Write(_T("IP"), _tszIP);
	writer.Write(_T("Port"), _nServerPort);
	writer.Write(_T("MaxSessionCount"), _nMaxSessionCount);
	writer.Write(_T("WorkerThreadCnt"), _nWorkerThreadCnt);

	writer.Write(_T("RedisPoolSize"), _nRedisPoolSize);

	writer.Write(_T("StorageDir"), _tszStorageDir);
	writer.Write(_T("PublicBaseUrl"), _tszPublicBaseUrl);
	writer.Write(_T("MaxUploadBytes"), _nMaxUploadBytes);

	writer.WriteObjects(_T("RedisNode"), _redisNodeVec);

	return writer.Save(tszServerInfo);
}

//***************************************************************************
// @brief 로드된 설정 값을 CFileServerMain::Start()에 넘길 구동 옵션으로 옮겨 담습니다.
//***************************************************************************
void CFileServerConfig::ToOptions(SFileServerOptions& options) const
{
	options.bindIp = _tszIP;
	options.bindPort = _nServerPort;
	options.maxSessionCount = _nMaxSessionCount;
	options.workerThreadCount = static_cast<uint32>(_nWorkerThreadCnt);

	options.redisNodes = _redisNodeVec;
	options.redisPoolSize = _nRedisPoolSize;

	options.storageDir = _tszStorageDir;
	options.publicBaseUrl = TStringToString(_tszPublicBaseUrl);
	options.maxUploadBytes = _nMaxUploadBytes;
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