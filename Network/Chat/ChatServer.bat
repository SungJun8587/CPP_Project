@ECHO OFF
PUSHD "%~dp0.."
Bin\x64\Release\ChatServer64.exe --config "Config\chatserver_config_mysql.json"
POPD
PAUSE
EXIT /B

:: ---- 아래는 설명(주석)이며 EXIT /B 이후라 실행되지 않는다. 한글 주석이 cmd 파싱에 영향을 주지 않도록 실행 줄 뒤에 둔다. ----
:: ChatServer64.exe [옵션...]
:: [설명]
::  - 채팅 서버를 실행한다. 설정 파일(JSON)을 읽고, 아래 옵션을 주면 파일 값 대신 그 값을 쓴다(명령행 > 설정 파일)
::  - 같은 설정 파일로 채널을 여러 개 띄울 때는 --channel 과 --port 를 인스턴스마다 다르게 준다
::    (채널 ID 는 메시지 id 의 노드 번호로 쓰이므로 인스턴스끼리 겹치면 안 된다)
::  - 실행 위치와 상관없이 Chat 폴더(Config 와 Bin 이 있는 곳)로 옮겨서 실행한다
::  - Ctrl+C 로 정상 종료
:: [파라미터 설명]
::      --config <file>       : 설정 파일 경로(기본 Config\chatserver_config_mysql.json)
::      --name <str>          : 서버 이름
::      --group <n>           : 서버 그룹 ID
::      --channel <n>         : 서버 채널 ID, 0~1023
::      --ip <addr>           : 수신 주소
::      --port <n>            : 수신 포트
::      --max-sessions <n>    : 최대 동시 세션 수
::      --workers <n>         : IOCP 워커 스레드 수, 0=자동
::  형식은 --key value 또는 --key=value, 도움말은 --help
:: 채널 2 를 같은 설정 파일로 추가 실행하는 예(위 명령 대신 사용):
::   Bin\x64\Release\ChatServer64.exe --channel 2 --port 30202
