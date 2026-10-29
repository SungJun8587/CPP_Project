
//***************************************************************************
// ZoneServerConfig.cpp: implementation of the CZoneServerConfig class.
//
//***************************************************************************

#include "pch.h"
#include "ZoneServerConfig.h"
#include <ConfigFile/JSONConfigFieldReader.h>
#include <ConfigFile/JSONConfigFieldWriter.h>

#include <filesystem>

//***************************************************************************
// @brief CZoneServerConfig 클래스의 생성자. SZoneServerOptions의 기본값으로 초기화한다.
//***************************************************************************
CZoneServerConfig::CZoneServerConfig()
{
	const SZoneServerOptions defaults;

	_tcsncpy_s(_tszServerName, _countof(_tszServerName), _T("ZoneServer"), _TRUNCATE);
	_nWorkerThreadCnt = static_cast<int32>(defaults.workerThreadCount);

	_nZoneId = defaults.zoneId;
	_nZoneKind = static_cast<uint32>(defaults.kind);
	_nCapacity = defaults.capacity;
	_nTickIntervalMs = defaults.tickIntervalMs;
	_fMinX = defaults.minX;
	_fMinY = defaults.minY;
	_fMaxX = defaults.maxX;
	_fMaxY = defaults.maxY;
	_fCellSize = defaults.cellSize;
	_fSpawnX = defaults.spawnX;
	_fSpawnY = defaults.spawnY;
	_fPlayerSpeed = defaults.playerSpeed;
	_nPlayerMaxHp = defaults.playerMaxHp;
	_nPlayerAttack = defaults.playerAttack;
	_nPlayerDefense = defaults.playerDefense;
	_fAttackRange = defaults.attackRange;
	_nAttackIntervalMs = defaults.attackIntervalMs;
	_nRespawnDelayMs = defaults.respawnDelayMs;
	_nRespawnInvulnMs = defaults.respawnInvulnMs;
	_bAllowPvp = defaults.allowPvp;
}

//***************************************************************************
// @brief JSON 설정 파일로부터 존 서버 구성 정보를 읽어와 초기화합니다.
// @param tszConfigPath 설정 파일 경로 (UTF-8, BOM 없음)
// @return 파일 열기/문법/타입/범위 오류가 없으면 true. 오류는 로그로 남긴다.
//***************************************************************************
bool CZoneServerConfig::Init(const TCHAR* tszConfigPath)
{
	CRapidJSONUtil jsonUtil;
	if( !jsonUtil.LoadFromFile(tszConfigPath) )
	{
		LOG_ERROR(_T("[CZoneServerConfig] load failed. path=%s"), tszConfigPath);
		return false;
	}
	if( !jsonUtil.IsObject() )
	{
		LOG_ERROR(_T("[CZoneServerConfig] top-level value must be an object. path=%s"), tszConfigPath);
		return false;
	}

	CJSONConfigFieldReader reader(jsonUtil);

	// 존 서버에 해당 개념이 없어 읽지 않는 키(공용 설정 템플릿에 남아 있어도 경고하지 않음)
	reader.Ignore(_T("ServiceName"));
	reader.Ignore(_T("DisplayName"));
	reader.Ignore(_T("GroupId"));
	reader.Ignore(_T("ChannelId"));
	reader.Ignore(_T("IP"));
	reader.Ignore(_T("Port"));
	reader.Ignore(_T("KeepAliveSec"));
	reader.Ignore(_T("MaxSessionCount"));
	reader.Ignore(_T("DBNode"));
	reader.Ignore(_T("RedisNode"));

	reader.Read(_T("Name"), _tszServerName);
	reader.Read(_T("WorkerThreadCnt"), _nWorkerThreadCnt, 1, 1024);
	reader.ReadObjects(_T("ServerNode"), _serverNodeVec,
		{
			{ _T("ID"), EFieldType::Int, 0, INT16_MAX },
			{ _T("Name"), EFieldType::String },
			{ _T("IP"), EFieldType::String },
			{ _T("Port"), EFieldType::UInt, 0, 65535 },
		});

	reader.Read(_T("ZoneId"), _nZoneId, 0, UINT32_MAX);
	reader.Read(_T("ZoneKind"), _nZoneKind, static_cast<uint32>(EZoneKind::Field), static_cast<uint32>(EZoneKind::Dungeon));
	reader.Read(_T("Capacity"), _nCapacity, 1, UINT32_MAX);
	reader.Read(_T("TickIntervalMs"), _nTickIntervalMs, 1, UINT32_MAX);
	reader.Read(_T("MinX"), _fMinX);
	reader.Read(_T("MinY"), _fMinY);
	reader.Read(_T("MaxX"), _fMaxX);
	reader.Read(_T("MaxY"), _fMaxY);
	reader.Read(_T("CellSize"), _fCellSize);
	reader.Read(_T("SpawnX"), _fSpawnX);
	reader.Read(_T("SpawnY"), _fSpawnY);
	reader.Read(_T("PlayerSpeed"), _fPlayerSpeed);

	reader.Read(_T("PlayerMaxHp"), _nPlayerMaxHp, 1, INT32_MAX);
	reader.Read(_T("PlayerAttack"), _nPlayerAttack, 0, INT32_MAX);
	reader.Read(_T("PlayerDefense"), _nPlayerDefense, 0, INT32_MAX);
	reader.Read(_T("AttackRange"), _fAttackRange, 0.1f, 10000.0f);
	reader.Read(_T("AttackIntervalMs"), _nAttackIntervalMs, 1, UINT32_MAX);
	reader.Read(_T("RespawnDelayMs"), _nRespawnDelayMs, 0, UINT32_MAX - 1);	// UINT32_MAX는 "리스폰 안 함" 예약값
	reader.Read(_T("RespawnInvulnMs"), _nRespawnInvulnMs, 0, UINT32_MAX);
	reader.Read(_T("AllowPvp"), _bAllowPvp);

	return reader.Validate();
}

