
//***************************************************************************
// BotClientConfig.h : interface for the CBotClientConfig class and the run-option functions.
//
//***************************************************************************

#ifndef BOTCLIENTCONFIG_H
#define BOTCLIENTCONFIG_H

#include <atomic>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "BotClientOptions.h"

//***************************************************************************
// @brief 봇 클라이언트 설정 정보 관리 클래스.
// @details 읽기는 CJSONConfigFieldReader(타입/범위 검증), 쓰기는 CJSONConfigFieldWriter를 쓴다.
//          로드한 값은 ToOptions()로 SBotClientOptions에 옮겨 담아 쓴다.
//          키 이름은 SBotClientOptions 필드 이름의 PascalCase다
//          (Host/Port/Bots/ZoneId/Heroes/CharIdBase/RampMs/TickMs/MoveIntervalMs/
//           AttackIntervalMs/WorkerThreadCnt/StatsSec/DurationSec).
//          Heroes는 쉼표로 구분한 영웅 ID 문자열이다(예: "1,2,3").
//***************************************************************************
class CBotClientConfig
{
public:
	CBotClientConfig();
	virtual ~CBotClientConfig();

	//***************************************************************************
	// @brief JSON 설정 파일로부터 실행 옵션을 읽어와 초기화합니다.
	// @param tszConfigPath JSON 설정 파일 경로
	// @return 모든 필드가 타입/범위 검증을 통과하면 true, 파일을 못 읽거나 하나라도 위반하면 false
	// @details 파일에 없는 키는 기본값(SBotClientOptions)을 유지한다.
	//***************************************************************************
	bool Init(const TCHAR* tszConfigPath);

	//***************************************************************************
	// @brief 현재 설정 값을 JSON 파일로 저장합니다.
	// @param tszConfigPath 저장할 JSON 설정 파일 경로
	// @return 성공 시 true, 실패 시 false
	// @details 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를 보존한다.
	//***************************************************************************
	bool Save(const TCHAR* tszConfigPath);

	//***************************************************************************
	// @brief 로드된 설정 값을 실행 옵션으로 옮겨 담습니다.
	//***************************************************************************
	void ToOptions(SBotClientOptions& options) const;

	//***************************************************************************
	// @brief 로드된 설정 정보를 로그로 출력합니다.
	//***************************************************************************
	void PrintSettingInfo();

private:
	_tstring	_host;
	uint16		_port;
	int32		_botCount;
	int32		_zoneId;
	_tstring	_heroes;
	int32		_charIdBase;
	int32		_rampMs;
	int32		_tickMs;
	int32		_moveIntervalMs;
	int32		_attackIntervalMs;
	int32		_workerThreadCnt;
	int32		_statsSec;
	int32		_durationSec;
};

extern SBotClientOptions	g_cfg;		// 실행 옵션(전역)
extern std::atomic<bool>	g_stop;		// true이면 모든 스레드가 종료 절차로 들어간다

void	PrintUsage();
bool	ParseArgs(int argc, wchar_t** argv, int& exitCode);

#endif // ndef BOTCLIENTCONFIG_H