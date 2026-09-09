
//***************************************************************************
// ChatClientMain.cpp: implementation of the CChatClientMain class.
//
//***************************************************************************

#include "pch.h"
#include "ChatClientMain.h"
#include "ChatClientSession.h"
#include <Crypto/CryptoUtil.h>
#include <Util/EncodingConvert.h>

#include <fstream>
#include <sstream>

namespace
{
	//***************************************************************************
	// @brief 이 실행 파일이 실제로 위치한 폴더 경로를 반환합니다(끝에 경로
	//        구분자 포함). 실패 시 빈 문자열(호출부가 그 경우 사실상 예전
	//        동작인 "현재 작업 디렉터리 기준 상대 경로"로 자연스럽게 폴백됨).
	// @details [수정 — 작업 디렉터리에 따라 재접속이 안 되던 문제] 예전엔
	// "chat_token_..." 상대 경로를 그대로 썼다. 상대 경로는 "현재 작업
	// 디렉터리(CWD)"를 기준으로 풀리는데, CWD는 실행 파일이 실제로 위치한
	// 폴더와 다를 수 있다 — 바로가기의 "시작 위치"나 Visual Studio 디버그
	// 실행 설정에 따라 서로 다른 값이 될 수 있어서, 한쪽에서 저장한 계정
	// 파일을 다른 쪽에서 못 찾는 사고가 났다(WinForms(C#) 클라이언트에서
	// 같은 문제를 먼저 발견하고 고친 뒤, 이쪽도 같은 방식으로 반영).
	//
	// GetModuleFileName(nullptr, ...)은 CWD가 아니라 "이 프로세스의
	// 실행 파일 자신이 실제로 위치한 경로"를 반환한다 — 어떻게 실행되든
	// 흔들리지 않는다. 프로세스 수명 동안 바뀌지 않는 값이라 함수 지역
	// static으로 한 번만 계산해 캐시한다(C++11부터 함수 지역 static
	// 초기화는 스레드 세이프가 보장됨).
	//***************************************************************************
	const _tstring& GetExecutableDirectory()
	{
		static const _tstring dir = []() -> _tstring
			{
				TCHAR buf[MAX_PATH] = {};
				DWORD len = ::GetModuleFileName(nullptr, buf, MAX_PATH);
				if( len == 0 || len == MAX_PATH )
					return _tstring(); // 조회 실패 — 빈 문자열 반환(호출부가 상대 경로로 폴백)

				_tstring path(buf, len);
				size_t lastSlash = path.find_last_of(_T("\\/"));
				if( lastSlash == _tstring::npos )
					return _tstring();

				return path.substr(0, lastSlash + 1); // 끝에 구분자 포함
			}();

		return dir;
	}
}

//***************************************************************************
// @brief 소멸자 — 아직 연결 중이면 Disconnect()로 정리합니다.
//***************************************************************************
CChatClientMain::~CChatClientMain()
{
	Disconnect();
}

//***************************************************************************
// @brief 프로필 이름에 대응하는 로컬 계정 파일 경로를 만듭니다.
// @details [수정 — 한글 프로필 이름 파일 I/O 실패] 이전엔 "chat_token_" +
// profileName(UTF-8 바이트) + ".dat"를 그냥 std::string으로 만들어
// std::ifstream/ofstream에 넘겼다. Windows의 narrow 문자열 파일 API는
// 이 바이트열을 시스템 ANSI 코드페이지(한국어 Windows면 CP949)로
// 해석하는데, UTF-8 한글(3바이트/글자)을 CP949(2바이트 DBCS)로 잘못
// 끊어 읽으면 대부분 유효하지 않은 바이트 시퀀스가 되어 파일 열기/생성
// 자체가 조용히 실패했다 — 그 결과 SaveAccount()가 아무것도 못 쓰고,
// 다음 실행의 LoadAccount()는 항상 "저장된 계정 없음"으로 판단해 매번
// 재접속 대신 신규 가입을 재시도하다 NicknameTaken으로 실패하는 증상이
// 났다.
//
// 이제 Utf8ToTString()로 profileName을 실제 UTF-16으로 변환한 뒤,
// std::ifstream/ofstream의 "const wchar_t*" 생성자(MSVC STL 확장 — 내부적으로
// CreateFileW를 그대로 타므로 ANSI 코드페이지 재해석이 전혀 없다)로 연다.
//
// [수정 — 작업 디렉터리 문제] 파일명 자체도 이제 GetExecutableDirectory()로
// 얻은 절대 경로를 앞에 붙인다 — 위 함수 설명 참고.
//***************************************************************************
_tstring CChatClientMain::TokenFilePath(const std::string& profileName)
{
	_tstring profileNameT = Utf8ToTString(profileName);
	return GetExecutableDirectory() + _T("chat_token_") + profileNameT + _T(".dat");
}

