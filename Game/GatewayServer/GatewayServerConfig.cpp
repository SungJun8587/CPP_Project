
//***************************************************************************
// GatewayServerConfig.cpp: implementation of the CGatewayServerConfig class.
//
//***************************************************************************

#include "pch.h"
#include "GatewayServerConfig.h"
#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

#include <filesystem>

namespace
{
	// 포트 번호 허용 범위
	constexpr uint16 kMinPort = 1;
	constexpr uint16 kMaxPort = 65535;
}

//***************************************************************************
// @brief CGatewayServerConfig 클래스의 생성자. SGatewayOptions의 기본값으로 초기화한다.
//***************************************************************************
CGatewayServerConfig::CGatewayServerConfig()
{
	const SGatewayOptions defaults;

	_tcsncpy_s(_tszServerName, _countof(_tszServerName), _T("GatewayServer"), _TRUNCATE);
	_tcsncpy_s(_tszIP, _countof(_tszIP), defaults.clientBindIp.c_str(), _TRUNCATE);
	_nServerPort = defaults.clientPort;
	_nMaxSessionCount = defaults.maxClientSessions;
	_nWorkerThreadCnt = static_cast<int32>(defaults.clientWorkerCount);

	_tcsncpy_s(_tszZoneIP, _countof(_tszZoneIP), defaults.zoneBindIp.c_str(), _TRUNCATE);
	_nZonePort = defaults.zonePort;
	_nZoneMaxSessionCount = defaults.maxZoneSessions;
	_nZoneWorkerThreadCnt = static_cast<int32>(defaults.zoneWorkerCount);
	_nHeartbeatIntervalMs = defaults.heartbeatIntervalMs;
	_nHeartbeatTimeoutMs = defaults.heartbeatTimeoutMs;
}

