
//***************************************************************************
// ChatServerConfig.cpp: implementation of the CChatServerConfig class.
//
//***************************************************************************

#include "pch.h"
#include "ChatServerConfig.h"

#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

//***************************************************************************
// Construction/Destruction
//***************************************************************************

//***************************************************************************
// @brief CChatServerConfig 클래스의 생성자
//***************************************************************************
CChatServerConfig::CChatServerConfig()
	: _nRedisPoolSize(0), _nDbWorkerThreadCnt(0)
	, _nHeartbeatTtlSec(0), _nHeartbeatIntervalSec(0)
	, _nMaxRoomsPerOwner(0)
	, _nEnableRelay(1)
{
	memset(_tszFileServerUrl, 0, sizeof(_tszFileServerUrl));
}

//***************************************************************************
// @brief CChatServerConfig 클래스의 소멸자
//***************************************************************************
CChatServerConfig::~CChatServerConfig()
{
}

namespace
{
	// 포트 번호 허용 범위
	constexpr uint16 kMinPort = 1;
	constexpr uint16 kMaxPort = 65535;
	// 노드 ID 허용 범위(Get*Node()가 ID - 1을 인덱스로 쓰므로 1부터)
	constexpr int64 kMinNodeId = 1;
	constexpr int64 kMaxNodeId = 32767;
}

