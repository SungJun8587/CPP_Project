
//***************************************************************************
// ChatClient.cpp : CChatClientMain 구동 예시 (콘솔 데모)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientMain.h"

#include <iostream>
#include <string>
#include <cstring>

//***************************************************************************
// @brief 클라이언트 종료 시 등록된 클라이언트 설정, 글로벌 리소스를 해제합니다.
//***************************************************************************
void MainClose()
{
	// 3. BaseGlobal 전역 프레임워크 리소스 해제
	BaseGlobal::Destroy();

	// 4. Winsock 라이브러리 정리
	CSocketUtils::Clear();
}

int main()
{
	// 1. Debug 빌드에서의 CRT 메모리 누수 감지 옵션 설정
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	// 2. 콘솔 유니코드/UTF-8 환경 초기화
	InitUtf8Console();

	// 4. BaseGlobal 프레임워크 초기화
	BaseGlobal::Init();

	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";

	std::cout << "User ID: ";
	std::string userId;
	std::getline(std::cin, userId);

	CChatClientMain client;

	client.SetOnLoginResult([](bool success, ELoginResult reason)
		{
			if( success )
			{
				// [수정] 여기서 MainClose()를 호출하면 안 된다 — 이 콜백은
				// IOCP 워커 스레드가 아직 소켓/세션이 살아있는 채로 직접
				// 호출하는 콜백이다(OnRecv() -> 디스패처 -> HandleLoginRes()
				// -> OnLoginResult() -> 여기). 그 한가운데서
				// BaseGlobal::Destroy()/CSocketUtils::Clear()로 전역 메모리
				// 프레임워크와 Winsock 자체를 뽑아버리면, 아직 통신 중이던
				// 소켓이 그 자리에서 깨져 서버 쪽에서 연결이 끊긴 것처럼
				// 보인다(실제 증상: DB엔 이미 저장됐는데 클라이언트가
				// 곧바로 끊김). 로그인 성공은 세션의 "끝"이 아니라 "시작"
				// 이므로, 여기서는 결과만 알리고 이후 흐름(채팅 루프)은
				// main()이 정상적으로 이어가게 둔다.
				_tcout << _T("[로그인 성공]") << std::endl;
				return;
			}

			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				_tcout << _T("[가입 실패] 이미 사용 중인 닉네임입니다.") << std::endl;
				break;
			case ELoginResult::InvalidNickname:
				_tcout << _T("[가입 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).") << std::endl;
				break;
			case ELoginResult::AccountNotFound:
				_tcout << _T("[재접속 실패] 저장된 토큰에 해당하는 계정을 찾을 수 없습니다.") << std::endl;
				break;
			case ELoginResult::TokenMismatch:
				_tcout << _T("[재접속 실패] 토큰이 일치하지 않습니다(다른 기기의 잔여 토큰이거나 손상됨).") << std::endl;
				break;
			case ELoginResult::DbError:
			default:
				_tcout << _T("[가입/재접속 실패] 서버 오류 - 잠시 후 다시 시도해주세요.") << std::endl;
				break;
			}

			// [수정] 실패 시에도 여기서 MainClose()를 호출하지 않는다 — 이유는
			// 성공 분기와 동일(IOCP 워커 스레드 콜백 한가운데). 실패했다는
			// 사실만 알리고, 실제 종료는 아래 main() 흐름(연결 실패 시
			// return 1, 또는 사용자가 /quit으로 정상 종료)에 맡긴다. 로그인
			// 실패라도 세션 자체는 아직 연결돼 있을 수 있으므로(예: 새
			// 닉네임으로 재시도하는 UI가 나중에 추가될 수 있음) 여기서
			// 인프라를 강제로 뽑는 건 위험하다.
		});

	client.SetOnChatMessage([](const std::string& message)
		{
			// [주의] 이 콜백은 IOCP 워커 스레드에서 직접 호출됩니다 — 아래
			// std::cin으로 입력을 읽는 메인 스레드와 std::cout 출력이 섞여
			// 콘솔 표시가 뒤엉길 수 있습니다(데모 단순화 — 실제 UI에서는
			// 스레드 마샬링 필요).
			_tcout << _T("\n> ") << StringToTString(message) << std::endl;
		});

	client.SetOnDisconnected([]()
		{
			_tcout << _T("[서버와 연결이 끊어졌습니다]") << std::endl;
		});

	client.SetOnNicknameChangeResult([](bool success, ELoginResult reason, const std::string& newNickname)
		{
			if( success )
			{
				_tcout << _T("[닉네임 변경 성공] -> ") << StringToTString(newNickname) << std::endl;
				return;
			}

			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				_tcout << _T("[닉네임 변경 실패] 이미 사용 중인 닉네임입니다.") << std::endl;
				break;
			case ELoginResult::InvalidNickname:
				_tcout << _T("[닉네임 변경 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).") << std::endl;
				break;
			case ELoginResult::DbError:
			default:
				_tcout << _T("[닉네임 변경 실패] 서버 오류 - 잠시 후 다시 시도해주세요.") << std::endl;
				break;
			}
		});

	// TODO: 실서비스에서는 설정 파일/커맨드라인 인자로 대체
	if( !client.Connect(_T("127.0.0.1"), 30201, userId) )
	{
		std::cerr << "CChatClientMain::Connect() 실패" << std::endl;

		MainClose();
		return 1;
	}

	_tcout << _T("메시지를 입력하세요 (/quit 종료, /nick <새닉네임> 닉네임 변경):") << std::endl;

	std::string line;
	while( std::getline(std::cin, line) )
	{
		if( line == "/quit" )
			break;

		if( line.empty() )
			continue;

		// [단순화] "/nick " 접두사만 정확히 일치할 때 명령으로 처리한다 —
		// 앞뒤 공백 트리밍, 대소문자 무시 같은 건 데모 범위 밖.
		constexpr const char* kNickCommandPrefix = "/nick ";
		if( line.rfind(kNickCommandPrefix, 0) == 0 )
		{
			std::string newNickname = line.substr(std::strlen(kNickCommandPrefix));
			if( !newNickname.empty() )
				client.RequestChangeNickname(newNickname);
			continue;
		}

		client.SendChat(line);
	}

	client.Disconnect();

	MainClose();

	return 0;
}