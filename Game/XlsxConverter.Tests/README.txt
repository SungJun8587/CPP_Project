XlsxConverter 골든 파일 회귀 테스트
================================

실행 (Python 3 + openpyxl 필요:  pip install openpyxl)
  python run_tests.py --exe ..\Bin\x64\Release\XlsxConverter64.exe
  python run_tests.py --exe ... --case enum_const        한 케이스만
  python run_tests.py --exe ... --update                 현재 출력을 골든(expect\)으로 저장. 저장 후 반드시 diff 로 검토할 것

각 케이스는 sheets\*.tsv 를 openpyxl 로 .xlsx 로 만들어 실제 XlsxConverter 을 실행하고, 결과 파일을 expect\ 와 바이트 단위로 비교한다.
  basic          샘플 GameData.xlsx 전체 (JSON, C++, C#)
  enum_const     enum 자동 증가값, uint32 기반 2^31 이상 값, 선택 enum 열, 모든 Const 타입, 문자열 이스케이프
  err_header     헤더 문법 오류들 (형식, 알 수 없는 타입, 범위 오류, 중복 필드 ...)
  err_enum_const Enum/Const 시트 오류들 (중복, Base, 범위, 식별자 ...)
  err_data       데이터 오류들 (범위, enum 오타, 정수 아님, bool 아님, 빈 칸 ...)
  err_reserved   예약 시트 이름 (Enums, GeneratedCommon)
  err_cpp_keyword C++ 키워드를 enum/멤버 이름으로 쓴 경우
  실패 케이스는 종료 코드 1, expect_stderr.txt 의 문장이 stderr 에 모두 있음, out/cpp/cs 에 파일이 하나도 없음을 확인한다.
  scn/*          여러 번 실행해야 보이는 동작: BOM 유무, 같은 내용은 다시 쓰지 않음, 낡은 생성 파일 정리(손으로 만든 파일은 유지),
                 --incremental 건너뛰기/재생성/인자 변경, 엑셀을 읽을 수 없을 때 경고 후 성공

케이스 추가: cases\<이름>\ 에 sheets\*.tsv (탭 구분, 숫자는 숫자 셀, TRUE/FALSE 는 불리언, 숫자처럼 보이는 문자열은 앞에 ' ), args.txt({out} {cpp} {cs}),
  실패 케이스는 expect_exit(1) 과 expect_stderr.txt, 성공 케이스는 --update 로 expect\ 생성.

주의: 오류 메시지 문구를 바꾸면 expect_stderr.txt 도 고쳐야 한다. 생성 코드 형식을 바꾸면 --update 후 변경 내용을 검토한다.
이 테스트는 xlnt 로 실제 xlsx 를 읽는 경로를 포함한다. (골든 파일은 xlnt 없는 개발 환경에서 같은 데이터를 TSV 스텁으로 읽어 만들었으므로,
처음 실행해 차이가 나면 xlnt 의 셀 값 해석 차이일 수 있으니 diff 를 확인할 것: 예) 숫자 셀의 표기, 빈 문자열 셀)
