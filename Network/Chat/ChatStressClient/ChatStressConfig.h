
//***************************************************************************
// ChatStressConfig.h : interface for the CChatStressConfig class and the run-option functions.
//
//***************************************************************************

#ifndef CHATSTRESSCONFIG_H
#define CHATSTRESSCONFIG_H

#include <atomic>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <random>

#include "ChatStressOptions.h"

//***************************************************************************
// @brief 스트레스 클라이언트 설정 정보 관리 클래스.
// @details 채팅 서버의 CChatServerConfig와 같은 형태다. 읽기는 CJSONConfigFieldReader
//          (타입/범위 검증), 쓰기는 CJSONConfigFieldWriter를 쓴다. 로드한 값은
//          ToOptions()로 SChatStressOptions에 옮겨 담아 쓴다.
//          키 이름은 SChatStressOptions 필드 이름의 PascalCase다.
//***************************************************************************
class CChatStressConfig
{
public:
	CChatStressConfig();
	virtual ~CChatStressConfig();

	//***************************************************************************
	// @brief JSON 설정 파일로부터 실행 옵션을 읽어와 초기화합니다.
	// @param tszConfigPath JSON 설정 파일 경로
	// @return 모든 필드가 타입/범위 검증을 통과하면 true, 파일을 못 읽거나 하나라도 위반하면 false
	// @details 파일에 없는 키는 기본값(SChatStressOptions)을 유지한다.
	//***************************************************************************
	bool Init(const TCHAR* tszConfigPath);

	//***************************************************************************
	// @brief 현재 설정 값을 JSON 파일로 저장합니다.
	// @param tszConfigPath 저장할 JSON 설정 파일 경로
	// @return 성공 시 true, 실패 시 false(기존 파일은 그대로 남음)
	// @details 기존 파일이 있으면 먼저 읽어 와서 이 클래스가 모르는 키와 키 순서를 보존한다.
	//***************************************************************************
	bool Save(const TCHAR* tszConfigPath);

	//***************************************************************************
	// @brief 로드된 설정 값을 실행 옵션으로 옮겨 담습니다.
	// @details 접두어는 ASCII만 허용하므로 128 이상의 문자는 '_'로 치환한다.
	//***************************************************************************
	void ToOptions(SChatStressOptions& options) const;

	//***************************************************************************
	// @brief 로드된 설정 정보를 로그로 출력합니다.
	//***************************************************************************
	void PrintSettingInfo();

private:
	_tstring	_host;
	uint16		_port;
	int32		_clients;
	int32		_workers;
	int32		_schedThreads;
	int32		_durationSec;
	double		_rampPerSec;
	int32		_chatIntervalMs;
	int32		_listIntervalMs;
	int32		_memberIntervalMs;
	int32		_countIntervalMs;
	int32		_roomId;
	int32		_roomSpread;
	int32		_msgBytes;
	int32		_pageSize;
	_tstring	_prefix;
	_tstring	_accountsFile;
};

extern SChatStressOptions	g_cfg;		// 실행 옵션(전역)
extern std::atomic<bool>	g_stop;		// true이면 모든 스레드가 종료 절차로 들어간다
extern std::string			g_runTag;	// 닉네임/RTT 마커에 쓰이는 4자리 태그

void	PrintUsage();
bool	ParseArgs(int argc, wchar_t** argv, int& exitCode);
void	GenerateRunTag();

#endif // ndef CHATSTRESSCONFIG_H