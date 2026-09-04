
//***************************************************************************
// ChatClientMain.h : interface for the CChatClientMain class.
//
//***************************************************************************

#ifndef UC_CHATCLIENTMAIN_H
#define UC_CHATCLIENTMAIN_H

#include <Crypto/CryptoUtil.h>
#include <Network/NetworkCommon.h>
#include "ChatPacket.h"		
#include "ChatClientSession.h"

#include <string>
#include <functional>
#include <memory>
#include <array>
#include <fstream>
#include <sstream>

class CChatClientSession;

//***************************************************************************
// @class CChatClientMain
// @brief CIocpClientService 하나로 서버에 접속해 로그인/채팅을 주고받는
//        채팅 클라이언트 파사드.
// @details
// [중요] 콜백 실행 스레드: SetOnLoginResult/SetOnChatMessage/SetOnDisconnected로
// 등록한 콜백은 IOCP 워커 스레드에서 직접 호출됩니다(CChatClientSession의
// OnConnected/OnRecv/OnDisconnected가 부르는 그대로). 콘솔 데모처럼 단순
// 출력 정도면 문제없지만, UI 스레드 갱신 등 스레드 제약이 있는 작업을 하려면
// 콜백 안에서 자체적으로 UI 스레드로 마샬링해야 합니다(예: CJobQueue 활용).
//
// [사용 순서 가정] Connect()는 SendChat()/Disconnect()와 동시에 호출되지
// 않는다고 가정합니다(일반적인 "먼저 연결, 그 다음 사용" 흐름). 연결 도중
// 재호출 등 동시성이 필요하면 _session 접근에 락을 추가해야 합니다.
//
// [재접속 토큰 로컬 저장 — 보안 주의]
// Connect() 시 닉네임에 대응하는 토큰 파일이 로컬에 있으면 자동으로 읽어
// 재접속을 시도하고, 로그인 성공(가입/재접속 모두) 시 서버가 회전 발급한
// 새 토큰을 그 파일에 덮어쓴다. 파일은 평문(16진 텍스트)으로 저장되므로
// 같은 PC의 다른 사용자/프로세스가 파일에 접근할 수 있는 환경이라면 안전하지
// 않다 — 실서비스에서는 Windows DPAPI(CryptProtectData)로 암호화해 저장하는
// 것을 권장한다(이 기본 뼈대엔 미적용).
//***************************************************************************
class CChatClientMain
{
public:
	CChatClientMain() = default;
	~CChatClientMain();

	CChatClientMain(const CChatClientMain&) = delete;
	CChatClientMain& operator=(const CChatClientMain&) = delete;

	//***************************************************************************
	// @brief 서버에 접속을 게시합니다. 로컬에 이 닉네임의 재접속 토큰 파일이
	//        있으면 자동으로 읽어 재접속을 시도하고, 없으면 신규 가입을 시도합니다.
	// @return "게시 시도" 성공 여부입니다 — CIocpClientService::Start()와 동일하게
	//         실제 TCP 연결 완료를 보장하지 않습니다. 로그인 성공 여부까지 알고
	//         싶다면 SetOnLoginResult() 콜백을 쓰세요.
	//***************************************************************************
	bool Connect(const _tstring& serverIp, uint16 serverPort, std::string userId, uint32 workerThreadCount = 0);

	//***************************************************************************
	// @brief 연결을 끊고 워커 스레드까지 정리될 때까지 블로킹 대기합니다.
	//***************************************************************************
	void Disconnect();

	//***************************************************************************
	// @brief 채팅 메시지를 전송합니다. 아직 연결/로그인 전이면 조용히 무시됩니다.
	//***************************************************************************
	void SendChat(const std::string& message);

	//***************************************************************************
	// @brief 서버에 랜덤 닉네임 생성을 요청합니다. 아직 연결 전이면 조용히 무시됩니다.
	//***************************************************************************
	void RequestNicknameGeneration();

public:
	using LoginResultHandler	= std::function<void(bool success, ELoginResult reason)>;
	using ChatMessageHandler	= std::function<void(const std::string& message)>;
	using DisconnectedHandler	= std::function<void()>;
	using NicknameGeneratedHandler	= std::function<void(const std::string& nickname)>;

	void SetOnLoginResult(LoginResultHandler handler) { _onLoginResult = std::move(handler); }
	void SetOnChatMessage(ChatMessageHandler handler) { _onChatMessage = std::move(handler); }
	void SetOnDisconnected(DisconnectedHandler handler) { _onDisconnected = std::move(handler); }
	void SetOnNicknameGenerated(NicknameGeneratedHandler handler) { _onNicknameGenerated = std::move(handler); }

public:
	// CChatClientSession에서 호출하는 콜백들 (IOCP 워커 스레드에서 호출됨 — 클래스 상단 주석 참고)
	// newToken: 로그인 성공 시 서버가 회전 발급한 새 토큰 — 내부적으로 로컬
	// 파일에 저장한 뒤, 앱 쪽 콜백(_onLoginResult)에는 success/reason만 전달한다
	// (토큰 저장은 이 클래스가 전담하는 내부 구현 세부사항).
	void OnLoginResult(bool success, ELoginResult reason, const std::array<BYTE, kTokenBytes>& newToken);
	void OnChatReceived(const std::string& message);
	void OnSessionClosed();
	void OnNicknameGenerated(const std::string& nickname);

private:
	static std::string TokenFilePath(const std::string& nickname);
	static bool LoadToken(const std::string& nickname, std::array<BYTE, kTokenBytes>& outToken);
	static void SaveToken(const std::string& nickname, const std::array<BYTE, kTokenBytes>& token);

private:
	CIocpCoreRef				_iocpCore;
	CIocpClientServiceRef		_service;
	std::weak_ptr<CChatClientSession>	_session;	// SendChat()에서 바로 쓰기 위한 핸들 (Connect() 시점에 1회 설정)
	std::string					_userId;			// 현재 로그인 시도 중인/완료된 닉네임 (토큰 저장 시 파일명 결정에 사용)

	LoginResultHandler		_onLoginResult;
	ChatMessageHandler		_onChatMessage;
	DisconnectedHandler		_onDisconnected;
	NicknameGeneratedHandler	_onNicknameGenerated;
};

#endif // ndef UC_CHATCLIENTMAIN_H
