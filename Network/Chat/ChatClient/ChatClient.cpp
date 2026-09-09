
//***************************************************************************
// ChatClient.cpp : CChatClientMain 구동 예시 (콘솔 데모)
//
//***************************************************************************

#include "pch.h"
#include "ChatClientMain.h"
#include <Util/EncodingConvert.h>

#include <iostream>
#include <string>
#include <cstring>
#include <mutex>
#include <conio.h>		// _getwch() — 입력 중인 내용을 한 글자씩 추적하기 위해 필요

namespace
{
	//***************************************************************************
	// [설계] 입력/출력 뒤섞임 문제
	// ──────────────────────────────────────────────────────────────────────
	// 서버로부터 오는 메시지(채팅/시스템 알림)는 전부 IOCP 워커 스레드가
	// 직접 호출하는 콜백에서 출력된다. 반면 사용자가 채팅을 치는 건 메인
	// 스레드다. 예전엔 std::getline()으로 입력을 받았는데, 이 함수는 Enter를
	// 누르기 전까지 "지금까지 뭘 입력했는지"를 호출부가 전혀 알 수 없다 —
	// 그래서 타이핑 도중 비동기 메시지가 도착하면 화면에 그냥 끼어들어
	// 입력 중이던 내용과 뒤섞였다.
	//
	// 해결: std::getline() 대신 한 글자씩 직접 읽는 ReadLineInteractive()를
	// 쓴다. 이러면 "지금까지 입력한 내용"을 g_currentInputLine에 항상
	// 정확히 알고 있으므로, 비동기 메시지가 도착했을 때 그 줄을 지우고
	// 메시지를 출력한 뒤 프롬프트+입력 중이던 내용을 그대로 복원할 수 있다.
	// 콘솔 출력 자체도 g_consoleMutex로 직렬화해 두 스레드가 동시에
	// 같은 줄을 건드리는 경쟁을 막는다.
	//
	// [알려진 한계] 한글(Hangul)은 콘솔에서 보통 2칸(더블 와이드)을 차지하는데,
	// 백스페이스 처리("\b \b")는 1칸만 지운다고 가정한다 — 한글을 지울 때
	// 화면에 잔상이 한 칸 남을 수 있다. 완전히 고치려면 각 글자의 실제
	// 화면 폭을 계산해야 하는데, 이 데모 범위를 벗어난다고 판단해 남겨뒀다.
	//***************************************************************************
	std::mutex		g_consoleMutex;
	std::wstring	g_currentInputLine;
	const wchar_t* kPrompt = L"> ";

	WORD g_defaultConsoleAttr = 0;

	//***************************************************************************
	// @brief 프로그램 시작 시 콘솔의 기본 글자색을 기억해둔다(색을 바꿨다가
	//        되돌릴 때 무조건 "흰색"이 아니라 사용자의 원래 콘솔 테마로
	//        복원하기 위함).
	//***************************************************************************
	void CaptureDefaultConsoleAttr()
	{
		CONSOLE_SCREEN_BUFFER_INFO info;
		HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if( hOut != INVALID_HANDLE_VALUE && ::GetConsoleScreenBufferInfo(hOut, &info) )
			g_defaultConsoleAttr = info.wAttributes;
		else
			g_defaultConsoleAttr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // 조회 실패 시 무난한 흰색으로 폴백
	}

	void SetConsoleColor(WORD attr)
	{
		::SetConsoleTextAttribute(::GetStdHandle(STD_OUTPUT_HANDLE), attr);
	}

	void ResetConsoleColor()
	{
		::SetConsoleTextAttribute(::GetStdHandle(STD_OUTPUT_HANDLE), g_defaultConsoleAttr);
	}

	//***************************************************************************
	// @brief 현재 콘솔 줄을 지운다(커서를 줄 맨 앞으로 옮기고 공백으로 덮어씀).
	// @details g_consoleMutex를 이미 잠근 상태에서만 호출할 것.
	//***************************************************************************
	void ClearCurrentLineLocked()
	{
		size_t clearWidth = wcslen(kPrompt) + g_currentInputLine.size() + 4; // 여유분
		std::wcout << L'\r' << std::wstring(clearWidth, L' ') << L'\r';
	}