//***************************************************************************
// @brief JSON 설정 파일로부터 서버 구성 정보를 읽어와 초기화합니다.
// @param tszServerInfo JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
// @details 베이스 필드와 이 클래스 고유 필드를 같은 문서에서 한 번에 읽는다(파일을
//          두 번 열지 않음). 파일에 없는 키는 기본값을 유지하고, 있는데 타입이
//          맞지 않거나 범위를 벗어나면 모아서 로그로 남기고 false를 돌려준다.
//          읽지 않는 키는 경고만 남긴다.
//***************************************************************************
bool CChatServerConfig::Init(const TCHAR* tszServerInfo)
{
	CRapidJSONUtil json;
	if( !json.LoadFromFile(tszServerInfo) )
	{
		LOG_ERROR(_T("[Config] load failed. path=%s"), tszServerInfo);
		return false;
	}

	CJSONConfigFieldReader reader(json);
	reader.Ignore(_T("UseTestMode"));	// 현재 코드에서 읽지 않는 키(파일에는 남아 있음)

	reader.Read(_T("ServiceName"), _tszServiceName);
	reader.Read(_T("DisplayName"), _tszDisplayName);
	reader.Read(_T("Name"), _tszServerName);
	reader.Read(_T("GroupId"), _nServerGroupId);
	reader.Read(_T("ChannelId"), _nServerChannelId);

	reader.Read(_T("IP"), _tszIP);
	reader.Read(_T("Port"), _nServerPort, kMinPort, kMaxPort);
	reader.Read(_T("KeepAliveSec"), _nKeepAliveSec, 0);
	reader.Read(_T("MaxSessionCount"), _nMaxSessionCount, 1);
	reader.Read(_T("WorkerThreadCnt"), _nWorkerThreadCnt, 0);		// 0이면 서비스가 자동 결정

	reader.ReadObjects(_T("ServerNode"), _serverNodeVec,
		{
			{ _T("ID"), EFieldType::Int, kMinNodeId, kMaxNodeId },
			{ _T("Name"), EFieldType::String },
			{ _T("IP"), EFieldType::String },
			{ _T("Port"), EFieldType::UInt, kMinPort, kMaxPort },
		});
	reader.ReadObjects(_T("DBNode"), _dbNodeVec,
		{
			{ _T("ID"), EFieldType::Int, kMinNodeId, kMaxNodeId },
			{ _T("DBClass"), EFieldType::Int, 1, 3 },
			{ _T("DSNDriver"), EFieldType::String },
			{ _T("IP"), EFieldType::String },
			{ _T("Port"), EFieldType::UInt, kMinPort, kMaxPort },
			{ _T("DBName"), EFieldType::String },
			{ _T("UID"), EFieldType::String },
			{ _T("PWD"), EFieldType::String },
		});
	reader.ReadObjects(_T("RedisNode"), _redisNodeVec,
		{
			{ _T("ID"), EFieldType::Int, kMinNodeId, kMaxNodeId },
			{ _T("IP"), EFieldType::String },
			{ _T("Port"), EFieldType::UInt, kMinPort, kMaxPort },
			{ _T("UID"), EFieldType::String },
			{ _T("PWD"), EFieldType::String },
			{ _T("DBIndex"), EFieldType::Int, 0 },
		});

	// 채팅 서버 고유 필드.
	reader.Read(_T("RedisPoolSize"), _nRedisPoolSize, 1);
	reader.Read(_T("DbWorkerThreadCnt"), _nDbWorkerThreadCnt, 1);
	reader.Read(_T("HeartbeatTtlSec"), _nHeartbeatTtlSec, 1);
	reader.Read(_T("HeartbeatIntervalSec"), _nHeartbeatIntervalSec, 1);
	reader.Read(_T("FileServerUrl"), _tszFileServerUrl);
	reader.Read(_T("MaxRoomsPerOwner"), _nMaxRoomsPerOwner);		// 0 이하면 무제한
	reader.Read(_T("EnableRelay"), _nEnableRelay, 0, 1);			// 1이면 다른 채널의 접속자에게도 알림/대화를 전달

	return reader.Validate();
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @param tszServerInfo 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false
//***************************************************************************
bool CChatServerConfig::Save(const TCHAR* tszServerInfo)
{
	// 기존 파일이 있으면 읽어 와서 모르는 키와 키 순서를 보존한다(없으면 빈 문서에서 시작).
	CRapidJSONUtil json;
	json.LoadFromFile(tszServerInfo);

	CJSONConfigFieldWriter writer(json);

	writer.Write(_T("ServiceName"), _tszServiceName);
	writer.Write(_T("DisplayName"), _tszDisplayName);
	writer.Write(_T("Name"), _tszServerName);
	writer.Write(_T("GroupId"), _nServerGroupId);
	writer.Write(_T("ChannelId"), _nServerChannelId);

	writer.Write(_T("IP"), _tszIP);
	writer.Write(_T("Port"), _nServerPort);
	writer.Write(_T("KeepAliveSec"), _nKeepAliveSec);
	writer.Write(_T("MaxSessionCount"), _nMaxSessionCount);
	writer.Write(_T("WorkerThreadCnt"), _nWorkerThreadCnt);

	writer.WriteObjects(_T("ServerNode"), _serverNodeVec);
	writer.WriteObjects(_T("DBNode"), _dbNodeVec);
	writer.WriteObjects(_T("RedisNode"), _redisNodeVec);

	writer.Write(_T("RedisPoolSize"), _nRedisPoolSize);
	writer.Write(_T("DbWorkerThreadCnt"), _nDbWorkerThreadCnt);
	writer.Write(_T("HeartbeatTtlSec"), _nHeartbeatTtlSec);
	writer.Write(_T("HeartbeatIntervalSec"), _nHeartbeatIntervalSec);
	writer.Write(_T("FileServerUrl"), _tszFileServerUrl);
	writer.Write(_T("MaxRoomsPerOwner"), _nMaxRoomsPerOwner);
	writer.Write(_T("EnableRelay"), _nEnableRelay);

	return writer.Save(tszServerInfo);
}

//***************************************************************************
// @brief 로드된 설정 값을 CChatServerMain::Start()에 넘길 구동 옵션으로 옮겨 담습니다.
//***************************************************************************
void CChatServerConfig::ToOptions(SChatServerOptions& options) const
{
	options.serverName = TStringToString(_tszServerName);
	options.serverGroupId = std::to_string(_nServerGroupId);
	options.serverChannelId = std::to_string(_nServerChannelId);

	options.bindIp = _tszIP;
	options.bindPort = _nServerPort;
	options.maxSessionCount = _nMaxSessionCount;
	options.workerThreadCount = static_cast<uint32>(_nWorkerThreadCnt);

	options.redisNodes = _redisNodeVec;
	options.redisPoolSize = _nRedisPoolSize;

	options.dbNodes = _dbNodeVec;
	options.dbWorkerThreadCount = _nDbWorkerThreadCnt;

	options.heartbeatTtlSec = _nHeartbeatTtlSec;
	options.heartbeatIntervalSec = _nHeartbeatIntervalSec;

	options.fileServerUrl = TStringToString(_tszFileServerUrl);
	options.maxRoomsPerOwner = _nMaxRoomsPerOwner;
	options.enableRelay = (_nEnableRelay != 0);
}

//***************************************************************************
// @brief 로드된 서버 설정 정보 및 노드 목록을 로그로 출력합니다.
//***************************************************************************
void CChatServerConfig::PrintServerSettingInfo()
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : Server Setting Info] ---------------"));
	LOG_INFO(_T("ServiceName : %s"), _tszServiceName);
	LOG_INFO(_T("DisplayName : %s"), _tszDisplayName);
	LOG_INFO(_T("ServerName : %s"), _tszServerName);
	LOG_INFO(_T("ServerGroupId : %d"), _nServerGroupId);
	LOG_INFO(_T("ServerChannelId : %d"), _nServerChannelId);
	LOG_INFO(_T("IP : %s"), _tszIP);
	LOG_INFO(_T("Port : %d"), _nServerPort);
	LOG_INFO(_T("KeepAliveSec : %d"), _nKeepAliveSec);
	LOG_INFO(_T("MaxSessionCount : %d"), _nMaxSessionCount);
	LOG_INFO(_T("WorkerThreadCnt : %d"), _nWorkerThreadCnt);
	LOG_INFO(_T("RedisPoolSize : %d"), _nRedisPoolSize);
	LOG_INFO(_T("DbWorkerThreadCnt : %d"), _nDbWorkerThreadCnt);
	LOG_INFO(_T("HeartbeatTtlSec : %d"), _nHeartbeatTtlSec);
	LOG_INFO(_T("HeartbeatIntervalSec : %d"), _nHeartbeatIntervalSec);
	LOG_INFO(_T("FileServerUrl : %s"), _tszFileServerUrl);
	LOG_INFO(_T("MaxRoomsPerOwner : %d"), _nMaxRoomsPerOwner);
	LOG_INFO(_T("EnableRelay : %d"), _nEnableRelay);

	LOG_INFO(_T("--------------- Connect ServerNode size : %d ---------------"), static_cast<int>(_serverNodeVec.size()));
	for( uint32 i = 0; i < _serverNodeVec.size(); i++ )
	{
		LOG_INFO(_T("ID : %d"), _serverNodeVec[i]._nID);
		LOG_INFO(_T("ServerName : %s"), _serverNodeVec[i]._tszServerName);
		LOG_INFO(_T("IP : %s"), _serverNodeVec[i]._tszIP);
		LOG_INFO(_T("Port : %d"), _serverNodeVec[i]._nPort);
		LOG_INFO(_T("------------------------------"));
	}

	LOG_INFO(_T("--------------- Connect DBNode size : %d ---------------"), static_cast<int>(_dbNodeVec.size()));
	for( uint32 i = 0; i < _dbNodeVec.size(); i++ )
	{
		LOG_INFO(_T("ID : %d"), _dbNodeVec[i]._nID);
		LOG_INFO(_T("DSNDriver : %s"), _dbNodeVec[i]._tszDSNDriver);
		LOG_INFO(_T("DBHost : %s"), _dbNodeVec[i]._tszDBHost);
		LOG_INFO(_T("Port : %d"), _dbNodeVec[i]._nPort);
		LOG_INFO(_T("DBName : %s"), _dbNodeVec[i]._tszDBName);
		LOG_INFO(_T("DBUserId : %s"), _dbNodeVec[i]._tszDBUserId);
		LOG_INFO(_T("DBPasswd : %s"), _dbNodeVec[i]._tszDBPasswd);
		LOG_INFO(_T("------------------------------"));
	}

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