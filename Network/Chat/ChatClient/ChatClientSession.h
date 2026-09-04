
//***************************************************************************
// ChatClientSession.h : interface for the CChatClientSession class.
//
//***************************************************************************

#ifndef UC_CHATCLIENTSESSION_H
#define UC_CHATCLIENTSESSION_H

#include <Crypto/CryptoUtil.h>
#include <Network/IOCP/IocpSession.h>
#include "ChatPacket.h"
#include "ChatClientMain.h"
#include "ChatClientPacketDispatcher.h"

#include <string>
#include <array>
#include <cstring>
#include <algorithm>

class CChatClientMain;

//***************************************************************************
// @class CChatClientSession
// @brief CIocpSession을 상속받는 채팅 클라이언트 전용 세션(1개 연결 = 1개 인스턴스).
// @details
// 연결이 실제로 완료되면(OnConnected) 곧바로 LoginReq를 보내고, 서버로부터
// 오는 LoginRes/Chat 패킷을 파싱해 CChatClientMain의 콜백으로 이관합니다.
// Redis는 이 클라이언트가 전혀 알 필요가 없습니다(서버 전담 영역).
//***************************************************************************
class CChatClientSession : public CIocpSession
{
public:
	//***************************************************************************
	// @param userId 로그인에 사용할 닉네임
	// @param hasToken true면 token으로 재접속 시도, false면 신규 가입 시도
	// @param token 재접속 토큰 원문(hasToken==false면 무시됨)
	// @param client 이 세션을 소유한 클라이언트 파사드
	//***************************************************************************
	CChatClientSession(std::string userId, bool hasToken,
		std::array<BYTE, kTokenBytes> token, CChatClientMain* client);
	virtual ~CChatClientSession() = default;

public:
	//***************************************************************************
	// @brief 채팅 메시지를 서버로 전송합니다.
	// @details 아직 로그인 완료 전(LoginRes 수신 전)이어도 호출은 가능합니다 —
	//          서버 쪽 HandleChat()이 _loggedIn 여부로 무시하므로, 굳이 클라이언트가
	//          이중으로 상태를 체크할 필요는 없습니다(기본 뼈대 단순화).
	//***************************************************************************
	void	SendChat(const std::string& message);

	//***************************************************************************
	// @brief 서버에 랜덤 닉네임 생성을 요청합니다. 로그인 여부와 무관하게 호출 가능합니다.
	//***************************************************************************
	void	SendNicknameGenerateReq();

	//***************************************************************************
	// @brief 이 세션을 소유한 클라이언트 파사드를 반환합니다.
	// @details [설계 노트] 자체 등록형 패킷 핸들러(ChatClientLoginHandler.cpp 등)가
	//          별도 파일의 자유 함수로 분리되면서 더 이상 이 세션의 private 멤버에
	//          직접 접근할 수 없다 — 그 핸들러들이 필요로 하는 유일한 접근점이라
	//          public으로 열어둔다.
	//***************************************************************************
	CChatClientMain* GetClient() const { return _client; }

protected:
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

private:
	void	HandlePacket(const PacketHeader* header);

private:
	std::string								_userId;
	bool									_hasToken = false;
	std::array<BYTE, kTokenBytes>	_token{};
	CChatClientMain* _client = nullptr;	// 뒤로 참조 — Client가 세션보다 오래 살아있음을 CChatClientMain::Disconnect()가 보장
};

#endif // ndef UC_CHATCLIENTSESSION_H