GameViewer - MMORPG 필드 전투 C# 뷰어
==========================================

[무엇인가]
  Gateway 클라이언트 포트에 BotClient 처럼 접속(로그인 -> 존 입장)해서 서버가 보내는 전투 패킷을 받아
  숲 필드 위에 영웅/몬스터의 이동, 공격, 스킬(시전/투사체/범위), 피해 숫자, 사망/부활, 상태 효과를
  눈으로 확인하는 도구입니다. 서버와 같은 GameData JSON(Hero/Monster/Skill/Status/HeroSkill/Block/Spawn)을
  읽으므로 이름/사거리/반경/쿨다운이 서버 값과 항상 같습니다.
  서버 없이 화면만 확인하는 데모 모드(--demo)도 있습니다.

[구성]
  Core/    플랫폼 비의존 코어(netstandard2.0, UI/엔진 참조 없음) - Unity 에서 그대로 재사용 가능
    Protocol/  패킷 상수/구조체(Parse/Build), 프레이머 (서버 ZonePacket.h 와 1:1)
    Net/       GatewayClient (수신 스레드 + 큐, 소비는 호출 스레드)
    World/     WorldModel(엔티티/연출/로그), Entity(이동 보간), ViewEffect(연출 데이터)
    Data/      MiniJson, GameData(서버와 같은 JSON 로더)
    Logic/     AutoPilot(자동 전투), DemoDirector(서버 없는 로컬 시뮬레이션)
    ViewerSession.cs  접속 흐름 + 월드 + 명령(이동/공격/스킬/쿨다운)
    ViewerConfig.cs   JSON + 명령행 옵션
  Assets/  (선택) 이미지 에셋과 sprites.json - 아래 [이미지 에셋]
  App/     WinForms 뷰어 (GDI+ 더블 버퍼). Render/ 는 System.Drawing 만 쓰는 렌더러
  Tests/   콘솔 테스트 (가짜 Gateway 왕복 포함)
  Tools/   RenderSnapshot - 서버/창 없이 데모 장면을 PNG 로 저장 (렌더러 확인용)
           <GameData> <outDir> [초...]   장면 저장 (환경변수 SNAP_BOSS=1 이면 영웅이 보스를 쫓고, SNAP_CAM=x,y 로 카메라 고정)
           --sheet <outDir> [배율]        스프라이트 시트    --bench <GameData> [프레임]  부분별 그리기 시간
           --selftest <GameData>          전 스프라이트/상태/해상도/배율 그리기 점검 + 이미지 에셋 로더 점검(합성 이미지)
  GameViewer.json / GameViewer.bat / GameViewer_Demo.bat

[빌드]
  GameViewer.sln 을 Visual Studio 로 열어 빌드 (".NET 데스크톱 개발" 워크로드, .NET Framework 4.8).
  .NET 8+ 로 올리려면 App.csproj 의 TargetFramework 를 net8.0-windows 로 바꾸면 됩니다.

[실행]
  1) 서버: GatewayServer 와 ZoneServer 를 띄움
  2) GameViewer.bat 실행  (GameViewer.json 의 값 사용, 명령행 옵션이 우선)
     - GameViewer.bat 안의 DATA 를 서버가 읽는 GameData 폴더로 맞춰 주세요.
     - 서버 없이 화면만 보려면 GameViewer_Demo.bat
  3) BotClient 를 같이 돌리면 뷰어 주변에서 봇들이 몬스터와 싸우는 모습을 볼 수 있습니다.
  종료 코드: 0 정상, 1 옵션 오류

[조작]
  좌클릭   땅 = 이동 / 몬스터 = 선택 + 쫓아가서 공격(스킬 포함)
  우클릭   누른 채 이동(MMO 방식), 몬스터 우클릭 = 공격
  1~9      스킬(영웅 스킬 순서). 단일 스킬은 선택한 대상(없으면 가장 가까운 몬스터), 범위 스킬은 대상 위치/커서 위치
  Q        기본 공격(대상 선택 후 접근-공격)    Tab/Shift+Tab 대상 전환    Esc 취소
  A        자동 전투(가까운 몬스터를 찾아 싸우고, 없으면 스폰 지점을 순회)
  F        시점 고정 on/off (끄면 방향키로 이동)    휠, +/- 확대/축소    Space 내 위치로
  F5 재접속   F1 도움말   L 로그 표시
  B        보스 선택(가장 가까운 보스)    F12  스크린샷 저장(실행 파일 옆 screenshots 폴더)
  미니맵 클릭  그 위치로 이동    휠/+- 확대·축소는 부드럽게 보간

