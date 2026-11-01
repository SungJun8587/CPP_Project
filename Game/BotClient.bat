@ECHO OFF
Bin\x64\Release\BotClient64.exe --config "Config\botclient_config.json"
PAUSE
EXIT /B

:: ---- 아래는 설명(주석)이며 EXIT /B 이후라 실행되지 않는다. 한글 주석이 cmd 파싱에 영향을 주지 않도록 실행 줄 뒤에 둔다. ----
:: BotClient.exe [옵션...]
:: [설명]
::  - IOCP 세션 기반 테스트 봇 N마리를 Gateway 클라이언트 포트에 접속시켜 로그인 -> 존 입장 -> 몬스터 접근/공격을 반복시키고,
::    주기적으로 상태 요약(봇 상태/송수신량/전투/거부/위치 보정/공격 지연 백분위)을 콘솔에 출력
::  - 옵션을 모두 생략하면 기본값으로 실행(--help 로 전체 옵션 확인)
:: [파라미터 설명]
::      --config <file>       : 실행 옵션을 JSON 파일에서 읽음(명령행에 같이 준 옵션이 파일 값보다 우선)
::      --dump-config <file>  : 기본값 설정 파일 템플릿을 저장하고 종료
::      --host <addr>         : Gateway 클라이언트 포트 주소(기본 127.0.0.1)
::      --port <n>            : Gateway 클라이언트 포트(기본 7000)
::      --bots <n>            : 봇 수(기본 10)
::      --zone <id>           : 입장할 존 ID(기본 1)
::      --heroes <list>       : 영웅 ID 목록, 쉼표 구분, 봇 번호 순서대로 순환 배정, 0=서버 기본 영웅(기본 0)
::      --charid-base <n>     : 첫 봇의 캐릭터 ID, 봇 i는 base+i(기본 100000)
::      --ramp-ms <n>         : 봇 사이 로그인 간격(ms)(기본 20)
::      --tick-ms <n>         : 봇 행동 결정 틱 주기(ms)(기본 50)
::      --move-ms <n>         : 이동 요청 최소 간격(ms)(기본 300)
::      --attack-ms <n>       : 공격 요청 간격(ms)(기본 400)
::      --workers <n>         : IOCP 워커 스레드 수(기본 2)
::      --stats-sec <n>       : 통계 출력 주기(초)(기본 5)
::      --duration <sec>      : 실행 시간(초), 0=Ctrl+C 까지 무한(기본 0)
::  JSON 키(BotClient.json 참고): Host, Port, Bots, ZoneId, Heroes, CharIdBase, RampMs, TickMs,
::      MoveIntervalMs, AttackIntervalMs, WorkerThreadCnt, StatsSec, DurationSec  (위 --옵션과 같은 뜻, 없는 키는 기본값)
::  종료 코드: 0 정상, 2 로그인/입장 실패 또는 예기치 않은 연결 종료 발생, 1 또는 -1 옵션 오류/구동 실패
