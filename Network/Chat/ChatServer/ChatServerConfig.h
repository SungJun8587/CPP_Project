
//***************************************************************************
// ChatServerConfig.h : interface for the CChatServerConfig class.
//
//***************************************************************************

#ifndef CHATSERVERCONFIG_H
#define CHATSERVERCONFIG_H

#include <ServerConfig.h>
#include <Memory/Singleton.h>
#include "ChatServerOptions.h"

//***************************************************************************
// @brief 채팅 서버 설정 정보 관리 클래스.
// @details CServerConfig는 필드+게터만 제공하고(ServerConfig.h 상단 설명 참고),
//          Init()/Save()/PrintServerSettingInfo()는 이 클래스가 전부 직접
//          구현한다. 읽기는 CJSONConfigFieldReader(타입/범위 검증), 쓰기는
//          CJSONConfigFieldWriter를 쓴다 —
//          베이스 필드(ServiceName/DisplayName/ServerName/GroupId/
//          ChannelId/IP/Port/KeepAliveSec/MaxSessionCount/WorkerThreadCnt/
//          ServerNodeVec/DBNodeVec/RedisNodeVec)와 이 클래스 고유 7개
//          필드(RedisPoolSize/DbWorkerThreadCnt/HeartbeatTtlSec/
//          HeartbeatIntervalSec/FileServerUrl/MaxRoomsPerOwner/EnableRelay)를 같은
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

	//***************************************************************************
	// @brief JSON 설정 파일로부터 서버 구성 정보를 읽어와 초기화합니다.
	// @param tszServerInfo JSON 설정 파일 경로
	// @return 모든 필드가 타입/범위 검증을 통과하면 true, 파일을 못 읽거나 하나라도 위반하면 false
	//***************************************************************************
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
	bool IsRelayEnabled() { return _nEnableRelay != 0; }

	void PrintServerSettingInfo();

	//***************************************************************************
	// @brief 로드된 설정 값을 CChatServerMain::Start()에 넘길 구동 옵션으로 옮겨 담습니다.
	// @details 설정 파일에 없는 항목(idleTimeoutMs 등)은 전달받은 options의 값을 그대로 둔다.
	//          벡터 항목은 대입으로 채우므로 같은 options에 여러 번 호출해도 결과가 같다.
	//***************************************************************************
	void ToOptions(SChatServerOptions& options) const;

	//***************************************************************************
	// @brief 현재 설정 값을 JSON 파일로 저장합니다.
	// @param tszServerInfo 저장할 JSON 설정 파일 경로
	// @return 성공 시 true, 실패 시 false(기존 파일은 그대로 남음)
	// @details 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키(UseTestMode 등)와
	//          키 순서를 보존한 채 알고 있는 필드의 값만 교체한다.
	//***************************************************************************
	bool Save(const TCHAR* tszServerInfo);

private:
	int32						_nRedisPoolSize;					// Redis 커넥션 풀 크기
	int32						_nDbWorkerThreadCnt;				// DB 처리 워커 스레드 수
	int32						_nHeartbeatTtlSec;					// 하트비트 TTL(초)
	int32						_nHeartbeatIntervalSec;				// 하트비트 갱신 주기(초)

	TCHAR						_tszFileServerUrl[HOSTNAME_STRLEN];	// 클라이언트에게 알려줄 파일 서버 주소(프로필 이미지 업로드용, 채팅 서버와 별개 프로세스)
	int32						_nMaxRoomsPerOwner;					// 1인당 생성 가능한 방 개수 상한(0 이하면 무제한)
	int32						_nEnableRelay;						// 1이면 채널 간 전달 계층 사용(Redis Pub/Sub), 0이면 이 프로세스 안에서만 전달
};

#endif // ndef CHATSERVERCONFIG_H