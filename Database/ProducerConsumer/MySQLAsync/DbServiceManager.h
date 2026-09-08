
//***************************************************************************
// DbServiceManager.h : interface for the CDbServiceManager class.
//
//***************************************************************************

#ifndef UC_DBSERVICEMANAGER_H
#define UC_DBSERVICEMANAGER_H

#include <DB/MySQL/MySQLAsyncSrv.h>
#include <memory>

//***************************************************************************
// @class CDbServiceManager
// @brief 멤버/게임/로그 등 도메인별 CMySQLAsyncSrv 인스턴스를 소유하고 이름 있는 접근자로 노출하는 프로세스 전역 매니저.
// @details
// [초기화 순서] 이 매니저 자체는 "최초 사용 시점 생성"(Instance() 최초 호출 시 생성)이라 다른 정적 초기화식과의 순서 경쟁에서 자유롭다.
// 다만 각 CMySQLAsyncSrv 인스턴스는 생성만 될 뿐 StartService()가 자동으로 불리지는 않는다.
// 실제 DB 접속/워커 스레드 기동은 호출부에서 명시적으로 MemberDB().StartService(...) 등을 호출해야 한다.
//***************************************************************************
class CDbServiceManager
{
public:
	//***************************************************************************
	// @brief 프로세스 전역 매니저 인스턴스를 반환합니다(최초 사용 시점 생성).
	// @return CDbServiceManager& 프로세스 싱글턴 인스턴스 참조
	//***************************************************************************
	static CDbServiceManager& Instance()
	{
		static CDbServiceManager instance;
		return instance;
	}

	CDbServiceManager(const CDbServiceManager&) = delete;
	CDbServiceManager& operator=(const CDbServiceManager&) = delete;

	//***************************************************************************
	// @brief 멤버 DB 서비스 객체 참조를 반환합니다.
	// @return CMySQLAsyncSrv& 멤버 DB 서비스 인스턴스 참조
	//***************************************************************************
	CMySQLAsyncSrv& MemberDB() { return *_memberDB; }

	//***************************************************************************
	// @brief 등록된 모든 도메인 서비스를 한 번에 종료합니다.
	// @details [수정 — 재호출 시 널 역참조] 이전 버전은 _memberDB가 이미
	// reset()된 뒤 이 함수가 다시 호출되면(예: 종료 경로가 두 곳에서 호출)
	// 바로 nullptr 역참조로 크래시가 났다. 멱등하게 동작하도록 널 체크를
	// 추가한다 — 두 번째 호출부터는 아무 일도 하지 않고 조용히 반환한다.
	//***************************************************************************
	void ShutdownAll()
	{
		if( !_memberDB )
			return;

		_memberDB->Stop();
		_memberDB->Join();
		_memberDB.reset();
	}

private:
	//***************************************************************************
	// @brief CDbServiceManager 생성자
	//***************************************************************************
	CDbServiceManager()
		: _memberDB(std::make_unique<CMySQLAsyncSrv>())
	{
	}
	~CDbServiceManager() = default;

	//***************************************************************************
	// @brief DB 관련 요청(멤버, 게임, 로그 DB 등)을 비동기로 처리하는 서비스 객체
	// @note (예시) 다른 DB 서비스 추가 시
	// std::unique_ptr<CMySQLAsyncSrv> _gameDB;   // 게임 콘텐츠 DB 비동기 서비스
	// std::unique_ptr<CMySQLAsyncSrv> _logDB;    // 로그 축적 DB 비동기 서비스
	//***************************************************************************
	std::unique_ptr<CMySQLAsyncSrv> _memberDB;	// 멤버 도메인 전용 비동기 DB 서비스 객체
};

//***************************************************************************
// @brief CDbServiceManager::Instance().MemberDB()의 축약형.
// @details 매크로이므로 헤더가 include된 모든 번역 단위에 이름이 그대로 노출된다
// 다른 곳에 MEMBER_DB_ASYNC 라는 이름의 매크로/심볼이 이미 있다면 충돌하니 주의할 것.
// (예시) 도메인이 늘어나면 아래처럼 같은 패턴으로 추가하면 된다:
//   #define GAME_DB_ASYNC (CDbServiceManager::Instance().GameDB())
//   #define LOG_DB_ASYNC (CDbServiceManager::Instance().LogDB())
//***************************************************************************
#define MEMBER_DB_ASYNC (CDbServiceManager::Instance().MemberDB())

#endif // ndef UC_DBSERVICEMANAGER_H