//***************************************************************************
// @brief 로컬 계정 파일을 읽습니다(1행: public_id 16진, 2행: token 16진).
//        파일이 없거나 형식이 잘못됐으면 false.
//***************************************************************************
bool CChatClientMain::LoadAccount(const std::string& profileName,
	std::array<BYTE, kPublicIdBytes>& outPublicId,
	std::array<BYTE, kTokenBytes>& outToken)
{
	std::ifstream in(TokenFilePath(profileName).c_str());
	if( !in.is_open() )
		return false;

	std::string publicIdHex;
	std::string tokenHex;
	if( !std::getline(in, publicIdHex) || !std::getline(in, tokenHex) )
		return false;

	if( !Crypto::CCryptoUtil::FromHex(publicIdHex, outPublicId.data(), outPublicId.size()) )
		return false;

	return Crypto::CCryptoUtil::FromHex(tokenHex, outToken.data(), outToken.size());
}

//***************************************************************************
// @brief 로컬 계정 파일에 저장(덮어쓰기)합니다.
//***************************************************************************
void CChatClientMain::SaveAccount(const std::string& profileName,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::array<BYTE, kTokenBytes>& token)
{
	std::ofstream out(TokenFilePath(profileName).c_str(), std::ios::trunc);
	if( !out.is_open() )
		return;

	out << Crypto::CCryptoUtil::ToHex(publicId.data(), publicId.size()) << '\n';
	out << Crypto::CCryptoUtil::ToHex(token.data(), token.size());
}

//***************************************************************************
// @brief 서버 접속을 게시합니다. 로컬에 이 프로필 이름으로 저장된 계정이
//        있으면 재접속으로, 없으면 이 이름을 원하는 닉네임으로 신규
//        가입을 시도합니다.
//***************************************************************************
bool CChatClientMain::Connect(const _tstring& serverIp, uint16 serverPort, std::string userId, uint32 workerThreadCount)
{
	_userId = userId;

	std::array<BYTE, kPublicIdBytes> publicId{};
	std::array<BYTE, kTokenBytes> token{};
	const bool hasToken = LoadAccount(userId, publicId, token);

	_iocpCore = MakeShared<CIocpCore>();

	// 세션 생성 시점에 곧바로 _session에 보관 — SendChat()이 이후 바로
	// 쓸 수 있어야 하므로, CIocpClientService::GetSession(index)를 거치지
	// 않고 팩토리에서 직접 캡처한다.
	SessionFactory factory = [this, userId, hasToken, publicId, token]() -> CSessionRef
		{
			auto session = std::make_shared<CChatClientSession>(userId, hasToken, publicId, token, this);
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
// @details 성공 시 서버가 반환한 public_id와 회전 발급한 새 토큰을 로컬
//          파일(프로필 이름 기준)에 저장한 뒤, 앱 쪽 콜백에는
//          success/reason/nickname을 전달한다(저장은 이 클래스가 전담하는
//          내부 구현 세부사항).
//***************************************************************************
void CChatClientMain::OnLoginResult(bool success, ELoginResult reason, const std::string& nickname,
	const std::array<BYTE, kPublicIdBytes>& publicId,
	const std::array<BYTE, kTokenBytes>& newToken)
{
	if( success )
		SaveAccount(_userId, publicId, newToken);

	if( _onLoginResult )
		_onLoginResult(success, reason, nickname);
}

//***************************************************************************
// @brief 채팅 메시지 수신 시 CChatClientSession이 호출합니다.
//***************************************************************************
void CChatClientMain::OnChatReceived(const std::string& senderNickname, const std::string& message)
{
	if( _onChatMessage )
		_onChatMessage(senderNickname, message);
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
// @details public_id는 이 요청으로 절대 바뀌지 않으므로 로컬 파일을 건드릴
//          필요가 없다 — 결과만 앱 쪽 콜백으로 전달한다.
//***************************************************************************
void CChatClientMain::OnNicknameChangeResult(bool success, ELoginResult reason, const std::string& newNickname)
{
	if( _onNicknameChangeResult )
		_onNicknameChangeResult(success, reason, newNickname);
}