	//***************************************************************************
	// @brief 프롬프트 + 지금까지 입력한 내용을 다시 그린다.
	// @details g_consoleMutex를 이미 잠근 상태에서만 호출할 것.
	//***************************************************************************
	void RedrawPromptLocked()
	{
		SetConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
		std::wcout << kPrompt;
		ResetConsoleColor();
		std::wcout << g_currentInputLine << std::flush;
	}

	//***************************************************************************
	// @brief 네트워크(비동기) 스레드에서 도착한 한 줄을 안전하게 출력한다.
	// @details 지금 입력 중이던 내용을 지우고 그 자리에 메시지를 출력한 뒤,
	//          프롬프트+입력 중이던 내용을 그대로 복원한다 — 사용자가 치던
	//          내용이 유실되지 않는다.
	// @param line 출력할 내용(색상 없이)
	// @param color 이 줄에 입힐 글자색(FOREGROUND_* 조합)
	//***************************************************************************
	void PrintAsyncLine(const std::wstring& line, WORD color)
	{
		std::lock_guard<std::mutex> lock(g_consoleMutex);

		ClearCurrentLineLocked();

		SetConsoleColor(color);
		std::wcout << line << std::endl;
		ResetConsoleColor();

		RedrawPromptLocked();
	}

	//***************************************************************************
	// @brief 한 글자씩 직접 읽어 입력 중인 내용을 실시간으로 추적하면서 한
	//        줄을 입력받는다. 반환값은 UTF-16(네이티브 wstring) — 호출부가
	//        필요하면 UnicodeToUtf8()로 변환해서 쓴다.
	//***************************************************************************
	std::wstring ReadLineInteractive()
	{
		{
			std::lock_guard<std::mutex> lock(g_consoleMutex);
			g_currentInputLine.clear();
			RedrawPromptLocked();
		}

		for( ;; )
		{
			wint_t ch = _getwch();

			if( ch == L'\r' || ch == L'\n' )
			{
				std::wcout << std::endl;
				break;
			}
			else if( ch == 8 ) // Backspace
			{
				std::lock_guard<std::mutex> lock(g_consoleMutex);
				if( !g_currentInputLine.empty() )
				{
					g_currentInputLine.pop_back();
					std::wcout << L"\b \b" << std::flush; // 알려진 한계: 한글은 1칸만 지워짐(위 주석 참고)
				}
			}
			else if( ch == 0 || ch == 0xE0 )
			{
				// 방향키/기능키 등 확장 키의 첫 바이트 — 다음 스캔코드를
				// 마저 읽어서 버린다(데모 단순화 — 커서 이동 등은 지원 안 함).
				_getwch();
			}
			else
			{
				std::lock_guard<std::mutex> lock(g_consoleMutex);
				g_currentInputLine.push_back(static_cast<wchar_t>(ch));
				std::wcout << static_cast<wchar_t>(ch) << std::flush;
			}
		}

		return g_currentInputLine;
	}
}

