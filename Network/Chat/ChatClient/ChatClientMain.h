
//***************************************************************************
// ChatClientMain.h : interface for the CChatClientMain class.
//
//***************************************************************************

#ifndef UC_CHATCLIENTMAIN_H
#define UC_CHATCLIENTMAIN_H

#include <Network/NetworkCommon.h>
#include "ChatPacket.h"	
#include <Crypto/CryptoUtil.h>

#include <string>
#include <functional>
#include <memory>
#include <array>

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
// [설계 변경 — "프로필 이름"과 서버 닉네임의 분리]
// users.nickname이 더 이상 계정 식별자가 아니게 되면서(create_chat_db.sql,
// public_id 참고), Connect()에 넘기는 문자열(userId)의 역할이 바뀌었다:
//   - 이 값은 이제 "로컬 저장 파일을 찾기 위한 키"(프로필 이름)일 뿐이다.
//   - 로컬에 이 이름으로 저장된 계정(public_id+token)이 있으면 재접속을
//     시도하고, 없으면 이 이름을 "원하는 신규 닉네임"으로 회원가입을
//     시도한다(최초 1회에 한해 이 값이 서버 닉네임과 실제로 일치함).
//   - 이후 서버 쪽에서 /nick 등으로 닉네임이 바뀌어도 이 로컬 프로필
//     이름/저장 파일은 전혀 영향받지 않는다 — 재접속은 항상 저장된
//     public_id를 기준으로 하기 때문이다.
//
// [재접속 계정 정보 로컬 저장 — 보안 주의]
// Connect() 시 프로필 이름에 대응하는 계정 파일이 로컬에 있으면 자동으로
// 읽어 재접속을 시도하고, 로그인 성공(가입/재접속 모두) 시 서버가 발급한
// public_id와 회전 발급한 새 토큰을 그 파일에 저장한다. 파일은 평문(16진
// 텍스트)으로 저장되므로 같은 PC의 다른 사용자/프로세스가 파일에 접근할
// 수 있는 환경이라면 안전하지 않다 — 실서비스에서는 Windows DPAPI
// (CryptProtectData)로 암호화해 저장하는 것을 권장한다(이 기본 뼈대엔 미적용).
//***************************************************************************
class CChatClientMain
{
public:
	CChatClientMain() = default;
	~CChatClientMain();

	CChatClientMain(const CChatClientMain&) = delete;
	CChatClientMain& operator=(const CChatClientMain&) = delete;

	//***************************************************************************
	// @brief 서버에 접속을 게시합니다. 로컬에 이 프로필 이름으로 저장된 계정
	//        (public_id+token)이 있으면 자동으로 읽어 재접속을 시도하고,
	//        없으면 이 이름을 원하는 닉네임으로 신규 가입을 시도합니다.
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

	//***************************************************************************
	// @brief 서버에 닉네임 변경을 요청합니다. 아직 연결/로그인 전이면 조용히
	//        무시됩니다(서버 쪽 HandleChangeNicknameReq()도 동일하게 확인).
	// @details [설계 변경] 로컬 저장 파일은 프로필 이름(Connect()에 넘긴 값)
	//          기준이지 서버 닉네임 기준이 아니므로, 닉네임 변경 성공과
	//          무관하게 로컬 파일은 그대로 둔다(더 이상 파일 이전이 필요
	//          없음 — public_id는 이 요청으로 절대 바뀌지 않는다).
	//***************************************************************************
	void RequestChangeNickname(const std::string& newNickname);

	//***************************************************************************
	// @brief 서버에 방 입장을 요청합니다. 아직 연결/로그인 전이면 조용히 무시됩니다.
	//***************************************************************************
	void RequestRoomEnter(int32 roomId);

	//***************************************************************************
	// @brief 서버에 방 퇴장(로비 복귀)을 요청합니다. 아직 연결/로그인 전이면 조용히 무시됩니다.
	//***************************************************************************
	void RequestRoomLeave();

public:
	using LoginResultHandler = std::function<void(bool success, ELoginResult reason, const std::string& nickname)>;
	using ChatMessageHandler = std::function<void(const std::string& senderNickname, const std::string& message)>;
	using DisconnectedHandler = std::function<void()>;
	using NicknameGeneratedHandler = std::function<void(const std::string& nickname)>;
	using NicknameChangeResultHandler = std::function<void(bool success, ELoginResult reason, const std::string& newNickname)>;
	using RoomEnterResultHandler = std::function<void(bool success, ERoomResult reason, int32 roomId, int32 roomUserCount)>;
	using RoomLeaveResultHandler = std::function<void(bool success, int32 roomId, int32 roomUserCount)>;
	using RoomUserCountChangedHandler = std::function<void(int32 roomId, int32 userCount)>;

