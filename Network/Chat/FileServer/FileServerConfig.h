
//***************************************************************************
// FileServerConfig.h : interface for the CFileServerConfig class.
//
//***************************************************************************

#ifndef UC_FILESERVERCONFIG_H
#define UC_FILESERVERCONFIG_H

#include <ServerConnectInfo.h>
#include <Memory/Singleton.h>

//***************************************************************************
// @brief 파일 서버 설정 정보 관리 클래스
// @details JSON 기반의 설정(바인드 IP/포트, Redis 노드, 저장 경로, 공개 URL,
//          업로드 크기 제한 등)을 로드하고 관리합니다.
//***************************************************************************
class CFileServerConfig : public CSingleton<CFileServerConfig>
{
public:
	CFileServerConfig();
	virtual ~CFileServerConfig();

	bool Init(const TCHAR* tszServerInfo);

	//***************************************************************************
	// @brief 서비스 이름을 반환합니다.
	// @return 서비스 이름 문자열 포인터
	//***************************************************************************
	TCHAR* GetServiceName() { return _tszServiceName; }

	//***************************************************************************
	// @brief 서비스 표시 이름을 반환합니다.
	// @return 서비스 표시 이름 문자열 포인터
	//***************************************************************************
	TCHAR* GetDisplayName(void) { return _tszDisplayName; }

	//***************************************************************************
	// @brief 서버 이름을 반환합니다(로그/하트비트 표시용).
	//***************************************************************************
	TCHAR* GetServerName() { return _tszServerName; }

	//***************************************************************************
	// @brief 서버 그룹 ID를 반환합니다.
	// @return 서버 그룹 ID
	//***************************************************************************
	uint16 GetServerGroupId() { return _nServerGroupId; }

	//***************************************************************************
	// @brief 서버 채널 ID를 반환합니다.
	// @return 서버 채널 ID
	//***************************************************************************
	uint16 GetServerChannelId() { return _nServerChannelId; }

	//***************************************************************************
	// @brief 이 서버가 바인드할 IP 주소를 반환합니다.
	//***************************************************************************
	TCHAR* GetServerIP() { return _tszIP; }

	//***************************************************************************
	// @brief 이 서버가 바인드할 포트 번호의 참조를 반환합니다.
	//***************************************************************************
	uint16& GetServerPort() { return _nServerPort; }

	//***************************************************************************
	// @brief 최대 동시 접속(HTTP 연결) 수를 반환합니다.
	//***************************************************************************
	int32 GetMaxSessionCount() { return _nMaxSessionCount; }

	//***************************************************************************
	// @brief 워커 스레드 수를 반환합니다.
	//***************************************************************************
	int32 GetWorkerThreadCnt() { return _nWorkerThreadCnt; }

	//***************************************************************************
	// @brief Redis 연결 풀 크기를 반환합니다.
	//***************************************************************************
	int32 GetRedisPoolSize() { return _nRedisPoolSize; }

	//***************************************************************************
	// @brief 업로드된 이미지를 저장할 로컬 디스크 경로(상대/절대 둘 다 가능)를
	//        반환합니다. CLocalFileImageStorage 생성자에 그대로 넘긴다.
	//***************************************************************************
	TCHAR* GetStorageDir() { return _tszStorageDir; }

	//***************************************************************************
	// @brief 저장된 이미지에 외부에서 접근할 때 쓸 공개 기본 URL을 반환합니다.
	// @details 예: "http://192.168.0.10:8081" — 이 뒤에 "/images/{path}"를
	//          붙여서 클라이언트에게 돌려줄 최종 URL을 만든다
	//          (FileServerMain::BuildPublicUrl() 참고). 이 값과 채팅 서버
	//          설정(ServerConfig.h::FileServerUrl)에 클라이언트가 실제로
	//          접속할 주소를 같게 맞춰야 한다.
	//***************************************************************************
	TCHAR* GetPublicBaseUrl() { return _tszPublicBaseUrl; }

