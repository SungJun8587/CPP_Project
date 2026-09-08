
//***************************************************************************
// ChatClientMain.cpp: implementation of the CChatClientMain class.
//
//***************************************************************************

#include "pch.h"
#include <Crypto/CryptoUtil.h>
#include "ChatClientMain.h"
#include "ChatClientSession.h"

#include <fstream>
#include <sstream>
#include <cstdio>	// ::rename, ::remove (닉네임 변경 시 토큰 파일 이전용)

//***************************************************************************
// @brief 소멸자 — 아직 연결 중이면 Disconnect()로 정리합니다.
//***************************************************************************
CChatClientMain::~CChatClientMain()
{
	Disconnect();
}

//***************************************************************************
// @brief 닉네임에 대응하는 로컬 토큰 파일 경로를 만듭니다.
//***************************************************************************
std::string CChatClientMain::TokenFilePath(const std::string& nickname)
{
	return "chat_token_" + nickname + ".dat";
}

//***************************************************************************
// @brief 로컬 토큰 파일을 읽습니다. 파일이 없거나 형식이 잘못됐으면 false.
//***************************************************************************
bool CChatClientMain::LoadToken(const std::string& nickname, std::array<BYTE, kTokenBytes>& outToken)
{
	std::ifstream in(TokenFilePath(nickname));
	if( !in.is_open() )
		return false;

	std::string hex;
	std::getline(in, hex);

	return Crypto::CCryptoUtil::FromHex(hex, outToken.data(), outToken.size());
}

//***************************************************************************
// @brief 로컬 토큰 파일에 저장(덮어쓰기)합니다.
//***************************************************************************
void CChatClientMain::SaveToken(const std::string& nickname, const std::array<BYTE, kTokenBytes>& token)
{
	std::ofstream out(TokenFilePath(nickname), std::ios::trunc);
	if( !out.is_open() )
		return;

	out << Crypto::CCryptoUtil::ToHex(token.data(), token.size());
}

//***************************************************************************
// @brief 로컬 토큰 파일을 oldNickname -> newNickname 이름으로 옮깁니다.
// @details std::rename()이 실패해도(예: oldNickname 파일이 애초에 없던
// 경우 — 아직 한 번도 로그인 성공을 못 해 토큰 파일 자체가 없는 상태에서
// 닉네임 변경에 성공하는 것도 이론상 가능함) 조용히 넘어간다 — 이건
// "다음 접속 때 재접속 대신 새 토큰을 다시 받는" 정도의 부작용만 있고
// 치명적이지 않다.
//***************************************************************************
void CChatClientMain::RenameTokenFile(const std::string& oldNickname, const std::string& newNickname)
{
	const std::string oldPath = TokenFilePath(oldNickname);
	const std::string newPath = TokenFilePath(newNickname);

	// 대상 경로에 이미 파일이 있으면 std::rename()이 플랫폼에 따라 실패할
	// 수 있다(POSIX는 덮어쓰지만 Windows CRT의 rename()은 실패한다) — 새
	// 닉네임으로 된 낡은 토큰 파일이 남아있을 가능성은 낮지만(그 닉네임을
	// 예전에 다른 계정으로 써본 적이 있어야 함), 방어적으로 먼저 지운다.
	::remove(newPath.c_str());
	::rename(oldPath.c_str(), newPath.c_str());
}

//***************************************************************************
// @brief 서버 접속을 게시합니다. 로컬에 이 닉네임의 토큰 파일이 있으면
//        재접속으로, 없으면 신규 가입으로 시도합니다.
//***************************************************************************
bool CChatClientMain::Connect(const _tstring& serverIp, uint16 serverPort, std::string userId, uint32 workerThreadCount)
{
	_userId = userId;

	std::array<BYTE, kTokenBytes> token{};
	const bool hasToken = LoadToken(userId, token);

	_iocpCore = MakeShared<CIocpCore>();

	// 세션 생성 시점에 곧바로 _session에 보관 — SendChat()이 이후 바로
	// 쓸 수 있어야 하므로, CIocpClientService::GetSession(index)를 거치지
	// 않고 팩토리에서 직접 캡처한다.
	SessionFactory factory = [this, userId, hasToken, token]() -> CSessionRef
		{
			auto session = std::make_shared<CChatClientSession>(userId, hasToken, token, this);
			_session = session;
			return session;
		};

	EngineCoreRef engineCore = _iocpCore;
	CNetServiceRef service = CNetworkFactory::CreateClientService(
		engineCore, CNetAddress(serverIp, serverPort), factory, 1, workerThreadCount);

	_service = std::static_pointer_cast<CIocpClientService>(service);
	if( _service == nullptr )
		return false;

	return _service->Start();
}

