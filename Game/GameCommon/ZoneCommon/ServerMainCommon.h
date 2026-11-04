
//***************************************************************************
// ServerMainCommon.h : GatewayServer / ZoneServer 진입점이 함께 쓰는 도우미
//
// Ctrl+C 종료 플래그, 프로세스 초기화/정리, --dump-config 처리를 한곳에 둔다.
// 서버별로 다른 부분(설정 클래스, 명령행 항목, 대기 루프)은 각 진입점에 남긴다.
//***************************************************************************

#ifndef SERVERMAINCOMMON_H
#define SERVERMAINCOMMON_H

#include <Util/MainArgParser.h>

#include <atomic>

namespace ServerMainCommon
{
	inline std::atomic<bool>& ExitFlag()
	{
		static std::atomic<bool> flag{ false };
		return flag;
	}

	//***************************************************************************
	// @brief 콘솔 Ctrl+C / 창 닫기 등을 감지해 메인 스레드에 종료를 알립니다.
	// @details 실제 정지(Stop())는 메인 스레드에서 수행한다 — 이 핸들러는 별도 스레드 컨텍스트에서 호출된다.
	//***************************************************************************
	inline BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		switch( ctrlType )
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			ExitFlag().store(true);
			return TRUE;
		default:
			return FALSE;
		}
	}

	//***************************************************************************
	// @brief 종료 요청(Ctrl+C 등)이 들어왔는지 확인합니다.
	//***************************************************************************
	inline bool ShouldExit() { return ExitFlag().load(); }

	//***************************************************************************
	// @brief 프로세스 시작 시 공통 초기화를 합니다. (콘솔 UTF-8, 소켓, BaseGlobal, Ctrl+C 핸들러)
	//***************************************************************************
	inline void Startup()
	{
#ifdef	_MSC_VER
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
		InitUtf8Console();
		CSocketUtils::Init();
		BaseGlobal::Init();
		::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	}

	//***************************************************************************
	// @brief 프로세스 종료 직전 정리 — 실패 경로/정상 종료 경로가 공유한다.
	//***************************************************************************
	inline void Close()
	{
		BaseGlobal::Destroy();
		CSocketUtils::Clear();
	}

	//***************************************************************************
	// @brief --dump-config=경로 가 있으면 기본값 설정 파일 템플릿을 저장합니다.
	// @tparam TConfig Save(const TCHAR*)를 가진 서버 설정 클래스
	// @param args     명령행 인자
	// @param exitCode [out] 처리했을 때 프로세스 종료 코드 (성공 0, 실패 -1)
	// @return --dump-config가 주어져 처리했으면 true (호출자는 exitCode로 종료한다)
	//***************************************************************************
	template<typename TConfig>
	bool TryDumpConfig(const CMainArgParser& args, int& exitCode)
	{
		const _tstring dumpPath = args.GetString(_T("dump-config"), _T(""));
		if( dumpPath.empty() )
			return false;

		const bool saved = dumpPath != _T("1") && TConfig().Save(dumpPath.c_str());
		if( saved )
			LOG_INFO(_T("Default config saved. path=%s"), dumpPath.c_str());
		else
			LOG_ERROR(_T("--dump-config needs a writable file path. value=%s"), dumpPath.c_str());
		exitCode = saved ? 0 : -1;
		return true;
	}
}

#endif // ndef SERVERMAINCOMMON_H