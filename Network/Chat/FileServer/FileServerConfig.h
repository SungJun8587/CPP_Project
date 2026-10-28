
//***************************************************************************
// FileServerConfig.h : interface for the CFileServerConfig class.
//
//***************************************************************************

#ifndef FILESERVERCONFIG_H
#define FILESERVERCONFIG_H

#include <ServerConfig.h>
#include <Memory/Singleton.h>
#include "FileServerOptions.h"

//***************************************************************************
// @brief 파일 서버 설정 정보 관리 클래스
// @details JSON 기반의 설정(바인드 IP/포트, Redis 노드, 저장 경로, 공개 URL,
//          업로드 크기 제한 등)을 로드하고 관리합니다.
// @details CServerConfig는 필드+게터만 제공하고(ServerConfig.h 상단 설명 참고),
//          Init()/Save()/PrintServerSettingInfo()는 이 클래스가 직접 구현한다.
//          읽기는 CJSONConfigFieldReader(타입/범위 검증), 쓰기는
//          CJSONConfigFieldWriter를 쓴다. 이 클래스는 베이스 필드 중 ServerName/IP/Port/MaxSessionCount/
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

	//***************************************************************************
	// @brief JSON 설정 파일로부터 파일 서버 구성 정보를 읽어와 초기화합니다.
	// @param tszServerInfo JSON 설정 파일 경로
	// @return 모든 필드가 타입/범위 검증을 통과하면 true, 파일을 못 읽거나 하나라도 위반하면 false
	//***************************************************************************
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
	// @brief 로드된 설정 값을 CFileServerMain::Start()에 넘길 구동 옵션으로 옮겨 담습니다.
	// @details 설정 파일에 없는 항목(idleTimeoutMs)은 전달받은 options의 값을 그대로 둔다.
	//***************************************************************************
	void ToOptions(SFileServerOptions& options) const;

	//***************************************************************************
	// @brief 현재 설정 값을 JSON 파일로 저장합니다.
	// @param tszServerInfo 저장할 JSON 설정 파일 경로
	// @return 성공 시 true, 실패 시 false(기존 파일은 그대로 남음)
	// @details 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를
	//          보존한 채 알고 있는 필드의 값만 교체한다.
	//***************************************************************************
	bool Save(const TCHAR* tszServerInfo);

private:
	int32					_nRedisPoolSize;						// Redis 커넥션 풀 크기

	TCHAR					_tszStorageDir[MAX_BUFFER_SIZE];		// 로컬 저장 경로
	TCHAR					_tszPublicBaseUrl[MAX_BUFFER_SIZE];		// 클라이언트에게 돌려줄 URL의 기본 주소
	int64					_nMaxUploadBytes;						// 업로드 최대 크기(바이트) — int64(2GB를 넘는 대용량 업로드 지원 목적)
};

#endif // ndef FILESERVERCONFIG_H