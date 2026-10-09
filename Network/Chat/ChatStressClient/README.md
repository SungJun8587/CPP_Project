# ChatStressClient — IOCP 세션 기반 채팅 스트레스 테스트 클라이언트

`CChatClientSession`과 같은 구조(CIocpSession 상속, OnConnected에서 LoginReq, 헤더 기반 OnRecv 프레이밍,
`CNetworkFactory::CreateClientService` → `CIocpClientService`)로 가상 클라이언트 N개를 한 IOCP 위에서 구동한다.

## 구성
| 파일 | 역할 |
|---|---|
| ChatStressSession | CIocpSession 기반 가상 클라이언트. 로그인/방입장/수신 통계/RTT, 주기 송신 Tick |
| ChatStressScheduler | 세션 레지스트리 + 주기 송신 스레드(세션 번호 % 스레드 수로 분담) |
| ChatStressAccounts | `--accounts` 파일에 public_id+token 저장/재사용(재실행 시 신규 가입 방지) |
| ChatStressConfig / ChatStressStats / ChatStressReporter | 옵션, 히스토그램·카운터, 출력 (시계는 `ptime::MonotonicNowMicros()`) |
| ChatStressMain | wmain: 서비스 생성, 램프업, 1초 리포트, 종료 |

## 프로젝트 설정 (Visual Studio)
1. `ChatStressClient.vcxproj`를 기존 솔루션(ChatClient가 있는 솔루션)에 추가.
2. ChatClient 프로젝트의 `pch.h` / `pch.cpp`를 이 폴더에 복사(소스가 `#include "pch.h"` 사용).
3. `ChatStressPaths.props`의 `ServerCoreInclude`, `ChatSrcDir`, `ServerCoreLibDir`, `ServerCoreLib`를 ChatClient 프로젝트와 동일하게 수정.
4. 문자 집합 Unicode(wmain, `_tstring`=wstring 가정).

## 실행 예
- 점진 접속: `ChatStressClient --host 127.0.0.1 --port 9000 --clients 1000 --ramp 100 --chat 1000`
- 한 번에 접속(burst): `--ramp 0`
- 방 입장 + 멤버 목록: `--room 1 --members 2000`
- 계정 재사용: `--accounts stress_accounts.dat` (첫 실행에 가입, 이후 재접속)

## 가정(확인 필요)
- `CIocpClientService::ConnectOneMoreSession()`이 public (램프업에 사용). private이면 `--ramp 0`(burst)만 사용 가능.
- ramp 모드는 서비스를 세션 수 0으로 만든다 (Start()는 워커만 구동).
- `CIocpSession::Send(const void*, uint16)`가 워커 밖(스케줄러 스레드)에서 호출 가능 — `CChatClientMain::SendChat()`이 콘솔 스레드에서 쓰는 경로와 동일.
- `CIocpSession::Send`의 반환값은 bool(IocpSession.cpp 시그니처 기준).