	//***************************************************************************
	// @brief 업로드 가능한 최대 파일 크기(바이트)를 반환합니다.
	//***************************************************************************
	int32 GetMaxUploadBytes() { return _nMaxUploadBytes; }

	//***************************************************************************
	// @brief Redis 노드 목록을 반환합니다.
	//***************************************************************************
	CVector<CRedisNode>& GetRedisNodeVec() { return _redisNodeVec; }

	//***************************************************************************
	// @brief 지정한 ID의 Redis 노드 참조를 반환합니다.
	//***************************************************************************
	CRedisNode& GetRedisNode(int16& nID) { return _redisNodeVec[nID - 1]; }

	void PrintServerSettingInfo();

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
		value.AddMember(_T("MaxSessionCount"), _nMaxSessionCount, allocator);
		value.AddMember(_T("WorkerThreadCnt"), _nWorkerThreadCnt, allocator);
		value.AddMember(_T("RedisPoolSize"), _nRedisPoolSize, allocator);
		value.AddMember(_T("StorageDir"), _tValue(_tszStorageDir, allocator), allocator);
		value.AddMember(_T("PublicBaseUrl"), _tValue(_tszPublicBaseUrl, allocator), allocator);
		value.AddMember(_T("MaxUploadBytes"), _nMaxUploadBytes, allocator);
	}

	void FromJSON(const _tValue& value)
	{
		_tcsncpy_s(_tszServiceName, _countof(_tszServiceName), value[_T("ServiceName")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszDisplayName, _countof(_tszDisplayName), value[_T("DisplayName")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszServerName, _countof(_tszServerName), value[_T("Name")].GetString(), _TRUNCATE);
		_nServerGroupId = value[_T("GroupId")].GetInt();
		_nServerChannelId = value[_T("ChannelId")].GetInt();
		_tcsncpy_s(_tszIP, _countof(_tszIP), value[_T("IP")].GetString(), _TRUNCATE);
		_nServerPort = value[_T("Port")].GetInt();
		_nMaxSessionCount = value[_T("MaxSessionCount")].GetInt();
		_nWorkerThreadCnt = value[_T("WorkerThreadCnt")].GetInt();
		_nRedisPoolSize = value[_T("RedisPoolSize")].GetInt();
		_tcsncpy_s(_tszStorageDir, _countof(_tszStorageDir), value[_T("StorageDir")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszPublicBaseUrl, _countof(_tszPublicBaseUrl), value[_T("PublicBaseUrl")].GetString(), _TRUNCATE);
		_nMaxUploadBytes = value[_T("MaxUploadBytes")].GetInt();
	}

protected:
	void Clear();

private:
	TCHAR						_tszServiceName[MAX_BUFFER_SIZE];		// 서비스 이름
	TCHAR						_tszDisplayName[MAX_BUFFER_SIZE];		// 서비스 표시 이름
	TCHAR						_tszServerName[HOSTNAME_STRLEN];		// 서버 이름(로그/하트비트 표시용)
	uint16						_nServerGroupId;						// 서버 그룹 ID				
	uint16						_nServerChannelId;						// 서버 채널 ID

	TCHAR						_tszIP[HOSTNAME_STRLEN];				// 바인드 IP
	uint16						_nServerPort;							// 바인드 포트

	int32						_nMaxSessionCount;						// 최대 동시 HTTP 연결 수
	int32						_nWorkerThreadCnt;						// IOCP 워커 스레드 수

	int32						_nRedisPoolSize;						// Redis 커넥션 풀 크기

	TCHAR						_tszStorageDir[MAX_BUFFER_SIZE];		// 로컬 저장 경로
	TCHAR						_tszPublicBaseUrl[MAX_BUFFER_SIZE];		// 클라이언트에게 돌려줄 URL의 기본 주소
	int32						_nMaxUploadBytes;						// 업로드 최대 크기(바이트)

	CVector<CRedisNode>			_redisNodeVec;							// Redis 노드 목록
};

#endif // ndef UC_FILESERVERCONFIG_H