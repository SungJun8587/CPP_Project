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

## 실행 파라미터
옵션을 모두 생략하면 기본값으로 실행한다. 알 수 없는 옵션이거나 `--help` / `-h`이면 사용법을 출력하고 종료한다(종료 코드: `--help`/`-h`는 0, 그 외 1). 값이 필요한 옵션에 값이 없으면 사용법을 출력하고 종료 코드 1로 끝난다.

| 옵션 | 값 | 기본값 | 설명 |
|---|---|---|---|
| `--host` | 주소 | `127.0.0.1` | 접속할 서버 주소 |
| `--port` | 정수 | `9000` | 접속할 서버 포트 |
| `--clients` | 정수 | `100` | 가상 클라이언트 수(`CChatClientSession`과 같은 종류의 IOCP 세션). 1보다 작으면 1로 보정 |
| `--workers` | 정수 | `0` | IOCP 워커 스레드 수. `0`=자동(하드웨어 코어 수) |
| `--sched` | 정수 | `2` | 주기 송신(채팅/목록/…)을 돌리는 스케줄러 스레드 수. 세션 번호 % 스레드 수로 분담. 1보다 작으면 1로 보정 |
| `--duration` | 초 | `60` | 테스트 시간. `0`=Ctrl+C까지 무한 |
| `--ramp` | 정수/초 | `50` | 초당 신규 접속 수. `0` 이하=전부 한 번에 접속(burst) |
| `--chat` | ms | `1000` | 클라이언트당 채팅 송신 간격. `0`=송신 안 함 |
| `--list` | ms | `5000` | 방 목록 조회 간격. `0`=조회 안 함 |
| `--members` | ms | `0` | 방 멤버 목록 조회 간격. `0`=조회 안 함. 방 입장(`--room`) 상태에서만 의미 있음 |
| `--count` | ms | `0` | 동접자 조회 간격. `0`=조회 안 함 |
| `--room` | 방 번호 | `0` | 로그인 후 입장할 방. `0`=로비 유지 |
| `--room-spread` | 정수 | `1` | `1`보다 크면 `room` ~ `room+n-1`번 방에 세션 번호 기준 순환 분산 |
| `--msg-bytes` | 정수 | `32` | 채팅 본문 길이(바이트). 패킷 본문 버퍼 크기를 넘길 수 없음 |
| `--page-size` | 정수 | `20` | 목록 조회 요청(방 목록/멤버 목록)의 pageSize |
| `--prefix` | 문자열 | `S` | 신규 가입 닉네임 접두어. ASCII만 허용(그 외 문자는 `_`로 치환). 닉네임 = 접두어 + 4자리 실행 태그 + 5자리 번호이며 16자 이내여야 함 |
| `--accounts` | 파일 경로 | (없음) | 계정(public_id+token) 저장/재사용 파일. 지정하면 재실행 시 신규 가입 대신 재접속 |

## 실행 배치파일 (ChatStressClient.bat)
`ChatStressClient.bat`은 위 옵션을 한 곳에 모아 두고 `x64\Release\ChatStressClient.exe`를 실행하는 스크립트다. 배치파일 상단 주석에 옵션 설명이 들어 있으므로 값만 고쳐 쓰면 된다.

```bat
@ECHO OFF
x64\Release\ChatStressClient.exe ^
    --host 127.0.0.1 --port 9000 ^
    --clients 1000 --ramp 100 --duration 60 ^
    --chat 1000 --list 5000 ^
    --accounts ChatStressAccounts.txt
PAUSE
```

사용법
1. 배치파일과 같은 위치에서 `x64\Release\ChatStressClient.exe`가 실행되도록 빌드 결과를 둔다(경로가 다르면 배치파일의 exe 경로를 수정).
2. 서버를 먼저 띄운 뒤 배치파일을 더블클릭하거나 명령 프롬프트에서 실행한다. 상대 경로(`x64\Release\…`, `--accounts` 파일)는 실행한 위치(현재 디렉터리) 기준이다.
3. 부하 조건을 바꾸려면 `^`로 이어진 옵션 줄의 값을 수정한다. 줄 끝의 `^`(줄 잇기)는 뒤에 공백 없이 붙여야 한다.
4. 실행 중에는 1초마다 상태 한 줄이 출력되고, 종료 시(`--duration` 경과 또는 Ctrl+C) 접속/로그인/송수신/RTT 백분위 요약이 출력된다. 끝나면 `PAUSE`로 창이 유지된다.
5. 지금 배치파일의 설정은 동시 1000 클라이언트를 초당 100개씩 접속시켜 60초간 채팅 1초, 방 목록 5초 간격으로 부하를 주고, 계정을 `ChatStressAccounts.txt`에 저장해 재실행 때 재사용한다.

용도별 옵션 조합
- 접속/로그인만 확인: `--chat 0 --list 0`
- 방 부하(채팅 + 멤버 목록): `--room 1 --members 2000`
- 여러 방에 분산: `--room 1 --room-spread 10`
- 순간 접속 폭주: `--ramp 0`
- 종료 없이 계속: `--duration 0`

## 가정(확인 필요)
- `CIocpClientService::ConnectOneMoreSession()`이 public (램프업에 사용). private이면 `--ramp 0`(burst)만 사용 가능.
- ramp 모드는 서비스를 세션 수 0으로 만든다 (Start()는 워커만 구동).
- `CIocpSession::Send(const void*, uint16)`가 워커 밖(스케줄러 스레드)에서 호출 가능 — `CChatClientMain::SendChat()`이 콘솔 스레드에서 쓰는 경로와 동일.
- `CIocpSession::Send`의 반환값은 bool(IocpSession.cpp 시그니처 기준).