//***************************************************************************
// @brief 현재 설정 값을 JSON 파일로 저장합니다.
// @detail 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를 보존한 채
//         알고 있는 필드의 값만 교체한다. 기존 파일을 읽을 수 없으면(문법 오류 등) 덮어쓰지 않는다.
// @param tszConfigPath 저장할 JSON 설정 파일 경로
// @return 성공 시 true, 실패 시 false(기존 파일은 그대로 남음)
//***************************************************************************
bool CZoneServerConfig::Save(const TCHAR* tszConfigPath) const
{
	CRapidJSONUtil jsonUtil;
	if( std::filesystem::exists(tszConfigPath) )
	{
		if( !jsonUtil.LoadFromFile(tszConfigPath) || !jsonUtil.IsObject() )
		{
			LOG_ERROR(_T("[CZoneServerConfig] existing file is not a valid config; not overwritten. path=%s"), tszConfigPath);
			return false;
		}
	}

	CJSONConfigFieldWriter writer(jsonUtil);

	writer.Write(_T("Name"), _tszServerName);
	writer.Write(_T("WorkerThreadCnt"), _nWorkerThreadCnt);
	writer.WriteObjects(_T("ServerNode"), _serverNodeVec);

	writer.Write(_T("ZoneId"), _nZoneId);
	writer.Write(_T("ZoneKind"), _nZoneKind);
	writer.Write(_T("Capacity"), _nCapacity);
	writer.Write(_T("TickIntervalMs"), _nTickIntervalMs);
	writer.Write(_T("MinX"), _fMinX);
	writer.Write(_T("MinY"), _fMinY);
	writer.Write(_T("MaxX"), _fMaxX);
	writer.Write(_T("MaxY"), _fMaxY);
	writer.Write(_T("CellSize"), _fCellSize);
	writer.Write(_T("SpawnX"), _fSpawnX);
	writer.Write(_T("SpawnY"), _fSpawnY);
	writer.Write(_T("PlayerSpeed"), _fPlayerSpeed);
	writer.Write(_T("PlayerMaxHp"), _nPlayerMaxHp);
	writer.Write(_T("PlayerAttack"), _nPlayerAttack);
	writer.Write(_T("PlayerDefense"), _nPlayerDefense);
	writer.Write(_T("AttackRange"), _fAttackRange);
	writer.Write(_T("AttackIntervalMs"), _nAttackIntervalMs);
	writer.Write(_T("RespawnDelayMs"), _nRespawnDelayMs);
	writer.Write(_T("RespawnInvulnMs"), _nRespawnInvulnMs);
	writer.Write(_T("AllowPvp"), _bAllowPvp);

	return writer.Save(tszConfigPath);
}

