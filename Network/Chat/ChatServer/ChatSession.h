
//***************************************************************************
// ChatSession.h : interface for the CChatSession class.
//
//***************************************************************************

#ifndef UC_CHATSESSION_H
#define UC_CHATSESSION_H

#include <Network/IOCP/IocpSession.h>
#include "ChatPacket.h"

#include <string>
#include <unordered_map>

class CChatServerMain;

//***************************************************************************
// @class CChatSession
// @brief CIocpSession을 상속받는 채팅 서버 전용 세션.
// @details
// 로그인 상태(_loggedIn/_userId)는 이 세션 객체 안에서만 관리하고, 실제
// Redis 반영(등록/삭제)은 CChatServer에 위임합니다 — 세션은 프로토콜 처리와
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

private:
	void	HandlePacket(const PacketHeader* header);

	// [설계 변경] 인스턴스 메서드 대신 static으로 선언 — 핸들러 테이블에
	// 순수 함수 포인터로 등록하기 위함(std::function 오버헤드 없이 O(1)
	// 테이블 조회 + 직접 호출). static 멤버 함수는 같은 클래스의 private
	// 멤버에 접근 가능하므로 캡슐화는 그대로 유지된다.
	static void	HandleLoginReq(CChatSession& session, const PacketHeader* header);
	static void	HandleChat(CChatSession& session, const PacketHeader* header);

private:
	CChatServerMain*	_server = nullptr;	// 뒤로 참조 — 서버 소유 세션이라 세션보다 오래 살아있음이 보장됨
	std::string			_userId;
	bool				_loggedIn = false;
};

#endif // ndef UC_CHATSESSION_H