//***************************************************************************
// @brief JSON 설정 파일로부터 게이트웨이 구성 정보를 읽어와 초기화합니다.
// @param tszConfigPath 설정 파일 경로 (UTF-8, BOM 없음)
// @return 파일 열기/문법/타입/범위 오류가 없으면 true. 오류는 로그로 남긴다.
//***************************************************************************
bool CGatewayServerConfig::Init(const TCHAR* tszConfigPath)
{
	CRapidJSONUtil jsonUtil;
	if( !jsonUtil.LoadFromFile(tszConfigPath) )
	{
		LOG_ERROR(_T("[CGatewayServerConfig] load failed. path=%s"), tszConfigPath);
		return false;
	}
	if( !jsonUtil.IsObject() )
	{
		LOG_ERROR(_T("[CGatewayServerConfig] top-level value must be an object. path=%s"), tszConfigPath);
		return false;
	}

	CJSONConfigFieldReader reader(jsonUtil);

	// 게이트웨이에 해당 개념이 없어 읽지 않는 키(공용 설정 템플릿에 남아 있어도 경고하지 않음)
	reader.Ignore(_T("ServiceName"));
	reader.Ignore(_T("DisplayName"));
	reader.Ignore(_T("GroupId"));
	reader.Ignore(_T("ChannelId"));
	reader.Ignore(_T("KeepAliveSec"));
	reader.Ignore(_T("ServerNode"));
	reader.Ignore(_T("DBNode"));
	reader.Ignore(_T("RedisNode"));

	reader.Read(_T("Name"), _tszServerName);
	reader.Read(_T("IP"), _tszIP);
	reader.Read(_T("Port"), _nServerPort, kMinPort, kMaxPort);
	reader.Read(_T("MaxSessionCount"), _nMaxSessionCount, 1, INT32_MAX);
	reader.Read(_T("WorkerThreadCnt"), _nWorkerThreadCnt, 0, 1024);

	reader.Read(_T("ZoneIP"), _tszZoneIP);
	reader.Read(_T("ZonePort"), _nZonePort, kMinPort, kMaxPort);
	reader.Read(_T("ZoneMaxSessionCount"), _nZoneMaxSessionCount, 1, INT32_MAX);
	reader.Read(_T("ZoneWorkerThreadCnt"), _nZoneWorkerThreadCnt, 0, 1024);

	reader.Read(_T("HeartbeatIntervalMs"), _nHeartbeatIntervalMs, 1, UINT32_MAX);
	reader.Read(_T("HeartbeatTimeoutMs"), _nHeartbeatTimeoutMs, 1, UINT32_MAX);

	if( !reader.Validate() )
		return false;

	if( _nHeartbeatTimeoutMs <= _nHeartbeatIntervalMs )
	{
		LOG_ERROR(_T("[CGatewayServerConfig] HeartbeatTimeoutMs(%u) must be greater than HeartbeatIntervalMs(%u)"),
			_nHeartbeatTimeoutMs, _nHeartbeatIntervalMs);
		return false;
	}

	return true;
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @detail 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를 보존한 채
//         알고 있는 필드의 값만 교체한다. 기존 파일을 읽을 수 없으면(문법 오류 등) 덮어쓰지 않는다.
// @param tszConfigPath 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false(기존 파일은 그대로 남음)
//***************************************************************************
bool CGatewayServerConfig::Save(const TCHAR* tszConfigPath) const
{
	CRapidJSONUtil jsonUtil;
	if( std::filesystem::exists(tszConfigPath) )
	{
		if( !jsonUtil.LoadFromFile(tszConfigPath) || !jsonUtil.IsObject() )
		{
			LOG_ERROR(_T("[CGatewayServerConfig] existing file is not a valid config; not overwritten. path=%s"), tszConfigPath);
			return false;
		}
	}

	CJSONConfigFieldWriter writer(jsonUtil);

	writer.Write(_T("Name"), _tszServerName);
	writer.Write(_T("IP"), _tszIP);
	writer.Write(_T("Port"), _nServerPort);
	writer.Write(_T("MaxSessionCount"), _nMaxSessionCount);
	writer.Write(_T("WorkerThreadCnt"), _nWorkerThreadCnt);

	writer.Write(_T("ZoneIP"), _tszZoneIP);
	writer.Write(_T("ZonePort"), _nZonePort);
	writer.Write(_T("ZoneMaxSessionCount"), _nZoneMaxSessionCount);
	writer.Write(_T("ZoneWorkerThreadCnt"), _nZoneWorkerThreadCnt);

	writer.Write(_T("HeartbeatIntervalMs"), _nHeartbeatIntervalMs);
	writer.Write(_T("HeartbeatTimeoutMs"), _nHeartbeatTimeoutMs);

	return writer.Save(tszConfigPath);
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CGatewayServerConfig::PrintServerSettingInfo() const
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : Gateway Setting Info] ---------------"));
	LOG_INFO(_T("ServerName : %s"), _tszServerName);
	LOG_INFO(_T("[Client] IP : %s"), _tszIP);
	LOG_INFO(_T("[Client] Port : %d"), _nServerPort);
	LOG_INFO(_T("[Client] MaxSessionCount : %d"), _nMaxSessionCount);
	LOG_INFO(_T("[Client] WorkerThreadCnt : %d"), _nWorkerThreadCnt);
	LOG_INFO(_T("[Zone] IP : %s"), _tszZoneIP);
	LOG_INFO(_T("[Zone] Port : %d"), _nZonePort);
	LOG_INFO(_T("[Zone] MaxSessionCount : %d"), _nZoneMaxSessionCount);
	LOG_INFO(_T("[Zone] WorkerThreadCnt : %d"), _nZoneWorkerThreadCnt);
	LOG_INFO(_T("HeartbeatIntervalMs : %u"), _nHeartbeatIntervalMs);
	LOG_INFO(_T("HeartbeatTimeoutMs : %u"), _nHeartbeatTimeoutMs);
	LOG_INFO(_T("--------------- [End Print] ---------------"));
	LOG_INFO(_T("###################################################################"));
}

//***************************************************************************
// @brief 로드된 설정을 SGatewayOptions로 변환합니다.
//***************************************************************************
void CGatewayServerConfig::ToOptions(SGatewayOptions& options) const
{
	options.clientBindIp = _tszIP;
	options.clientPort = _nServerPort;
	options.maxClientSessions = _nMaxSessionCount;
	options.clientWorkerCount = static_cast<uint32>(_nWorkerThreadCnt);

	options.zoneBindIp = _tszZoneIP;
	options.zonePort = _nZonePort;
	options.maxZoneSessions = _nZoneMaxSessionCount;
	options.zoneWorkerCount = static_cast<uint32>(_nZoneWorkerThreadCnt);

	options.heartbeatIntervalMs = _nHeartbeatIntervalMs;
	options.heartbeatTimeoutMs = _nHeartbeatTimeoutMs;
}