//***************************************************************************
// @brief 로드된 설정 정보를 로그로 출력합니다.
//***************************************************************************
void CZoneServerConfig::PrintServerSettingInfo() const
{
	LOG_INFO(_T("###################################################################"));
	LOG_INFO(_T("--------------- [Start Print : Zone Server Setting Info] ---------------"));
	LOG_INFO(_T("ServerName : %s"), _tszServerName);
	LOG_INFO(_T("WorkerThreadCnt : %d"), _nWorkerThreadCnt);
	LOG_INFO(_T("ZoneId : %u"), _nZoneId);
	LOG_INFO(_T("ZoneKind : %u"), _nZoneKind);
	LOG_INFO(_T("Capacity : %u"), _nCapacity);
	LOG_INFO(_T("TickIntervalMs : %u"), _nTickIntervalMs);
	LOG_INFO(_T("Bounds : (%.1f, %.1f) - (%.1f, %.1f)"), _fMinX, _fMinY, _fMaxX, _fMaxY);
	LOG_INFO(_T("CellSize : %.1f"), _fCellSize);
	LOG_INFO(_T("Spawn : (%.1f, %.1f)"), _fSpawnX, _fSpawnY);
	LOG_INFO(_T("PlayerSpeed : %.1f"), _fPlayerSpeed);
	LOG_INFO(_T("PlayerMaxHp : %d, Attack : %d, Defense : %d"), _nPlayerMaxHp, _nPlayerAttack, _nPlayerDefense);
	LOG_INFO(_T("AttackRange : %.1f, AttackIntervalMs : %u"), _fAttackRange, _nAttackIntervalMs);
	LOG_INFO(_T("RespawnDelayMs : %u, RespawnInvulnMs : %u, AllowPvp : %d"), _nRespawnDelayMs, _nRespawnInvulnMs, _bAllowPvp ? 1 : 0);

	LOG_INFO(_T("--------------- Connect ServerNode size : %d ---------------"), static_cast<int>(_serverNodeVec.size()));
	for( uint32 i = 0; i < _serverNodeVec.size(); i++ )
	{
		LOG_INFO(_T("ID : %d"), _serverNodeVec[i]._nID);
		LOG_INFO(_T("ServerName : %s"), _serverNodeVec[i]._tszServerName);
		LOG_INFO(_T("IP : %s"), _serverNodeVec[i]._tszIP);
		LOG_INFO(_T("Port : %d"), _serverNodeVec[i]._nPort);
		LOG_INFO(_T("------------------------------"));
	}

	LOG_INFO(_T("--------------- [End Print] ---------------"));
	LOG_INFO(_T("###################################################################"));
}

//***************************************************************************
// @brief 로드된 설정을 SZoneServerOptions로 변환합니다.
// @details ServerNode가 비어 있으면 SZoneServerOptions의 기본 Gateway 주소를 유지한다.
//***************************************************************************
void CZoneServerConfig::ToOptions(SZoneServerOptions& options) const
{
	options.zoneId = _nZoneId;
	options.kind = static_cast<EZoneKind>(_nZoneKind);
	options.capacity = _nCapacity;
	options.serverName = TStringToString(_tszServerName);

	if( !_serverNodeVec.empty() )
	{
		options.gatewayIp = _serverNodeVec[0]._tszIP;
		options.gatewayPort = _serverNodeVec[0]._nPort;
	}
	options.workerThreadCount = static_cast<uint32>(_nWorkerThreadCnt);

	options.tickIntervalMs = _nTickIntervalMs;
	options.minX = _fMinX;
	options.minY = _fMinY;
	options.maxX = _fMaxX;
	options.maxY = _fMaxY;
	options.cellSize = _fCellSize;
	options.spawnX = _fSpawnX;
	options.spawnY = _fSpawnY;
	options.playerSpeed = _fPlayerSpeed;
	options.playerMaxHp = _nPlayerMaxHp;
	options.playerAttack = _nPlayerAttack;
	options.playerDefense = _nPlayerDefense;
	options.attackRange = _fAttackRange;
	options.attackIntervalMs = _nAttackIntervalMs;
	options.respawnDelayMs = _nRespawnDelayMs;
	options.respawnInvulnMs = _nRespawnInvulnMs;
	options.allowPvp = _bAllowPvp;
}