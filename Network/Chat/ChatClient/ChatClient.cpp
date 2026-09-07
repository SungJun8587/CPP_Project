
//***************************************************************************
// ChatClient.cpp : CChatClientMain 구동 예시 (콘솔 데모)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientMain.h"

#include <iostream>
#include <string>

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
				_tcout << _T("[로그인 성공]") << std::endl;

				MainClose();
				return;
			}

			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				_tcout << _T("[가입 실패] 이미 사용 중인 닉네임입니다.") << std::endl;
				break;
			case ELoginResult::InvalidNickname:
				_tcout << _T("[가입 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄, 1~31자).") << std::endl;
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

	// TODO: 실서비스에서는 설정 파일/커맨드라인 인자로 대체
	if( !client.Connect(_T("127.0.0.1"), 30201, userId) )
	{
		std::cerr << "CChatClientMain::Connect() 실패" << std::endl;

		MainClose();
		return 1;
	}

	_tcout << _T("메시지를 입력하세요 (/quit 입력 시 종료):") << std::endl;

	std::string line;
	while( std::getline(std::cin, line) )
	{
		if( line == "/quit" )
			break;

		if( !line.empty() )
			client.SendChat(line);
	}

	client.Disconnect();
	
	MainClose();

	return 0;
}