
//***************************************************************************
// ChatClient.cpp : CChatClientMain 구동 예시 (콘솔 데모)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientMain.h"

#include <iostream>
#include <string>

int main()
{
	std::cout << "User ID: ";
	std::string userId;
	std::getline(std::cin, userId);

	CChatClientMain client;

	client.SetOnLoginResult([](bool success, ELoginResult reason)
		{
			if( success )
			{
				std::cout << "[로그인 성공]" << std::endl;
				return;
			}

			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				std::cout << "[가입 실패] 이미 사용 중인 닉네임입니다." << std::endl;
				break;
			case ELoginResult::InvalidNickname:
				std::cout << "[가입 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄, 1~31자)." << std::endl;
				break;
			case ELoginResult::AccountNotFound:
				std::cout << "[재접속 실패] 저장된 토큰에 해당하는 계정을 찾을 수 없습니다." << std::endl;
				break;
			case ELoginResult::TokenMismatch:
				std::cout << "[재접속 실패] 토큰이 일치하지 않습니다(다른 기기의 잔여 토큰이거나 손상됨)." << std::endl;
				break;
			case ELoginResult::DbError:
			default:
				std::cout << "[가입/재접속 실패] 서버 오류 - 잠시 후 다시 시도해주세요." << std::endl;
				break;
			}
		});

	client.SetOnChatMessage([](const std::string& message)
		{
			// [주의] 이 콜백은 IOCP 워커 스레드에서 직접 호출됩니다 — 아래
			// std::cin으로 입력을 읽는 메인 스레드와 std::cout 출력이 섞여
			// 콘솔 표시가 뒤엉길 수 있습니다(데모 단순화 — 실제 UI에서는
			// 스레드 마샬링 필요).
			std::cout << "\n> " << message << std::endl;
		});

	client.SetOnDisconnected([]()
		{
			std::cout << "[서버와 연결이 끊어졌습니다]" << std::endl;
		});

	// TODO: 실서비스에서는 설정 파일/커맨드라인 인자로 대체
	if( !client.Connect(_T("127.0.0.1"), 7777, userId) )
	{
		std::cerr << "CChatClientMain::Connect() 실패" << std::endl;
		return 1;
	}

	std::cout << "메시지를 입력하세요 (/quit 입력 시 종료):" << std::endl;

	std::string line;
	while( std::getline(std::cin, line) )
	{
		if( line == "/quit" )
			break;

		if( !line.empty() )
			client.SendChat(line);
	}

	client.Disconnect();
	return 0;
}