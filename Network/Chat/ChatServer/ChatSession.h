
//***************************************************************************
// ChatSession.h : interface for the CChatSession class.
//
//***************************************************************************

#ifndef UC_CHATSESSION_H
#define UC_CHATSESSION_H

#include <Network/IOCP/IocpSession.h>
#include "ChatPacket.h"

#include <string>

class CChatServerMain;

//***************************************************************************
// @class CChatSession
// @brief CIocpSession을 상속받는 채팅 서버 전용 세션.
// @details
// 로그인 상태(_loggedIn/_userId)는 이 세션 객체 안에서만 관리하고, 실제
// Redis 반영(등록/삭제)은 CChatServerMain에 위임합니다 — 세션은 프로토콜 처리와
// 자기 상태만 알고, 인프라(Redis) 연동은 서버 파사드가 전담하는 구조입니다.
//***************************************************************************
class CChatSession : public CIocpSession
{
public:
	explicit CChatSession(CChatServerMain* server);
	virtual ~CChatSession() = default;

protected:
	// CIocpSession의 상위 콘텐츠 레이어 훅 오버라이드
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

public:
	//***************************************************************************
	// @brief 현재 로그인 상태를 반환합니다.
	//***************************************************************************
	bool IsLoggedIn() const { return _loggedIn; }

	//***************************************************************************
	// @brief 로그인한 유저 ID를 반환합니다(로그인 전이면 빈 문자열).
	//***************************************************************************
	const std::string& GetUserId() const { return _userId; }

	//***************************************************************************
	// @brief 이 세션이 속한 채팅 서버를 반환합니다.
	//***************************************************************************
	CChatServerMain* GetServer() const { return _server; }

	//***************************************************************************
	// @brief 로그인 완료 상태를 세션에 반영합니다.
	// @details [설계 노트] 자체 등록형 패킷 핸들러가 별도 파일의 자유 함수로
	//          분리되면서 더 이상 CChatSession의 private 멤버에 직접 접근할 수
	//          없다 — 그 대가로 캡슐화를 "핸들러가 필요로 하는 최소한의 public
	//          API"로 한 단계 완화했다. 이 메서드는 로그인 처리 핸들러
	//          (ChatLoginHandler.cpp)만 호출하는 것을 의도한 좁은 용도의 API이며,
	//          컴파일러가 강제하지는 못하므로 컨벤션으로 지킨다.
	//***************************************************************************
	void MarkLoggedIn(std::string userId) { _userId = std::move(userId); _loggedIn = true; }

private:
	void	HandlePacket(const PacketHeader* header);

private:
	CChatServerMain* _server = nullptr;	// 뒤로 참조 — 서버 소유 세션이라 세션보다 오래 살아있음이 보장됨
	std::string		_userId;
	bool			_loggedIn = false;
};

#endif // ndef UC_CHATSESSION_H