[화면]
  숲 바닥/나무/바위(Block.json 의 Circle=나무, Rect=바위), 영웅(전사/궁수/마법사 색과 무기), 몬스터(종류별 색),
  이름/체력바/시전바/상태 아이콘, 범위 스킬 예고(시전 중 붉은 원)와 폭발 링, 투사체(화살/화염구, 단일 대상은 추적),
  피해/회복/상태 문구, 무적 표시, 하단 체력/스킬바(쿨다운), 우상단 미니맵(스폰 지점/장애물/엔티티), 좌하단 로그
  (공격/스킬 거부, 위치 보정 사유가 표시되어 서버 규칙 점검에 유용).

[보스 표시]
  GameData 에 Boss/BossPhase JSON 이 있으면(없으면 보스 표시 없이 동작) 몬스터 템플릿 ID 로 보스를 알아보고 다음을 그립니다.
  상단 보스 HP 바(페이즈 눈금, 지연 체력, 페이즈 번호), 페이즈 진입 배너와 화면 흔들림, 격노 표시와 화면 가장자리 붉은 테두리,
  범위 스킬 예고(시전 시간만큼 차오르는 위험 지역)와 시전 바, 미니맵 보스 표시, 대상 프레임(상태 효과 남은 시간).
  패킷은 추가하지 않았습니다. 페이즈는 체력 비율로 추정합니다(서버 규칙과 같은 hp*100 <= HpBelowPercent*maxHp, 사망/부활 전까지 내려가지 않음).
  격노는 EnrageStatusId 상태 효과가 걸렸는지로 판단합니다. 데모 모드 보스(오우거)는 이 표시를 확인하기 위한 단순한 로컬 패턴이며 서버 로직과 다릅니다.

[그래픽]
  외부 이미지 없이 코드로 그린 음영 스프라이트입니다(사실적인 사진 수준이 아님). 영웅/몬스터 종류별 걷기/공격/시전/피격/사망 프레임,
  방향 반전, 그림자, 피격 번쩍임, 사망 시 시체. 스프라이트는 배율별로 캐시하고, 바닥은 큰 비트맵을 캐시해 카메라가 여유분을 벗어날 때만 다시 그립니다.
  효과(타격 불꽃, 범위 폭발, 투사체 꼬리, 치유/상태 효과)는 시작 시각에서 계산하는 무상태 방식입니다. 프레임은 오프스크린 비트맵에 그린 뒤 복사합니다.
  Preview/ 에 스프라이트 시트, 보스 장면, 창 캡처 예시가 있습니다.

[이미지 에셋]  (선택. 사진/3D 렌더 같은 그림을 쓰려면)
  실행 파일 옆 Assets\sprites.json 에 적은 종류만 이미지로 그리고, 나머지는 위의 코드로 그린 그림을 그대로 씁니다. 파일이 없으면 모두 코드 그림입니다.
    { "sprites": [ { "kind": "monster", "template": 9, "pattern": "demon/{0:000}.png", "angles": 18, "height": 4.6 } ] }
    kind monster|hero|tree, template = Monster/Hero 시트의 Id (tree 는 없음: Block 의 원 = 나무 전부를 이 이미지로 그림, height 는 반경 1 일 때 높이), pattern = sprites.json 기준 경로({0} = 각도(도)), angles = 방향 수(1..72),
    height = 그림 내용이 차지할 높이(단위, 0.5..12). 각도 = 번호*360/angles. 0도는 화면 아래(카메라 쪽)를 보는 모습이고
    커질수록 화면 오른쪽 -> 위 -> 왼쪽으로 돕니다. 이미지는 PNG/JPG/GIF/BMP(투명 배경은 PNG)이며 WebP 는 GDI+ 가 못 읽습니다.
  - 모든 방향의 불투명 영역을 합친 상자로 발 밑과 크기를 잡으므로 돌 때 흔들리지 않고, 클릭 판정/체력바 높이도 이 크기를 따릅니다.
  - 프레임 애니메이션은 없습니다. 걸을 땐 튀고 설 땐 숨 쉬듯 눌러서 흉내 내며, 피격 번쩍임/시체는 같은 방식으로 처리합니다.
  - 잘못된 항목(파일 없음, 크기 불일치, 값 범위 등)은 그 항목만 건너뛰고 표준 오류에 "assets: ..." 로 적습니다.
  - 샘플: Assets\demon\ 의 악마를 오우거(몬스터 9)에, Assets\slime\ 의 점액 괴물을 슬라임(몬스터 1)에, Assets\mushroom\ 의 버섯 전사를 고블린(몬스터 2)에, Assets\dragonkid\ 의 용 옷 아이를 영웅 1(전사)에, Assets\spruce\ 의 가문비나무를 나무에 연결해 두었습니다(각 18방향, 20도 간격).
    원본은 Pixabay 무료 3D 모델 페이지의 회전 렌더이고, WebP 였던 프레임은 PNG 로 바꾸고 400x400 으로 줄였습니다. 쓰기 전에 Pixabay 콘텐츠 라이선스를 확인하세요. 연결을 빼려면 sprites.json 의 항목을 지우면 됩니다.
  - 샘플 두 개와 코드로 그린 다른 몬스터는 화풍이 다릅니다. 통일하려면 같은 화풍의 에셋을 전 종류에 맞춰 넣어야 합니다.