//***************************************************************************
// @brief 클라이언트 종료 시 등록된 클라이언트 설정, 글로벌 리소스를 해제합니다.
//***************************************************************************
void MainClose()
{
	// 1. BaseGlobal 전역 프레임워크 리소스 해제
	BaseGlobal::Destroy();

	// 2. Winsock 라이브러리 자원(WSACleanup)을 해제
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

	// 3. Winsock 라이브러리(WSAStartup) 및 IOCP 확장 함수 포인터를 초기화
	CSocketUtils::Init();
	std::cout << "[System] CSocketUtils::Init()...\n\n";
	CaptureDefaultConsoleAttr();

	// 4. BaseGlobal 프레임워크 초기화
	BaseGlobal::Init();

	std::cout << "User ID: ";
	std::string userId;
	std::getline(std::cin, userId);

	CChatClientMain client;

	client.SetOnLoginResult([](bool success, ELoginResult reason, const std::string& nickname)
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
				PrintAsyncLine(L"[로그인 성공] 닉네임: " + Utf8ToTString(nickname), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
				return;
			}

			// [수정] 실패해도 서버가 채워 보낸 닉네임이 있으면(신규 가입 시도
			// 닉네임, 또는 재접속 대상 계정의 현재 닉네임) 같이 보여준다 —
			// AccountNotFound처럼 애초에 계정을 못 찾은 경우엔 빈 문자열이라
			// 자연스럽게 생략된다.
			const std::wstring nicknameSuffix = nickname.empty() ? L"" : (L" (닉네임: " + Utf8ToTString(nickname) + L")");

			std::wstring msg;
			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				msg = L"[가입 실패] 이미 사용 중인 닉네임입니다.";
				break;
			case ELoginResult::InvalidNickname:
				msg = L"[가입 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).";
				break;
			case ELoginResult::AccountNotFound:
				msg = L"[재접속 실패] 저장된 토큰에 해당하는 계정을 찾을 수 없습니다.";
				break;
			case ELoginResult::TokenMismatch:
				msg = L"[재접속 실패] 토큰이 일치하지 않습니다(다른 기기의 잔여 토큰이거나 손상됨).";
				break;
			case ELoginResult::DbError:
			default:
				msg = L"[가입/재접속 실패] 서버 오류 - 잠시 후 다시 시도해주세요.";
				break;
			}
			PrintAsyncLine(msg + nicknameSuffix, FOREGROUND_RED | FOREGROUND_INTENSITY);

			// [수정] 실패 시에도 여기서 MainClose()를 호출하지 않는다 — 이유는
			// 성공 분기와 동일(IOCP 워커 스레드 콜백 한가운데). 실패했다는
			// 사실만 알리고, 실제 종료는 아래 main() 흐름(연결 실패 시
			// return 1, 또는 사용자가 /quit으로 정상 종료)에 맡긴다. 로그인
			// 실패라도 세션 자체는 아직 연결돼 있을 수 있으므로(예: 새
			// 닉네임으로 재시도하는 UI가 나중에 추가될 수 있음) 여기서
			// 인프라를 강제로 뽑는 건 위험하다.
		});

	client.SetOnChatMessage([](const std::string& senderNickname, const std::string& message)
		{
			// [수정] 예전엔 여기서 곧바로 _tcout으로 출력해 std::getline()으로
			// 입력 중이던 메인 스레드와 화면이 뒤섞였다 — 이제 PrintAsyncLine()이
			// 입력 중이던 줄을 지웠다 복원까지 해주므로 걱정 없다.
			// [수정] "> {메시지}"처럼 뭉뚱그리지 않고 발신자 닉네임을 그대로 출력한다.
			PrintAsyncLine(Utf8ToTString(senderNickname) + L": " + Utf8ToTString(message), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		});

	client.SetOnDisconnected([]()
		{
			PrintAsyncLine(L"[서버와 연결이 끊어졌습니다]", FOREGROUND_RED | FOREGROUND_INTENSITY);
		});

	client.SetOnNicknameChangeResult([](bool success, ELoginResult reason, const std::string& newNickname)
		{
			if( success )
			{
				PrintAsyncLine(L"[닉네임 변경 성공] -> " + Utf8ToTString(newNickname), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
				return;
			}

			std::wstring msg;
			switch( reason )
			{
			case ELoginResult::NicknameTaken:
				msg = L"[닉네임 변경 실패] 이미 사용 중인 닉네임입니다.";
				break;
			case ELoginResult::InvalidNickname:
				msg = L"[닉네임 변경 실패] 닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).";
				break;
			case ELoginResult::DbError:
			default:
				msg = L"[닉네임 변경 실패] 서버 오류 - 잠시 후 다시 시도해주세요.";
				break;
			}
			PrintAsyncLine(msg, FOREGROUND_RED | FOREGROUND_INTENSITY);
		});

	// TODO: 실서비스에서는 설정 파일/커맨드라인 인자로 대체
	if( !client.Connect(_T("127.0.0.1"), 30201, userId) )
	{
		std::cerr << "CChatClientMain::Connect() 실패" << std::endl;

		MainClose();
		return 1;
	}

	PrintAsyncLine(L"메시지를 입력하세요 (/quit 종료, /nick <새닉네임> 닉네임 변경):", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

	for( ;; )
	{
		std::wstring wline = ReadLineInteractive();

		if( wline == L"/quit" )
			break;

		if( wline.empty() )
			continue;

		// ASCII 명령("/nick ")은 UTF-8/UTF-16 어느 쪽으로 비교해도 결과가
		// 같으므로, 여기서 UTF-8로 한 번만 변환해 이후 로직은 기존과 동일하게 유지.
		std::string line = UnicodeToUtf8(wline);

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