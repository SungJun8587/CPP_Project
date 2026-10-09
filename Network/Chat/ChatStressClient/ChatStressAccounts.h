
//***************************************************************************
// ChatStressAccounts.h : interface for the CChatStressAccounts class.
//
//***************************************************************************

#ifndef UC_CHATSTRESSACCOUNTS_H
#define UC_CHATSTRESSACCOUNTS_H

#include <Crypto/CryptoUtil.h>
#include "ChatPacket.h"
#include "ChatStressConfig.h"

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>

//***************************************************************************
// @brief 가상 클라이언트 1개의 계정 정보(public_id + token)
// @details 로그인 성공 시 서버가 내려준 값이다. 토큰은 로그인마다 회전 발급되므로
//          항상 가장 최근에 받은 값을 보관한다.
//***************************************************************************
struct ChatStressAccount
{
	std::array<BYTE, kPublicIdBytes>	publicId{};	// 서버가 부여한 계정 식별자
	std::array<BYTE, kTokenBytes>		token{};	// 재접속용 토큰(로그인마다 회전 발급)
};

//***************************************************************************
// @brief 가상 클라이언트별 계정(public_id+token) 저장소(싱글턴)
// @details --accounts 파일을 지정하면 실행 종료 시 각 클라이언트의 최신
//          public_id/token을 저장하고, 다음 실행에서 hasToken=1(재접속)로
//          로그인한다 -> 실행할 때마다 DB에 신규 계정이 쌓이는 것을 막는다.
//          토큰은 로그인마다 회전 발급되므로 매 로그인 성공 시 갱신한다.
//          파일 형식: 1행 "TAG <태그>", 이후 "<idx> <public_id hex> <token hex>".
//          모든 멤버 함수는 내부 뮤텍스로 보호되어 여러 스레드에서 호출할 수 있다.
//***************************************************************************
class CChatStressAccounts
{
public:
	//***************************************************************************
	// @brief 프로세스 전역 단일 인스턴스를 반환한다.
	// @return CChatStressAccounts 싱글턴 참조
	//***************************************************************************
	static CChatStressAccounts& Instance() { static CChatStressAccounts inst; return inst; }

	bool	Load(const _tstring& path);
	bool	Save(const _tstring& path);
	bool	SaveIfDirty(const _tstring& path);

	bool	Find(int index, ChatStressAccount& out);
	void	Update(int index, const ChatStressAccount& account);

private:
	std::mutex							_mutex;		// _accounts 보호
	std::map<int, ChatStressAccount>	_accounts;	// 클라이언트 번호 -> 계정
	std::atomic<bool>					_dirty{ false };	// 마지막 저장 이후 계정이 바뀌었는지 여부
};

#endif // ndef UC_CHATSTRESSACCOUNTS_H