[설정 JSON 키]  Host, Port, CharId, ZoneId, HeroId, DataDir, Auto, PixelsPerUnit, WindowWidth, WindowHeight,
                ConnectTimeoutMs, MoveIntervalMs  (--dump-config <file> 로 기본값 템플릿 저장)

[서버와의 약속]
  - 내 엔티티 ID = 로그인한 CharId (BotClient 와 같음). 몬스터 ID 는 최상위 비트가 켜진 값.
  - 이동 요청에는 로컬에서 보간한 내 위치를 실어 보냅니다. 서버 허용 오차를 넘으면 ZcCorrectPosNotify 가 오고
    로그에 "위치 보정: PositionMismatch" 로 표시됩니다(지연이 큰 환경에서 늘어날 수 있음).
  - 투사체는 서버에 엔티티가 없으므로 뷰어가 발사/착탄 패킷으로 연출만 합니다(Single 은 대상 추적).
  - 사망 시 상태 효과는 해제 알림 없이 사라지므로 ZcDeathNotify 에서 비웁니다.
  - 시야(AOI) 밖은 서버가 보내지 않으므로 화면에는 내 캐릭터 주변만 나옵니다.
  - 영웅 ID 0(서버 기본 영웅)이면 스폰 알림의 templateId 로 영웅을 확인합니다.

[Unity 클라이언트에서 재사용]
  Core 폴더를 Assets 아래에 그대로 복사하고(.csproj 제외) 다음 순서로 씁니다. (netstandard2.0 / C# 7.3 문법만 사용)
    var data = GameData.Load(path, zoneId);                // 또는 JSON 을 직접 읽어 GameData 를 채움
    var session = new ViewerSession(cfg, data);  session.Start(nowMs);
    void Update() { session.Update(nowMs); pilot.Update(nowMs); ... }     // nowMs = Time.realtimeSinceStartup * 1000
    session.World.Entities / Effects / Log 를 읽어 GameObject, 파티클, 텍스트로 표현
    입력 -> session.MoveTo / Attack / UseSkill
  App/Render 의 그리기 코드만 Unity 용으로 바꾸면 되고, 패킷/월드/AI 는 그대로입니다.
  ZoneCommon/ZonePacket.h 가 바뀌면 Core/Protocol/Packets.cs 를 같이 고치고, Tests 의 cpp_packet_sizes.txt 를
  새 sizeof 값으로 갱신하면 크기 불일치를 테스트가 잡아 줍니다.

[검증]
  Tests: 패킷 27종 크기를 C++ sizeof 와 대조, 프레이머(분할/병합/위반), 파서, 월드 모델(이동 보간/피해/사망/부활/
  스킬/투사체/상태), 데이터 로더, 설정, 가짜 Gateway 와 TCP 왕복(로그인 실패/입장 실패/존 소실/연결 끊김/프로토콜 위반),
  자동 전투(접근 -> 스킬 -> 기본 공격 -> 순회), 데모 시뮬레이션 40초 불변식,
  보스(데이터/페이즈 추정/지연 체력/흔들림/걷기 판정/데모 보스) - 모두 290개 점검.
  샌드박스(mono)에서 전부 통과했고, WinForms 앱도 가상 디스플레이에서 실행해 화면을 확인했습니다.
  mono 소프트웨어 렌더링 기준 프레임당 약 9ms(이전 8ms). 스프라이트 모양/애니메이션은 mono 에서 맞췄습니다.
  아직 확인하지 못한 것: Visual Studio/dotnet 으로의 빌드, Windows(GDI+) 실행과 성능, 실제 Gateway/ZoneServer 와의 접속(보스 표시 포함).
