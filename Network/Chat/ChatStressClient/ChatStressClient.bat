@ECHO OFF
:: ChatStressClient.exe [옵션...]
:: [설명]
::  - IOCP 세션 기반 가상 클라이언트 N개를 서버에 접속시켜 채팅/방 목록/멤버 목록/동접자 조회 부하를 주고,
::    1초마다 상태 한 줄, 종료 시 결과 요약(접속/로그인/송수신/RTT 백분위)을 콘솔에 출력
::  - 옵션을 모두 생략하면 기본값으로 실행(--help 로 전체 옵션 확인)
:: [파라미터 설명]
::      --host <addr>       : 서버 주소(기본 127.0.0.1)
::      --port <n>          : 서버 포트(기본 9000)
::      --clients <n>       : 가상 클라이언트 수(기본 100)
::      --workers <n>       : IOCP 워커 스레드 수, 0=자동(기본 0)
::      --sched <n>         : 주기 송신 스케줄러 스레드 수(기본 2)
::      --duration <sec>    : 테스트 시간(초), 0=Ctrl+C 까지 무한(기본 60)
::      --ramp <n/sec>      : 초당 신규 접속 수, 0=전부 한 번에 접속(기본 50)
::      --chat <ms>         : 클라이언트당 채팅 송신 간격, 0=송신 안 함(기본 1000)
::      --list <ms>         : 방 목록 조회 간격, 0=조회 안 함(기본 5000)
::      --members <ms>      : 방 멤버 목록 조회 간격, 0=조회 안 함, --room 필요(기본 0)
::      --count <ms>        : 동접자 조회 간격, 0=조회 안 함(기본 0)
::      --room <id>         : 로그인 후 입장할 방 번호, 0=로비 유지(기본 0)
::      --room-spread <n>   : room ~ room+n-1 번 방에 순환 분산(기본 1)
::      --msg-bytes <n>     : 채팅 본문 길이(기본 32)
::      --page-size <n>     : 목록 조회 pageSize(기본 20)
::      --prefix <str>      : 신규 가입 닉네임 접두어(기본 S)
::      --accounts <file>   : 계정(public_id+token) 저장/재사용 파일, 재실행 시 신규 가입 대신 재접속
..\Bin\x64\Release\ChatStressClient64.exe ^
    --host 127.0.0.1 --port 30201 ^
    --clients 1000 --ramp 100 --duration 30 ^
    --chat 1000 --list 5000 ^
    --accounts ChatStressAccounts.txt
PAUSE