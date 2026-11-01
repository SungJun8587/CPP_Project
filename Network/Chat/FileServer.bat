@ECHO OFF
PUSHD "%~dp0.."
Bin\x64\Release\FileServer64.exe --config "Config\fileserver_config_mysql.json"
POPD
PAUSE
EXIT /B

:: ---- 아래는 설명(주석)이며 EXIT /B 이후라 실행되지 않는다. 한글 주석이 cmd 파싱에 영향을 주지 않도록 실행 줄 뒤에 둔다. ----
:: FileServer64.exe [옵션...]
:: [설명]
::  - 파일 서버(프로필 이미지 업로드/다운로드)를 실행한다. 설정 파일(JSON)을 읽고,
::    아래 옵션을 주면 파일 값 대신 그 값을 쓴다(명령행 > 설정 파일)
::  - 채팅 서버 설정의 FileServerUrl 과 이 서버의 PublicBaseUrl(--public-url) 은 같은 주소여야 한다
::  - 실행 위치와 상관없이 Chat 폴더(Config 와 Bin 이 있는 곳)로 옮겨서 실행한다
::  - Ctrl+C 로 정상 종료
:: [파라미터 설명]
::      --config <file>         : 설정 파일 경로(기본 Config\fileserver_config_mysql.json)
::      --name <str>            : 서버 이름
::      --ip <addr>             : 수신 주소
::      --port <n>              : 수신 포트
::      --max-sessions <n>      : 최대 동시 세션 수
::      --workers <n>           : IOCP 워커 스레드 수, 0=자동
::      --storage-dir <dir>     : 업로드 파일 저장 디렉터리
::      --public-url <url>      : 클라이언트에 알려 줄 접근 URL 의 기준(예: http://host:8081)
::      --max-upload-bytes <n>  : 업로드 최대 크기(바이트)
::  형식은 --key value 또는 --key=value, 도움말은 --help
