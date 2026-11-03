XlsxToJson 골든 파일 회귀 테스트
================================

[원리]
1. cases\<이름>\sheets\*.tsv에 테스트용 엑셀 내용이 들어 있습니다.
2. run_tests.py가 이걸로 .xlsx를 만들고 XlsxToJson을 실제로 실행합니다.
3. 나온 JSON, C++, C# 파일을 미리 저장해 둔 정답 파일(expect\ 폴더, 이게 "골든 파일")과 한 바이트씩 비교합니다.
4. 하나라도 다르면 FAIL과 어느 파일이 다른지를 출력합니다.

[왜 필요한가]
최근 타입 표 통합처럼 내부 구조를 바꿀 때, 출력이 그대로인지 사람이 눈으로 비교하기는 어렵습니다.
이 테스트는 파일 수십 개를 몇 초 만에 비교합니다. 한 글자라도 달라지면 바로 알려줍니다.
잘못된 엑셀을 넣었을 때 의도한 오류 메시지가 나오는지, 그때 파일이 하나도 쓰이지 않는지도 확인합니다. 이 항목이 실패 케이스 5개입니다.
증분 실행, BOM 유무, 낡은 생성 파일 정리처럼 여러 번 실행해야 보이는 동작도 확인합니다.

[사용법]
pip install openpyxl
python run_tests.py --exe ..\Bin\x64\Release\XlsxToJson64.exe

마지막 줄에 20/20 passed가 나오면 정상입니다.

[언제 돌리면 되나]
XlsxToJson 코드를 수정한 직후에 돌리면 됩니다.
생성 코드 형식을 일부러 바꿨다면 --update로 정답 파일을 새로 저장합니다. 이때 바뀐 내용이 의도한 것인지 diff로 확인한 뒤 반영합니다.


실행 (Python 3 + openpyxl 필요:  pip install openpyxl)
  python run_tests.py --exe ..\Bin\x64\Release\XlsxToJson64.exe
  python run_tests.py --exe ... --case enum_const        한 케이스만
  python run_tests.py --exe ... --update                 현재 출력을 골든(expect\)으로 저장. 저장 후 반드시 diff 로 검토할 것

각 케이스는 sheets\*.tsv 를 openpyxl 로 .xlsx 로 만들어 실제 XlsxToJson 을 실행하고, 결과 파일을 expect\ 와 바이트 단위로 비교한다.
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