	void SetOnLoginResult(LoginResultHandler handler) { _onLoginResult = std::move(handler); }
	void SetOnChatMessage(ChatMessageHandler handler) { _onChatMessage = std::move(handler); }
	void SetOnDisconnected(DisconnectedHandler handler) { _onDisconnected = std::move(handler); }
	void SetOnNicknameGenerated(NicknameGeneratedHandler handler) { _onNicknameGenerated = std::move(handler); }
	void SetOnNicknameChangeResult(NicknameChangeResultHandler handler) { _onNicknameChangeResult = std::move(handler); }
	void SetOnRoomEnterResult(RoomEnterResultHandler handler) { _onRoomEnterResult = std::move(handler); }
	void SetOnRoomLeaveResult(RoomLeaveResultHandler handler) { _onRoomLeaveResult = std::move(handler); }
	void SetOnRoomUserCountChanged(RoomUserCountChangedHandler handler) { _onRoomUserCountChanged = std::move(handler); }

public:
	// CChatClientSession에서 호출하는 콜백들 (IOCP 워커 스레드에서 호출됨 — 클래스 상단 주석 참고)
	//***************************************************************************
	// @brief 로그인 응답 수신 시 CChatClientSession이 호출합니다.
	// @details 성공 시 서버가 반환한 public_id와 회전 발급한 새 토큰을 로컬
	//          파일(프로필 이름 기준)에 저장한 뒤, 앱 쪽 콜백에는
	//          success/reason/nickname을 전달한다(저장은 내부 구현 세부사항).
	//          nickname은 서버가 채워 보낸 값을 그대로 전달 — 실패 상황에
	//          따라 빈 문자열일 수 있다(ChatPacket.h::LoginResPacket 참고).
	//***************************************************************************
	void OnLoginResult(bool success, ELoginResult reason, const std::string& nickname,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& newToken);
	void OnChatReceived(const std::string& senderNickname, const std::string& message);
	void OnSessionClosed();
	void OnNicknameGenerated(const std::string& nickname);

	//***************************************************************************
	// @brief 닉네임 변경 응답 수신 시 CChatClientSession이 호출합니다.
	// @details [설계 변경] public_id가 바뀌지 않으므로 로컬 파일을 건드릴
	//          필요가 없다 — 성공/실패 결과만 앱 쪽 콜백으로 전달한다.
	//***************************************************************************
	void OnNicknameChangeResult(bool success, ELoginResult reason, const std::string& newNickname);

	//***************************************************************************
	// @brief 방 입장/퇴장 응답, 방 인원수 변경 알림 수신 시 호출됩니다.
	//***************************************************************************
	void OnRoomEnterResult(bool success, ERoomResult reason, int32 roomId, int32 roomUserCount);
	void OnRoomLeaveResult(bool success, int32 roomId, int32 roomUserCount);
	void OnRoomUserCountChanged(int32 roomId, int32 userCount);

private:
	static _tstring TokenFilePath(const std::string& profileName);

	//***************************************************************************
	// @brief 로컬 계정 파일(public_id+token)을 읽습니다. 파일이 없거나
	//        형식이 잘못됐으면 false.
	//***************************************************************************
	static bool LoadAccount(const std::string& profileName,
		std::array<BYTE, kPublicIdBytes>& outPublicId,
		std::array<BYTE, kTokenBytes>& outToken);

	//***************************************************************************
	// @brief 로컬 계정 파일(public_id+token)에 저장(덮어쓰기)합니다.
	//***************************************************************************
	static void SaveAccount(const std::string& profileName,
		const std::array<BYTE, kPublicIdBytes>& publicId,
		const std::array<BYTE, kTokenBytes>& token);

private:
	CIocpCoreRef				_iocpCore;
	CIocpClientServiceRef		_service;
	std::weak_ptr<CChatClientSession>	_session;	// SendChat()에서 바로 쓰기 위한 핸들 (Connect() 시점에 1회 설정)
	std::string					_userId;			// 로컬 저장 파일을 찾기 위한 "프로필 이름"(서버 닉네임과 다를 수 있음 — 클래스 상단 설명 참고)

	LoginResultHandler		_onLoginResult;
	ChatMessageHandler		_onChatMessage;
	DisconnectedHandler		_onDisconnected;
	NicknameGeneratedHandler	_onNicknameGenerated;
	NicknameChangeResultHandler	_onNicknameChangeResult;
	RoomEnterResultHandler		_onRoomEnterResult;
	RoomLeaveResultHandler		_onRoomLeaveResult;
	RoomUserCountChangedHandler	_onRoomUserCountChanged;
};


#endif // ndef UC_CHATCLIENTMAIN_H