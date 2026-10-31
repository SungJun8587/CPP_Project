@ECHO OFF
:: XlsxToJson.exe [엑셀 파일] --out [출력 폴더] [--sheets 시트이름 ...]
:: [설명]
::  - 기획 데이터 엑셀(.xlsx)의 시트를 시트별 JSON 파일로 변환(시트 이름이 곧 파일 이름, Monster 시트 -> Monster.json)
::  - 1행은 헤더("필드명:타입", 타입은 int/uint/float/string/bool), 2행부터 데이터. 첫 칸이 #로 시작하는 행과 빈 행은 건너뜀
::  - _로 시작하는 시트는 변환하지 않음(--sheets로 직접 지정하면 변환). 오류가 하나라도 있으면 아무 파일도 쓰지 않음
::  - 종료 코드: 0 성공, 1 변환 실패(데이터 오류), 2 사용법/파일 오류
:: [파라미터 설명]
::      - 1. 엑셀 파일 : K:\GitHub\CPP\Project\Game\GameData\GameData.xlsx
::      - 2. --out 출력 폴더 : K:\GitHub\CPP\Project\Game\GameData
::      - 3. --sheets 변환할 시트 이름(생략 시 _로 시작하지 않는 모든 시트) : Monster Spawn
Bin\x64\Release\XlsxToJson64.exe "K:\GitHub\CPP\Project\Game\GameData\GameData.xlsx" --out "K:\GitHub\CPP\Project\Game\GameData"