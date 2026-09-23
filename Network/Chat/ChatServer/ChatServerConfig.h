
//***************************************************************************
// ChatServerConfig.h : interface for the CChatServerConfig class.
//
//***************************************************************************

#ifndef UC_CHATSERVERCONFIG_H
#define UC_CHATSERVERCONFIG_H

#include <ServerConfig.h>

//***************************************************************************
// @brief 채팅 서버 설정 정보 관리 클래스.
// @details [수정 — 리팩터링] CServerConfig는 이제 필드+게터만 제공하고
//          (ServerConfig.h 상단 설명 참고), Init()/ToJSON()/FromJSON()/
//          PrintServerSettingInfo()는 이 클래스가 전부 직접 구현한다 —
//          베이스 필드(ServiceName/DisplayName/ServerName/GroupId/
//          ChannelId/IP/Port/KeepAliveSec/MaxSessionCount/WorkerThreadCnt/
//          ServerNodeVec/DBNodeVec/RedisNodeVec)와 이 클래스 고유 6개
//          필드(RedisPoolSize/DbWorkerThreadCnt/HeartbeatTtlSec/
//          HeartbeatIntervalSec/FileServerUrl/MaxRoomsPerOwner)를 같은
//          JSON 파일 하나에서 한 번에 읽는다.
//
//          이전에 "SERVER_CONFIG" 같은 매크로/전역 접근자로
//          CServerConfig::Instance()를 가리키던 곳이 있다면, 그 정의를
//          CChatServerConfig::Instance()로 같이 고쳐야 한다(이 리팩터링만
//          으로는 자동으로 안 바뀜 — 그 매크로가 정의된 파일을 몰라서 이
//          커밋에 포함 못 시켰다).
//***************************************************************************
class CChatServerConfig : public CServerConfig, public CSingleton<CChatServerConfig>
{
public:
	CChatServerConfig();
	virtual ~CChatServerConfig();

	bool Init(const TCHAR* tszServerInfo);

	//***************************************************************************
	// @brief Redis 커넥션 풀 크기를 반환합니다.
	//***************************************************************************
	int32 GetRedisPoolSize() { return _nRedisPoolSize; }

	//***************************************************************************
	// @brief DB 처리 워커 스레드 수를 반환합니다.
	//***************************************************************************
	int32 GetDbWorkerThreadCnt() { return _nDbWorkerThreadCnt; }

	//***************************************************************************
	// @brief 하트비트 TTL(초)을 반환합니다.
	//***************************************************************************
	int32 GetHeartbeatTtlSec() { return _nHeartbeatTtlSec; }

	//***************************************************************************
	// @brief 하트비트 갱신 주기(초)를 반환합니다.
	//***************************************************************************
	int32 GetHeartbeatIntervalSec() { return _nHeartbeatIntervalSec; }

	//***************************************************************************
	// @brief 클라이언트에게 알려줄 파일 서버 주소를 반환합니다(프로필
	//        이미지 업로드용, 채팅 서버와 별개 프로세스).
	//***************************************************************************
	TCHAR* GetFileServerUrl() { return _tszFileServerUrl; }

	//***************************************************************************
	// @brief 1인당 생성 가능한 방 개수 상한을 반환합니다(0 이하면 무제한).
	//***************************************************************************
	int32 GetMaxRoomsPerOwner() { return _nMaxRoomsPerOwner; }

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
		value.AddMember(_T("RedisPoolSize"), _nRedisPoolSize, allocator);
		value.AddMember(_T("DbWorkerThreadCnt"), _nDbWorkerThreadCnt, allocator);
		value.AddMember(_T("HeartbeatTtlSec"), _nHeartbeatTtlSec, allocator);
		value.AddMember(_T("HeartbeatIntervalSec"), _nHeartbeatIntervalSec, allocator);
		value.AddMember(_T("FileServerUrl"), _tValue(_tszFileServerUrl, allocator), allocator);
		value.AddMember(_T("MaxRoomsPerOwner"), _nMaxRoomsPerOwner, allocator);
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
		_nRedisPoolSize = value[_T("RedisPoolSize")].GetInt();
		_nDbWorkerThreadCnt = value[_T("DbWorkerThreadCnt")].GetInt();
		_nHeartbeatTtlSec = value[_T("HeartbeatTtlSec")].GetInt();
		_nHeartbeatIntervalSec = value[_T("HeartbeatIntervalSec")].GetInt();
		_tcsncpy_s(_tszFileServerUrl, _countof(_tszFileServerUrl), value[_T("FileServerUrl")].GetString(), _TRUNCATE);
		_nMaxRoomsPerOwner = value[_T("MaxRoomsPerOwner")].GetInt();
	}

private:
	int32						_nRedisPoolSize;					// Redis 커넥션 풀 크기
	int32						_nDbWorkerThreadCnt;				// DB 처리 워커 스레드 수
	int32						_nHeartbeatTtlSec;					// 하트비트 TTL(초)
	int32						_nHeartbeatIntervalSec;				// 하트비트 갱신 주기(초)

	TCHAR						_tszFileServerUrl[HOSTNAME_STRLEN];	// 클라이언트에게 알려줄 파일 서버 주소(프로필 이미지 업로드용, 채팅 서버와 별개 프로세스)
	int32						_nMaxRoomsPerOwner;					// 1인당 생성 가능한 방 개수 상한(0 이하면 무제한)
};

#endif // ndef UC_CHATSERVERCONFIG_H