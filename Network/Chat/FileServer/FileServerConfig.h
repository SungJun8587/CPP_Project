
//***************************************************************************
// FileServerConfig.h : interface for the CFileServerConfig class.
//
//***************************************************************************

#ifndef UC_FILESERVERCONFIG_H
#define UC_FILESERVERCONFIG_H

#include "ServerConfig.h"

//***************************************************************************
// @brief 파일 서버 설정 정보 관리 클래스
// @details JSON 기반의 설정(바인드 IP/포트, Redis 노드, 저장 경로, 공개 URL,
//          업로드 크기 제한 등)을 로드하고 관리합니다.
// @details [수정 — 리팩터링] CServerConfig는 이제 필드+게터만 제공하고
//          (ServerConfig.h 상단 설명 참고), Init()/ToJSON()/FromJSON()/
//          PrintServerSettingInfo()는 이 클래스가 직접 구현한다. 이
//          클래스는 베이스 필드 중 ServerName/IP/Port/MaxSessionCount/
//          WorkerThreadCnt/RedisNodeVec만 실제로 읽고 쓴다 — ServiceName/
//          DisplayName/GroupId/ChannelId/KeepAliveSec/ServerNodeVec/
//          DBNodeVec은 파일 서버 개념에 없는 값이라 아예 건드리지 않는다
//          (그 필드들은 상속은 받지만 항상 기본값 그대로 남는다). 그래서
//          파일 서버 JSON 설정엔 그 키들이 없어도 된다.
//***************************************************************************
class CFileServerConfig : public CServerConfig, public CSingleton<CFileServerConfig>
{
public:
	CFileServerConfig();
	virtual ~CFileServerConfig();

	bool Init(const TCHAR* tszServerInfo);

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
	//          설정(CChatServerConfig::GetFileServerUrl())에 클라이언트가
	//          실제로 접속할 주소를 같게 맞춰야 한다.
	//***************************************************************************
	TCHAR* GetPublicBaseUrl() { return _tszPublicBaseUrl; }

	//***************************************************************************
	// @brief 업로드 가능한 최대 파일 크기(바이트)를 반환합니다.
	// @details [수정] int32 -> int64로 확장. int32는 ~2.1GB가 상한이라
	//          "1GB 이상(동영상 등)" 대용량 업로드를 지원하려면 2GB를
	//          넘는 값을 설정할 수 있어야 한다.
	//***************************************************************************
	int64 GetMaxUploadBytes() { return _nMaxUploadBytes; }

	void PrintServerSettingInfo();

	//***************************************************************************
	// @brief 서버 설정 객체를 JSON 형태로 직렬화합니다.
	//***************************************************************************
	void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
	{
		value.SetObject();
		value.AddMember(_T("Name"), _tValue(_tszServerName, allocator), allocator);
		value.AddMember(_T("IP"), _tValue(_tszIP, allocator), allocator);
		value.AddMember(_T("Port"), _nServerPort, allocator);
		value.AddMember(_T("MaxSessionCount"), _nMaxSessionCount, allocator);
		value.AddMember(_T("WorkerThreadCnt"), _nWorkerThreadCnt, allocator);
		value.AddMember(_T("RedisPoolSize"), _nRedisPoolSize, allocator);
		value.AddMember(_T("StorageDir"), _tValue(_tszStorageDir, allocator), allocator);
		value.AddMember(_T("PublicBaseUrl"), _tValue(_tszPublicBaseUrl, allocator), allocator);
		value.AddMember(_T("MaxUploadBytes"), static_cast<int64>(_nMaxUploadBytes), allocator);
	}

	//***************************************************************************
	// @brief JSON 객체로부터 서버 설정 정보를 역직렬화합니다.
	//***************************************************************************
	void FromJSON(const _tValue& value)
	{
		_tcsncpy_s(_tszServerName, _countof(_tszServerName), value[_T("Name")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszIP, _countof(_tszIP), value[_T("IP")].GetString(), _TRUNCATE);
		_nServerPort = value[_T("Port")].GetInt();
		_nMaxSessionCount = value[_T("MaxSessionCount")].GetInt();
		_nWorkerThreadCnt = value[_T("WorkerThreadCnt")].GetInt();
		_nRedisPoolSize = value[_T("RedisPoolSize")].GetInt();
		_tcsncpy_s(_tszStorageDir, _countof(_tszStorageDir), value[_T("StorageDir")].GetString(), _TRUNCATE);
		_tcsncpy_s(_tszPublicBaseUrl, _countof(_tszPublicBaseUrl), value[_T("PublicBaseUrl")].GetString(), _TRUNCATE);
		_nMaxUploadBytes = value[_T("MaxUploadBytes")].GetInt64();
	}

private:
	int32					_nRedisPoolSize;						// Redis 커넥션 풀 크기

	TCHAR					_tszStorageDir[MAX_BUFFER_SIZE];		// 로컬 저장 경로
	TCHAR					_tszPublicBaseUrl[MAX_BUFFER_SIZE];		// 클라이언트에게 돌려줄 URL의 기본 주소
	int64					_nMaxUploadBytes;						// 업로드 최대 크기(바이트) — int64(2GB를 넘는 대용량 업로드 지원 목적)
};

#endif // ndef UC_FILESERVERCONFIG_H