//***************************************************************************
// @brief 연결을 끊고 정리될 때까지 대기합니다.
//***************************************************************************
void CChatClientMain::Disconnect()
{
	if( _service )
	{
		_service->Close(); // 세션이 실제로 0개 될 때까지 블로킹 대기 + 워커 스레드 Join
		_service.reset();
	}

	_iocpCore.reset();
}

//***************************************************************************
// @brief 채팅 메시지 전송. 연결/로그인 전이면 조용히 무시합니다.
//***************************************************************************
void CChatClientMain::SendChat(const std::string& message)
{
	auto session = _session.lock();
	if( session == nullptr )
		return;

	session->SendChat(message);
}

//***************************************************************************
// @brief 서버에 랜덤 닉네임 생성을 요청합니다.
//***************************************************************************
void CChatClientMain::RequestNicknameGeneration()
{
	auto session = _session.lock();
	if( session == nullptr )
		return;

	session->SendNicknameGenerateReq();
}

//***************************************************************************
// @brief 서버에 닉네임 변경을 요청합니다.
//***************************************************************************
void CChatClientMain::RequestChangeNickname(const std::string& newNickname)
{
	auto session = _session.lock();
	if( session == nullptr )
		return;

	session->SendChangeNicknameReq(newNickname);
}

//***************************************************************************
// @brief 로그인 응답 수신 시 CChatClientSession이 호출합니다.
// @details 성공 시 서버가 회전 발급한 새 토큰을 로컬 파일에 저장한 뒤,
//          앱 쪽 콜백에는 success/reason만 전달한다(토큰 저장은 내부 구현
//          세부사항 — 호출부가 신경 쓸 필요 없음).
//***************************************************************************
void CChatClientMain::OnLoginResult(bool success, ELoginResult reason,
	const std::array<BYTE, kTokenBytes>& newToken)
{
	if( success )
		SaveToken(_userId, newToken);

	if( _onLoginResult )
		_onLoginResult(success, reason);
}

//***************************************************************************
// @brief 채팅 메시지 수신 시 CChatClientSession이 호출합니다.
//***************************************************************************
void CChatClientMain::OnChatReceived(const std::string& message)
{
	if( _onChatMessage )
		_onChatMessage(message);
}

//***************************************************************************
// @brief 세션이 끊겼을 때 CChatClientSession이 호출합니다.
//***************************************************************************
void CChatClientMain::OnSessionClosed()
{
	if( _onDisconnected )
		_onDisconnected();
}

//***************************************************************************
// @brief 닉네임 생성 응답 수신 시 핸들러가 호출합니다.
//***************************************************************************
void CChatClientMain::OnNicknameGenerated(const std::string& nickname)
{
	if( _onNicknameGenerated )
		_onNicknameGenerated(nickname);
}

//***************************************************************************
// @brief 닉네임 변경 응답 수신 시 핸들러가 호출합니다.
// @details 성공 시 로컬 토큰 파일을 옛 닉네임(_userId) -> newNickname으로
//          이전한 뒤 _userId를 갱신한다 — 그래야 다음 접속 때 새 닉네임
//          기준으로 토큰 파일을 찾는다.
//***************************************************************************
void CChatClientMain::OnNicknameChangeResult(bool success, ELoginResult reason, const std::string& newNickname)
{
	if( success )
	{
		RenameTokenFile(_userId, newNickname);
		_userId = newNickname;
	}

	if( _onNicknameChangeResult )
		_onNicknameChangeResult(success, reason, newNickname);
}