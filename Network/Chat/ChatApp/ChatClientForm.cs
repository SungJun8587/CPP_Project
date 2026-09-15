
//***************************************************************************
// ChatClientForm.cs : 메인 창 — 접속/채팅/닉네임 변경/방 입퇴장 UI.
//
// [설계] Designer.cs 없이 코드로 컨트롤을 직접 배치했다(단일 파일 전달을
// 위한 단순화 — 실제 프로젝트라면 Visual Studio 디자이너로 분리하는 걸
// 권장). ChatNetworkClient의 이벤트는 백그라운드 스레드에서 오므로, 전부
// Invoke()로 UI 스레드에 넘긴 뒤에야 컨트롤을 건드린다.
//
// [UI 구조 — 참고 프로젝트와의 차이] 이 레이아웃은 사용자가 준 SignalR
// 기반 채팅 클라이언트 예시(그룹박스 2개 + OwnerDraw 리스트박스 2개 +
// 방 콤보박스/입장/퇴장/방 인원수)의 골격을 그대로 따랐다 — 서버에
// 로비/룸 개념이 추가되면서 이제 이 컨트롤들이 실제로 동작한다. 다만
// 아래는 여전히 대응 기능이 없어서 뺐다:
//   - 동접자수(전체 서버 접속자 수) 조회 — 프로토콜에 없음. 방 인원수는 있음.
//   - URL/FpID 필드 — SignalR(HTTP) 전용 개념. 이 프로젝트는 순수 TCP라
//     "서버 IP + 포트 + 프로필 이름"으로 대체했다.
//   - "유저 가입"/"연결" 버튼 분리 — 이 프로젝트의 Connect()는 로컬 저장된
//     계정 유무로 신규가입/재접속을 자동 판별하므로 버튼 하나로 통합.
//
// [설계 — 로비/룸] 로그인하면 서버가 자동으로 로비(RoomId=0)에 배정한다.
// cbChatRoomId에서 방을 골라 "입장" 하면 그 방으로 이동, "나가기"를 누르면
// 로비로 돌아간다. 방 인원수는 서버가 RoomEnterRes/RoomLeaveRes 응답
// 뿐만 아니라 RoomUserCountNotify로도 실시간 갱신해준다 — 내가 직접
// 입/퇴장하지 않아도 같은 방에 있는 "다른" 누군가가 들고나면 자동으로
// 화면 숫자가 바뀐다.
//
// [설계 — 보낸/받은 메시지 구분] 서버(ChatMessageHandler.cpp)는 채팅
// 메시지를 발신자 포함, 지금 있는 방/로비 전체에게 브로드캐스트한다.
// "이게 내가 보낸 게 되돌아온 건지"는 프로토콜만으로 구분이 안 돼서,
// 메시지를 보낼 때 그 문자열을 _pendingSentEchoes 큐에 넣어두고, 도착한
// 메시지가 큐 맨 앞과 일치하면 그 서버 에코를 조용히 소비해 중복 표시를 막는다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Forms;
// [수정 — 컴파일 오류] 이 프로젝트 설정(암시적 using 등)에서 System.Net.Mime.MediaTypeNames에도
// Font/Image라는 이름의 클래스가 있어서, System.Drawing.Font/Image와 이름이 겹쳐 모호한
// 참조 오류가 났다. 타입 별칭으로 System.Drawing 쪽을 명시적으로 고정한다.
using Font = System.Drawing.Font;
using Image = System.Drawing.Image;

namespace ChatApp
{
    public class ChatClientForm : Form
    {
        //***************************************************************************
        // @brief listBoxMsg(시스템 로그)에 OwnerDraw로 색깔 있는 한 줄을 넣기 위한 항목.
        //***************************************************************************
        private class ColoredEntry
        {
            public string Text;
            public Color Color;
            public DateTime Timestamp;
            public override string ToString() => Text; // 접근성 등 폴백 경로용
        }

        //***************************************************************************
        // @brief listBoxChat(채팅 로그)에 카카오톡 스타일 말풍선으로 그리기 위한 항목.
        //        내가 보낸 메시지는 우측 정렬(노란 계열), 상대는 좌측 정렬(흰색)로 그린다.
        //***************************************************************************
        private class ChatBubbleItem
        {
            public string SenderName;
            public string SenderProfileImageUrl;
            public string Message;
            public bool IsMyMessage;
            public DateTime Timestamp;

            public ChatBubbleItem(string senderName, string senderProfileImageUrl, string message, bool isMyMessage)
            {
                SenderName = senderName;
                SenderProfileImageUrl = senderProfileImageUrl;
                Message = message;
                IsMyMessage = isMyMessage;
                Timestamp = DateTime.Now;
            }
        }

        //***************************************************************************
        // @brief 채팅 로그 중간에 끼워 넣는 날짜 구분선("2026년 9월 15일 월요일").
        //        ChatBubbleItem이 아닌 항목은 ChatListBox_DrawItem의 기존
        //        폴백 경로(ToString()을 가운데 정렬 회색 텍스트로 그림)를 그대로
        //        타므로, 별도 그리기 코드 없이 이 클래스만 추가하면 된다.
        //***************************************************************************
        private class DateSeparatorItem
        {
            public DateTime Date;
            public override string ToString() => Date.ToString("yyyy년 M월 d일 dddd", new System.Globalization.CultureInfo("ko-KR"));
        }

        private TextBox _txtServerIp;
        private TextBox _txtServerPort;
        private TextBox _txtProfileName;
        private Button _btnConnect;
        private Label _lblStatus;
        private Panel _pnlStatusDot;
        private Label _txtServerUserCount;
        private System.Windows.Forms.Timer _serverUserCountPollTimer;

        private string _currentNickname; // 화면에 별도로 표시하지 않고, 닉네임 변경 다이얼로그에 넘겨줄 용도로만 보관
        private string _myProfileImageUrl = string.Empty; // 서버에 현재 설정돼 있는(=다른 사람에게 보이는) 내 프로필 이미지 URL
        private string _pendingProfileImageUrlRequest; // SetMyProfileImageUrl()/ClearMyProfileImageUrl()이 요청한 값 — 응답(success/reason만 있음) 처리 시 참고용

        // [추가] UploadMyProfileImage()가 await로 기다리는 업로드 토큰 발급 응답.
        // 업로드가 진행 중이 아닐 때(null)는 이벤트가 와도 조용히 무시된다.
        private TaskCompletionSource<RequestUploadTokenResData> _uploadTokenTcs;
        private Button _btnOpenNicknameDialog;
        private TabControl _tabControl;
        private Panel _headerPanel;
        private Label _lblAppName;
        private Button _btnSkin;
        private Panel _chatPagePanel;
        private Panel _galleryPagePanel;
        private ProfileImageGalleryPanel _galleryPanel;

        // [추가] 프로필 이미지 — 로컬 전용(네트워크로 다른 사람에게 전송되지
        // 않음). 프로필 이름별로 이미지 파일 경로를 저장해뒀다가 다음 실행 때
        // 다시 불러온다.
        private PictureBox _picProfileImage;

        private ComboBox _cbChatRoomId;
        private Button _btnRoomEnter;
        private Button _btnRoomLeave;
        private Label _txtLobbyUserCount;
        private Label _txtRoomUserCount;
        private Label _lblCurrentRoom;

        private TextBox _txtMessage;
        private Button _btnSend;
        private ListBox _listBoxChat;
        // [추가] 참고 코드는 DrawItem마다 Font를 새로 만들고 버렸는데, 매 프레임
        // GDI 리소스를 할당/해제하는 건 낭비라 필드로 캐시해서 재사용한다.
        // Dispose()에서 함께 정리한다.
        private Font _nameFont;

        // [추가] 채팅 메시지 안의 URL을 감지해 파란 밑줄로 표시하고 클릭하면
        // 브라우저로 여는 기능. UrlRegex로 찾은 URL의 실제 그려진 위치(픽셀
        // 좌표)를 DrawItem 시점에 이 캐시에 저장해뒀다가, MouseClick/MouseMove가
        // 그 좌표로 히트테스트한다 — ListBox 자체엔 "이 글자를 클릭했다"를
        // 알려주는 기능이 없어서 직접 구현해야 한다.
        private static readonly Regex UrlRegex = new Regex(@"https?://[^\s]+", RegexOptions.IgnoreCase | RegexOptions.Compiled);
        private readonly Dictionary<int, List<(string Url, RectangleF Bounds)>> _chatItemUrlRegions
            = new Dictionary<int, List<(string Url, RectangleF Bounds)>>();
        private Font _timeFont;

        // [추가] 말풍선 그리기용 색상 브러시 — DrawItem마다 새로 만들면 GDI
        // 핸들이 누적되므로(스크롤/재도색 때마다 호출됨), 폰트와 같은 방식으로
        // 한 번만 만들어 재사용한다.
        private Brush _myBubbleBrush;
        private Brush _myTextBrush;
        private Brush _otherTextBrush;
        private Brush _bubbleNameBrush;
        private Brush _bubbleTimeBrush;

        //***************************************************************************
        // @brief 링크(URL) 미리보기 — 카카오톡처럼 메시지 속 URL의 제목/설명/
        //        썸네일을 비동기로 가져와 메시지 아래 카드로 보여준다.
        // @details URL 하나당 한 번만 요청한다(_linkPreviewCache에 캐시) — 같은
        //          링크를 여러 사람이 반복해서 보내도 네트워크 요청은 한 번뿐이다.
        //          HTML은 직접 실행/렌더링하지 않고, 정규식으로 메타 태그만
        //          뽑아 텍스트/이미지로만 쓴다.
        // @details [보안 주의] 채팅 메시지에 적힌 임의의 URL로 클라이언트가
        //          자동으로 HTTP 요청을 보낸다 — 이건 사실상 SSRF 성격의
        //          위험을 내포한다(내부망 주소, 파일 서버 등으로 유도될 수
        //          있음). 신뢰할 수 있는 사용자들만 쓰는 사내/사설 채팅용
        //          데모로 한정해서 판단했다 — 공인망에 노출하는 서비스라면
        //          허용 도메인 화이트리스트, private IP 대역 차단 등의
        //          추가 방어가 필요하다.
        //***************************************************************************
        private class LinkPreviewData
        {
            public string Title;
            public string Description;
            public Image Thumbnail;
            public bool IsLoading = true;
            public bool Failed;
        }

        private static readonly HttpClient _httpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        // [추가] 파일 업로드는 크기(최대 2MB)와 파일 서버의 디스크 I/O 때문에
        // 링크 미리보기/아바타 fetch(_httpClient, 5초)보다 오래 걸릴 수 있다 —
        // HttpClient.Timeout은 인스턴스 전체에 적용되는 값이라(호출마다 다르게
        // 줄 수 없음) 업로드 전용으로 타임아웃을 넉넉히 둔 별도 인스턴스를 쓴다.
        private static readonly HttpClient _uploadHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(60) };
        private readonly Dictionary<string, LinkPreviewData> _linkPreviewCache = new Dictionary<string, LinkPreviewData>();

        // [추가] 프로필 이미지 — 채팅 메시지에 실려오는 발신자의 profileImageUrl로
        // 실제 이미지를 내려받아 아바타 자리에 표시한다(링크 미리보기와 동일한
        // fetch 인프라/HttpClient를 재사용). URL 하나당 한 번만 요청(캐시).
        // 아직 못 받아왔거나 실패했으면 DrawAvatar()가 색깔 원형+이니셜로 대체한다.
        private readonly Dictionary<string, Image> _avatarImageCache = new Dictionary<string, Image>();
        private readonly HashSet<string> _avatarFetchInProgress = new HashSet<string>();

        private const int kLinkPreviewCardHeight = 66;
        private const int kLinkPreviewThumbnailSize = 56;

        private ListBox _listBoxMsg;

        private ChatNetworkClient _client;
        private string _profileName;
        private int _currentRoomId = -1; // -1: 아직 로그인 전(로비 배정 전). 로그인하면 서버가 0(로비)으로 넣어준다.

        // 보낸/받은 메시지 구분용 — UI 스레드(Send)와 네트워크 수신 스레드
        // (OnChatMessageReceived, Invoke() 이전) 양쪽에서 건드리므로 락 필요.
        private readonly Queue<string> _pendingSentEchoes = new Queue<string>();
        private readonly object _pendingSentEchoesLock = new object();

        private static readonly Color ColorSystemOk = Color.Green;
        private static readonly Color ColorSystemError = Color.Firebrick;
        private static readonly Color ColorSystemInfo = Color.Gray;

        public ChatClientForm()
        {
            InitializeComponents();
        }

        // ── 디자인 시스템 — 갤러리 탭(ProfileImageGalleryPanel)과 동일한
        // 팔레트를 대화 탭에도 그대로 적용해 두 탭이 한 앱처럼 보이게 한다.
        // [수정 — 윈도우 스킨 기능] 예전엔 이 색상들이 컴파일 타임에 고정된
        // static readonly 필드였는데, 이제 CurrentTheme(교체 가능한 인스턴스)를
        // 두고 각 색상은 거기서 값을 가져오는 static 프로퍼티로 바꿨다.
        // 이렇게 하면 기존에 AccentColor/BorderColor 등을 참조하던 코드
        // (카드 Paint 이벤트, 말풍선 DrawItem, 갤러리 타일 등 대부분 페인트
        // 시점에 값을 다시 읽는 코드)는 한 줄도 안 고쳐도 CurrentTheme만
        // 바꾸고 다시 그리면 새 색으로 반영된다. 다만 Button.BackColor처럼
        // 생성 시점에 값을 그대로 굳혀버린 것들은 ApplyTheme()에서 별도로
        // 다시 씌워줘야 한다.
        // [수정] ChatTheme 클래스 자체는 ProfileImageGalleryPanel.cs도 같이
        // 참조해야 해서 이 클래스 밖(네임스페이스 레벨)으로 옮겼다 —
        // ChatTheme.Current가 앱 전체가 공유하는 단일 진실 공급원이다.
        private static ChatTheme CurrentTheme => ChatTheme.Current;

        private static Color AccentColor => CurrentTheme.Accent;
        private static Color SuccessColor => CurrentTheme.Success;
        private static Color DangerColor => CurrentTheme.Danger;
        // [수정] 스킨은 버튼/테두리/말풍선 색상에만 적용되고, 창/페이지
        // 배경은 항상 고정된 밝은 색을 쓴다 — Label의 "투명 배경" 렌더링이
        // 커스텀 그리기 부모(CardPanel) 위에서 부모의 부모(폼) 배경색을
        // 잘못 참조하는 WinForms의 알려진 문제가 있어서, 폼 배경이 어두운
        // 색으로 바뀌면 카드 위 라벨들 뒤에 어두운 상자가 비쳐 보이는
        // 문제가 있었다. 창 배경 자체를 스킨과 무관하게 고정하면 이 문제도
        // 같이 해결된다.
        private static Color PageBackColor => ChatTheme.Blue.PageBack;
        private static Color BorderColor => CurrentTheme.Border;
        private static Color TextSecondaryColor => CurrentTheme.TextSecondary;
        private static Color TextMutedColor => CurrentTheme.TextMuted;

        //***************************************************************************
        // @brief GroupBox의 투박한 테두리 대신 쓰는 둥근 모서리 흰 카드 패널.
        //        ProfileImageGalleryPanel의 GalleryTile과 동일한 방식(GraphicsPath
        //        직접 그리기)으로 앤티앨리어싱된 모서리를 낸다 — Region 클리핑만
        //        쓰면 작은 반지름에서 계단현상이 보이기 때문.
        //***************************************************************************
        private class CardPanel : Panel
        {
            public CardPanel()
            {
                DoubleBuffered = true;
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                var g = e.Graphics;
                g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                var rect = new Rectangle(0, 0, Width - 1, Height - 1);
                // 내부 콘텐츠 여백이 다시 넉넉해졌으니(11~14px) 모서리
                // 반지름도 적당히 복원 — 4는 지나치게 각져 보여서 8로.
                using (var path = RoundedRectPath(rect, 8))
                {
                    using (var b = new SolidBrush(Color.White))
                        g.FillPath(b, path);
                    // [수정] 중립 회색(BorderColor) 대신 스킨의 액센트 색을
                    // 그대로 써서, 카드 테두리에서도 지금 선택된 스킨이
                    // 눈에 띄게 반영되게 한다.
                    using (var pen = new Pen(AccentColor))
                        g.DrawPath(pen, path);
                }
            }
        }

        private static System.Drawing.Drawing2D.GraphicsPath RoundedRectPath(Rectangle bounds, int radius)
        {
            int d = radius * 2;
            var path = new System.Drawing.Drawing2D.GraphicsPath();
            path.AddArc(bounds.X, bounds.Y, d, d, 180, 90);
            path.AddArc(bounds.Right - d, bounds.Y, d, d, 270, 90);
            path.AddArc(bounds.Right - d, bounds.Bottom - d, d, d, 0, 90);
            path.AddArc(bounds.X, bounds.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        //***************************************************************************
        // @brief 버튼을 채워진 액센트 색(filled) 또는 테두리만 있는 아웃라인
        //        스타일로 통일해서 꾸민다. 모서리도 살짝 둥글게(Region).
        // @param dangerOutline true면 아웃라인 글자색을 위험(빨강)으로 — 삭제/
        //        나가기 계열 버튼용.
        //***************************************************************************
        //***************************************************************************
        // @brief 말풍선 그리기용 캐시 브러시를 현재 테마(CurrentTheme) 색상으로
        //        (다시) 만든다. 초기화 시점과 스킨 전환 시점 둘 다에서 호출된다.
        //        기존 브러시가 있으면 GDI 핸들 누수 방지를 위해 먼저 Dispose한다.
        //***************************************************************************
        private void RebuildBubbleBrushes()
        {
            (_myBubbleBrush as IDisposable)?.Dispose();
            (_myTextBrush as IDisposable)?.Dispose();
            (_otherTextBrush as IDisposable)?.Dispose();
            (_bubbleNameBrush as IDisposable)?.Dispose();
            (_bubbleTimeBrush as IDisposable)?.Dispose();

            _myBubbleBrush = new SolidBrush(CurrentTheme.MyBubble);
            _myTextBrush = new SolidBrush(CurrentTheme.MyText);
            _otherTextBrush = new SolidBrush(CurrentTheme.OtherText);
            _bubbleNameBrush = new SolidBrush(TextSecondaryColor);
            _bubbleTimeBrush = new SolidBrush(TextMutedColor);
        }

        //***************************************************************************
        // @brief 스킨(테마)을 교체하고, 생성 시점에 색이 굳어버린 컨트롤들
        //        (버튼의 BackColor/ForeColor 등)을 다시 씌운 뒤 전체를 다시
        //        그린다. 카드(CardPanel)/탭 밑줄/말풍선처럼 Paint 이벤트에서
        //        색을 그때그때 다시 읽는 컨트롤들은 Invalidate(true) 한 번으로
        //        자동 반영된다. [설계] 창/페이지/헤더 배경은 스킨과 무관하게
        //        항상 고정된 밝은 색을 유지한다(PageBackColor 선언부 참고) —
        //        그래서 여기서 배경색을 다시 씌우는 코드가 없다.
        //***************************************************************************
        private void ApplyTheme(ChatTheme theme)
        {
            ChatTheme.Current = theme;
            RebuildBubbleBrushes();
            // [수정] 채팅 리스트박스 배경은 이제 스킨과 무관하게 고정이라
            // 여기서 다시 씌울 필요가 없다(ChatTheme.FixedChatBackground).

            if (_picProfileImage != null && _picProfileImage.Image == null)
                _picProfileImage.BackColor = ControlPaint.Light(AccentColor, 0.9f);

            // 버튼들 — 처음 만들 때와 같은 filled/outline/danger 조합으로 재적용.
            StyleButton(_btnConnect, filled: true);
            StyleButton(_btnOpenNicknameDialog, filled: false);
            RefreshDynamicButtonColors(_btnRoomEnter);
            RefreshDynamicButtonColors(_btnRoomLeave);
            RefreshDynamicButtonColors(_btnSend);
            if (_btnSkin != null) RefreshDynamicButtonColors(_btnSkin);

            AppendSystemLog($"[시스템] 스킨을 '{theme.Name}'(으)로 변경했습니다.", ColorSystemInfo);

            // Invalidate(true)는 이미 재귀적으로 모든 자식 컨트롤까지 다시
            // 그리게 하므로 별도로 자식을 순회할 필요는 없다.
            // ProfileImageGalleryPanel(갤러리 탭)은 별도 클래스라 자기 색상도
            // 직접 갱신해줘야 한다 — 같은 ChatTheme.Current를 참조하지만
            // 버튼/라벨의 BackColor/ForeColor는 생성 시점에 굳어있으므로.
            _galleryPanel?.RefreshTheme();

            Invalidate(true);
        }

        //***************************************************************************
        // @brief 활성/비활성 상태에 따라 자동으로 모양이 바뀌는 버튼 스타일.
        //        활성화(Enabled=true)면 접속 버튼과 같은 "채워진" 모양(액센트
        //        배경+흰 글씨), 비활성화면 회색 배경+회색 글씨로 확실히
        //        구분되게 한다. 테두리는 상태와 무관하게 항상 그려서 배경과
        //        분리돼 보이게 한다("버튼임"이 항상 눈에 띄어야 하므로).
        //***************************************************************************
        private static void RefreshDynamicButtonColors(Button btn)
        {
            btn.FlatAppearance.BorderSize = 1;

            if (btn.Enabled)
            {
                btn.BackColor = AccentColor;
                btn.ForeColor = Color.White;
                btn.FlatAppearance.BorderColor = AccentColor;
            }
            else
            {
                btn.BackColor = Color.FromArgb(236, 238, 241);
                btn.ForeColor = TextMutedColor;
                btn.FlatAppearance.BorderColor = BorderColor;
            }
        }

        //***************************************************************************
        // @brief RefreshDynamicButtonColors()를 최초 1회 적용하고, 이후
        //        Enabled 값이 바뀔 때마다(방 입장/퇴장, 접속/해제 등으로
        //        Enabled = true/false가 바뀔 때) 자동으로 다시 적용되도록
        //        EnabledChanged에 걸어둔다. 스킨이 바뀔 때는 이 이벤트가 아니라
        //        ApplyTheme()가 RefreshDynamicButtonColors()를 직접 다시
        //        호출한다(새 액센트 색 반영 목적 — Enabled 값 자체는 안
        //        바뀌므로 EnabledChanged가 안 불림).
        //***************************************************************************
        private static void StyleDynamicButton(Button btn)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.Cursor = Cursors.Hand;
            btn.EnabledChanged += (s, e) => RefreshDynamicButtonColors(btn);
            RefreshDynamicButtonColors(btn);
        }

        private static void StyleButton(Button btn, bool filled, bool dangerOutline = false)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.FlatAppearance.BorderSize = filled ? 0 : 1;
            btn.FlatAppearance.BorderColor = BorderColor;
            btn.Cursor = Cursors.Hand;

            if (filled)
            {
                btn.BackColor = AccentColor;
                btn.ForeColor = Color.White;
            }
            else
            {
                btn.BackColor = Color.White;
                btn.ForeColor = dangerOutline ? DangerColor : AccentColor;
            }

            // [수정] Region으로 둥근 모서리를 주면 FlatStyle.Flat의 네이티브
            // 테두리(FlatAppearance.BorderSize/BorderColor) 렌더링이 그 Region에
            // 함께 잘려나가 버린다 — 특히 아웃라인 버튼은 배경이 흰색이라
            // 테두리가 없으면 카드 배경과 구분이 안 되고, 텍스트도 Region
            // 경계에 걸려 잘려 보일 수 있다(실제로 "배경 설정"이 "배경 설"로
            // 잘려 보인 원인). 그래서 일반 버튼엔 Region을 안 쓴다 — 각진
            // 사각형 flat 버튼으로도 충분히 깔끔하다. 완전히 직접 그리는
            // 커스텀 컨트롤(GalleryTile, CircleBadgeButton처럼 OnPaint를
            // 전부 스스로 담당하는 경우)은 이 문제가 없어서 그쪽만 Region을 쓴다.
        }

        private void InitializeComponents()
        {
            Text = "채팅 클라이언트 (WinForms)";
            ClientSize = new Size(577, 704);
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            DoubleBuffered = true;
            BackColor = PageBackColor;

            // ── 카드 1: 서버 접속 ──────────────────────────────────────
            var groupBox1 = new CardPanel { Left = 3, Top = 9, Width = 562, Height = 85 };
            var lblCard1Title = new Label
            {
                Text = "서버 접속",
                Left = 14,
                Top = 4,
                Width = 100,
                Height = 14,
                ForeColor = TextSecondaryColor,
                Font = new Font(Font.FontFamily, 8f, FontStyle.Bold),
            };

            var lblIp = new Label { Text = "서버 IP", Left = 14, Top = 25, Width = 45 };
            _txtServerIp = new TextBox { Left = 63, Top = 21, Width = 120, Text = "127.0.0.1" };

            var lblPort = new Label { Text = "포트", Left = 193, Top = 25, Width = 35 };
            _txtServerPort = new TextBox { Left = 228, Top = 21, Width = 60, Text = "30201" };

            var lblProfile = new Label { Text = "프로필 이름", Left = 14, Top = 51, Width = 80 };
            _txtProfileName = new TextBox { Left = 93, Top = 47, Width = 150 };

            // [추가] 프로필 이름 옆의 닉네임 변경 버튼 — 서버에 로그인 성공한
            // 뒤에만 보인다(Visible, 단순 Enabled가 아님 — 로그인 전에는
            // 아예 존재를 드러내지 않는다는 요구사항). 누르면 별도 팝업
            // (NicknameChangeDialog)이 뜨고, 그 안에 자동 생성/변경 버튼이 있다.
            _btnOpenNicknameDialog = new Button { Text = "닉네임 변경", Left = 248, Top = 46, Width = 90, Height = 25, Visible = false };
            _btnOpenNicknameDialog.Click += BtnOpenNicknameDialog_Click;
            StyleButton(_btnOpenNicknameDialog, filled: false);

            // [추가] 프로필 이미지 — 클릭하면 파일 선택 창이 뜬다. 기본은
            // 회색 빈 칸(이미지 없음 표시)이고, 로그인 전에도 조작 가능
            // (네트워크와 무관한 순수 로컬 개인화 기능이므로).
            _picProfileImage = new PictureBox
            {
                Visible = false,
                Left = 343,
                Top = 44,
                Width = 28,
                Height = 28,
                SizeMode = PictureBoxSizeMode.StretchImage,
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = ControlPaint.Light(AccentColor, 0.9f), // 이미지 미설정 시 플레이스홀더 배경도 스킨 색상 반영
                Cursor = Cursors.Hand,
            };
            // [수정] 로컬 파일 선택(나만 보임)과 URL 설정(모두에게 공유) 중
            // 고르게 컨텍스트 메뉴로 바꿨다 — 클릭 한 번으로 로컬 선택만
            // 하던 예전 동작에서 확장.
            var profileImageMenu = new ContextMenuStrip();
            profileImageMenu.Items.Add("URL로 설정 (모두에게 공유)", null, (s, e) => SetMyProfileImageUrl());
            profileImageMenu.Items.Add("이미지 업로드 (서버에 저장, 공유)", null, (s, e) => UploadMyProfileImage());
            profileImageMenu.Items.Add("로컬 파일로 설정 (나만 보임)", null, (s, e) => SetMyProfileImage());
            profileImageMenu.Items.Add("프로필 이미지 해제 (공유 해제)", null, (s, e) => ClearMyProfileImageUrl());
            profileImageMenu.Items.Add("갤러리 관리", null, (s, e) => SwitchToGalleryTab());
            _picProfileImage.Click += (s, e) => profileImageMenu.Show(_picProfileImage, new Point(0, _picProfileImage.Height));

            _btnConnect = new Button { Text = "접속", Left = 499, Top = 19, Width = 58, Height = 25 };
            _btnConnect.Click += BtnConnect_Click;
            StyleButton(_btnConnect, filled: true);


            var pnlStatusDot = new Panel { Left = 405, Top = 27, Width = 8, Height = 8 };
            pnlStatusDot.Paint += (s, e) =>
            {
                e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                using (var b = new SolidBrush(_lblStatus.ForeColor))
                    e.Graphics.FillEllipse(b, 0, 0, pnlStatusDot.Width - 1, pnlStatusDot.Height - 1);
            };
            _pnlStatusDot = pnlStatusDot;

            _lblStatus = new Label { Text = "연결 안 됨", Left = 417, Top = 23, Width = 74, ForeColor = TextMutedColor };

            groupBox1.Controls.AddRange(new Control[]
            {
                lblCard1Title,
                lblIp, _txtServerIp, lblPort, _txtServerPort, lblProfile, _txtProfileName, _btnOpenNicknameDialog,
                _picProfileImage, _btnConnect, _pnlStatusDot, _lblStatus,
            });

            // ── 그룹박스 2: 채팅방(로비/룸) ──────────────────────────────
            var groupBox2 = new CardPanel { Left = 3, Top = 100, Width = 562, Height = 374 };
            var lblCard2Title = new Label
            {
                Text = "채팅방",
                Left = 14,
                Top = 4,
                Width = 100,
                Height = 14,
                ForeColor = TextSecondaryColor,
                Font = new Font(Font.FontFamily, 8f, FontStyle.Bold),
            };

            _cbChatRoomId = new ComboBox { Left = 11, Top = 23, Width = 71, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = false };
            for (int roomId = 1; roomId <= ProtocolConstants.MaxRoomId; roomId++)
                _cbChatRoomId.Items.Add(roomId);
            if (_cbChatRoomId.Items.Count > 0)
                _cbChatRoomId.SelectedIndex = 0;

            _btnRoomEnter = new Button { Text = "방 입장", Left = 88, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomEnter.Click += BtnRoomEnter_Click;
            StyleDynamicButton(_btnRoomEnter);

            _btnRoomLeave = new Button { Text = "방 나가기", Left = 163, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomLeave.Click += BtnRoomLeave_Click;
            StyleDynamicButton(_btnRoomLeave);

            // [수정] 서버 동접자수를 로비 유저수 왼쪽에 배치 — 프로필 이름과
            // 같은 형태(정적 라벨 + 읽기전용 텍스트박스)로 통일했다. 그룹박스1이
            // 아니라 여기(그룹박스2)로 옮긴 이유는 "로비 유저수 왼쪽"이라는
            // 배치 요구를 만족하려면 로비/방 유저수와 같은 줄에 있어야 하기
            // 때문이다.
            var lblServerUserCount = new Label { Text = "서버 동접자수 : ", Left = 11, Top = 58, Width = 94, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
            _txtServerUserCount = new Label { Left = 105, Top = 58, Width = 40, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, Font = new Font(Font, FontStyle.Bold) };

            var lblLobbyUserCount = new Label { Text = "로비 유저수 : ", Left = 155, Top = 58, Width = 81, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
            _txtLobbyUserCount = new Label { Left = 236, Top = 58, Width = 40, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, Font = new Font(Font, FontStyle.Bold) };

            var lblRoomUserCount = new Label { Text = "방 유저수 : ", Left = 286, Top = 58, Width = 68, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
            _txtRoomUserCount = new Label { Left = 354, Top = 58, Width = 40, Height = 15, AutoSize = false, TextAlign = ContentAlignment.MiddleLeft, Font = new Font(Font, FontStyle.Bold) };

            _lblCurrentRoom = new Label { Text = "위치: (로그인 전)", Left = 404, Top = 58, Width = 153, ForeColor = TextMutedColor };

            _txtMessage = new TextBox { Left = 10, Top = 338, Width = 474, Height = 24, Enabled = false, BorderStyle = BorderStyle.FixedSingle, Multiline = true };
            _txtMessage.KeyDown += TxtMessage_KeyDown;

            _btnSend = new Button { Text = "전송", Left = 490, Top = 338, Width = 67, Height = 26, Enabled = false, Visible = true };
            _btnSend.Click += BtnSend_Click;
            StyleDynamicButton(_btnSend);

            _listBoxChat = new ListBox
            {
                Left = 10,
                Top = 88,
                Width = 542,
                Height = 244,
                DrawMode = DrawMode.OwnerDrawVariable,
                HorizontalScrollbar = false, // 말풍선 너비를 자동으로 줄바꿈하려면 가로 스크롤은 꺼둬야 함
                ScrollAlwaysVisible = true,
                BackColor = ChatTheme.FixedChatBackground,
            };
            _listBoxChat.MeasureItem += ChatListBox_MeasureItem;
            _listBoxChat.DrawItem += ChatListBox_DrawItem;
            _listBoxChat.MouseClick += ChatListBox_MouseClick;
            _listBoxChat.MouseMove += ChatListBox_MouseMove;

            // 말풍선의 닉네임/시간 표시용 폰트 — DrawItem에서 재사용(캐시).
            _nameFont = new Font(_listBoxChat.Font.FontFamily, 8.5f, FontStyle.Bold);
            _timeFont = new Font(_listBoxChat.Font.FontFamily, 7.5f, FontStyle.Regular);
            // [수정] 이미지 레퍼런스처럼 카카오톡 스타일 노란 말풍선으로 —
            // 파란 배경엔 흰 글씨가 맞지만, 노란 배경엔 대비상 검정 글씨가 맞다.
            RebuildBubbleBrushes();

            groupBox2.Controls.AddRange(new Control[]
            {
                lblCard2Title,
                _cbChatRoomId, _btnRoomEnter, _btnRoomLeave,
                lblServerUserCount, _txtServerUserCount, lblLobbyUserCount, _txtLobbyUserCount,
                lblRoomUserCount, _txtRoomUserCount, _lblCurrentRoom,
                _txtMessage, _btnSend, _listBoxChat,
            });

            // ── 탭 구성: "대화"(기존 화면 전체) / "갤러리"(카카오톡 스타일로
            // 별도 창이 아니라 메인 창 안에 탭으로 통합) ──────────────────
            var tabControl = new TabControl { Dock = DockStyle.Fill, DrawMode = TabDrawMode.OwnerDrawFixed, SizeMode = TabSizeMode.Fixed, ItemSize = new Size(90, 32) };
            tabControl.DrawItem += (s, e) =>
            {
                var g = e.Graphics;
                var tabRect = e.Bounds;
                bool selected = e.Index == tabControl.SelectedIndex;

                using (var b = new SolidBrush(Color.White))
                    g.FillRectangle(b, tabRect);

                string text = tabControl.TabPages[e.Index].Text;
                using (var font = new Font(Font.FontFamily, 9f, selected ? FontStyle.Bold : FontStyle.Regular))
                using (var brush = new SolidBrush(selected ? AccentColor : TextSecondaryColor))
                using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                    g.DrawString(text, font, brush, tabRect, fmt);

                if (selected)
                {
                    using (var pen = new Pen(AccentColor, 2))
                        g.DrawLine(pen, tabRect.Left + 8, tabRect.Bottom - 1, tabRect.Right - 8, tabRect.Bottom - 1);
                }
            };

            var tabPageChat = new TabPage("대화");
            // [수정] TabPage.BackColor는 비주얼 스타일이 켜져 있으면 무시되고
            // 시스템 기본 흰색이 그려지는 WinForms의 잘 알려진 한계가 있다 —
            // 그래서 TabPage에 컨트롤을 직접 넣지 않고, 원하는 배경색을 가진
            // Panel(Dock=Fill)로 한 번 감싸서 그 안에 넣는다. Panel은 이
            // 문제가 없다.
            var chatPagePanel = new Panel { Dock = DockStyle.Fill, BackColor = PageBackColor };
            _chatPagePanel = chatPagePanel;
            // 시스템 로그도 다른 두 카드와 같은 톤으로 감싼다 — 리스트박스 자체는
            // 카드 안에서 (0,0) 기준 상대좌표로 다시 배치.
            var card3 = new CardPanel { Left = 3, Top = 480, Width = 562, Height = 139 };
            var lblCard3Title = new Label
            {
                Text = "시스템 로그",
                Left = 14,
                Top = 4,
                Width = 100,
                Height = 14,
                ForeColor = TextSecondaryColor,
                Font = new Font(Font.FontFamily, 8f, FontStyle.Bold),
            };

            // ── 하단: 시스템 로그(로그인/닉네임 변경/방 입퇴장 결과/연결 끊김/오류 등) ──
            _listBoxMsg = new ListBox
            {
                Left = 10,
                Top = 20,
                Width = 542,
                Height = 116,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 16,
                ScrollAlwaysVisible = true,
                BackColor = ChatTheme.FixedChatBackground,
            };
            _listBoxMsg.DrawItem += ColoredListBox_DrawItem;
            card3.Controls.AddRange(new Control[] { lblCard3Title, _listBoxMsg });

            chatPagePanel.Controls.AddRange(new Control[] { groupBox1, groupBox2, card3 });
            tabPageChat.Controls.Add(chatPagePanel);

            var tabPageGallery = new TabPage("갤러리");
            var galleryPagePanel = new Panel { Dock = DockStyle.Fill, BackColor = PageBackColor };
            _galleryPagePanel = galleryPagePanel;
            _galleryPanel = new ProfileImageGalleryPanel(_httpClient);
            _galleryPanel.ActiveImageChanged += OnGalleryActiveImageChanged;
            // [추가] 갤러리 상단 카메라 배지 메뉴는 실제 업로드/네트워크 로직을
            // 갖고 있지 않다(그건 여전히 이 폼이 소유) — 이벤트로 위임만 받아서
            // 채팅 탭의 프로필 이미지 메뉴와 똑같은 메서드를 그대로 재사용한다.
            _galleryPanel.UploadRequested += () => UploadMyProfileImage();
            _galleryPanel.SetUrlRequested += () => SetMyProfileImageUrl();
            _galleryPanel.LocalFileRequested += () => SetMyProfileImage();
            _galleryPanel.ClearRequested += () => ClearMyProfileImageUrl();
            galleryPagePanel.Controls.Add(_galleryPanel);
            tabPageGallery.Controls.Add(galleryPagePanel);

            tabControl.TabPages.Add(tabPageChat);
            tabControl.TabPages.Add(tabPageGallery);

            // 갤러리 탭으로 전환할 때마다 최신 목록을 다시 받아온다 — 방금
            // 다른 탭(대화)에서 업로드/설정한 이미지가 곧바로 반영되게.
            tabControl.SelectedIndexChanged += (s, e) =>
            {
                if (tabControl.SelectedTab == tabPageGallery)
                    _galleryPanel.RefreshList();
            };

            _tabControl = tabControl;

            Controls.Add(_tabControl);

            // ── 상단 헤더 바 — 로고 + 앱 이름. Dock 순서 주의: _tabControl(Fill)을
            // 먼저 추가해야 헤더(Top)가 나중에 그 위쪽 띠를 차지할 수 있다. ──
            var headerPanel = new Panel { Dock = DockStyle.Top, Height = 44, BackColor = ChatTheme.Blue.HeaderBack };
            headerPanel.Paint += (s, e) =>
            {
                using (var pen = new Pen(BorderColor))
                    e.Graphics.DrawLine(pen, 0, headerPanel.Height - 1, headerPanel.Width, headerPanel.Height - 1);
            };
            _headerPanel = headerPanel;

            var logoBox = new Panel { Left = 14, Top = 11, Width = 22, Height = 22 };
            logoBox.Paint += (s, e) =>
            {
                e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                using (var path = RoundedRectPath(new Rectangle(0, 0, logoBox.Width - 1, logoBox.Height - 1), 6))
                using (var b = new SolidBrush(AccentColor))
                    e.Graphics.FillPath(b, path);
            };

            var lblAppName = new Label
            {
                Text = "ChatApp",
                Left = 44,
                Top = 12,
                Width = 120,
                Height = 20,
                Font = new Font(Font.FontFamily, 10f, FontStyle.Regular),
                ForeColor = Color.FromArgb(26, 29, 33), // 헤더 배경이 항상 밝은 색으로 고정이라 어두운 글씨로 고정
            };
            _lblAppName = lblAppName;

            // [추가] 스킨 선택 버튼 — 헤더 우측.
            var btnSkin = new Button { Text = "스킨", Left = 504, Top = 8, Width = 56, Height = 28 };
            _btnSkin = btnSkin;
            StyleDynamicButton(btnSkin);
            var skinMenu = new ContextMenuStrip();
            foreach (var theme in ChatTheme.All)
            {
                var themeForMenu = theme;
                var item = new ToolStripMenuItem(theme.Name);
                item.Click += (s, e) => ApplyTheme(themeForMenu);
                skinMenu.Items.Add(item);
            }
            // 메뉴를 열 때마다 지금 적용된 스킨에만 체크 표시를 갱신한다 —
            // ApplyTheme() 시점이 아니라 여기서 하는 이유는, 메뉴가 안 열려
            //있는 동안에는 체크 상태가 화면에 안 보이니 굳이 미리 갱신해둘
            // 필요가 없어서다(연 순간에만 맞으면 충분).
            skinMenu.Opening += (s, e) =>
            {
                foreach (ToolStripMenuItem menuItem in skinMenu.Items)
                    menuItem.Checked = menuItem.Text == ChatTheme.Current.Name;
            };
            btnSkin.Click += (s, e) => skinMenu.Show(btnSkin, new Point(0, btnSkin.Height));

            headerPanel.Controls.AddRange(new Control[] { logoBox, lblAppName, btnSkin });
            Controls.Add(headerPanel);

            // [추가] 서버 동접자수 폴링 — 로그인 성공 시 시작, 연결 끊기면 정지.
            // 39초마다 서버에 물어보는 방식이라(예전의 로그인/로그아웃마다
            // 자발적으로 브로드캐스트하던 방식 대신) 접속자가 많아져도
            // 트래픽이 "클라이언트 수 / 폴링 주기"로 예측 가능하게 유지된다.
            _serverUserCountPollTimer = new System.Windows.Forms.Timer { Interval = 39000 };
            _serverUserCountPollTimer.Tick += (s, e) => _client?.RequestServerUserCount();

            FormClosing += (s, e) =>
            {
                _serverUserCountPollTimer.Stop();
                _client?.Close();
                _nameFont?.Dispose();
                _timeFont?.Dispose();

                // 링크 미리보기 썸네일들과 배경 이미지도 GDI 리소스라 정리한다.
                foreach (var preview in _linkPreviewCache.Values)
                    preview.Thumbnail?.Dispose();
                _listBoxChat.BackgroundImage?.Dispose();
                _picProfileImage.Image?.Dispose();
            };
        }

        //***************************************************************************
        // @brief listBoxMsg(시스템 로그) 전용 OwnerDraw 핸들러. Items에는
        //        ColoredEntry만 들어간다는 전제.
        //***************************************************************************
        private void ColoredListBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0)
                return;

            var listBox = (ListBox)sender;
            var entry = (ColoredEntry)listBox.Items[e.Index];

            e.DrawBackground();

            string timePrefix = entry.Timestamp.ToString("HH:mm:ss") + "  ";
            SizeF timeSize = e.Graphics.MeasureString(timePrefix, listBox.Font);

            using (var timeBrush = new SolidBrush(TextMutedColor))
                e.Graphics.DrawString(timePrefix, listBox.Font, timeBrush, e.Bounds);

            var textRect = new RectangleF(e.Bounds.X + timeSize.Width, e.Bounds.Y, e.Bounds.Width - timeSize.Width, e.Bounds.Height);
            using (var brush = new SolidBrush(entry.Color))
            {
                e.Graphics.DrawString(entry.Text, listBox.Font, brush, textRect);
            }
            e.DrawFocusRectangle();
        }

        //***************************************************************************
        // @brief listBoxChat(채팅 로그) 전용 — 말풍선 높이를 텍스트 길이에 맞춰
        //        동적으로 계산한다. OwnerDrawVariable에서는 이 이벤트가 없으면
        //        모든 줄이 같은 높이로 찌그러진다.
        //***************************************************************************
        private void ChatListBox_MeasureItem(object sender, MeasureItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _listBoxChat.Items.Count)
                return;

            if (!(_listBoxChat.Items[e.Index] is ChatBubbleItem item))
            {
                e.ItemHeight = 24;
                return;
            }

            int maxBubbleWidth = (int)(_listBoxChat.ClientSize.Width * 0.65);

            using (Graphics g = _listBoxChat.CreateGraphics())
            {
                SizeF textSize = g.MeasureString(item.Message, _listBoxChat.Font, maxBubbleWidth);

                // [수정 — 첫 줄(시간) 텍스트 잘림] 내 메시지와 상대 메시지는
                // DrawItem에서 쓰는 세로 오프셋이 다르다:
                //   - 내 메시지: 말풍선이 bounds.Top+5부터 시작, 시간은 말풍선
                //     하단에 딱 맞춰 그려짐 → 필요 높이 ≈ 5(상단 여백) + 10(말풍선
                //     상하 패딩) + 여유
                //   - 상대 메시지: 닉네임 영역(15) + 여백(5) + 말풍선(bounds.Top+20부터
                //     시작) + 말풍선 하단에 걸쳐 그려지는 시간 텍스트까지 고려하면
                //     내 메시지보다 훨씬 더 큰 여유가 필요하다.
                // 예전엔 이 둘을 구분하지 않고 kBaseHeight=25 하나만 썼는데,
                // 상대 메시지 쪽 실제 필요 높이(약 35)보다 작아서 시간 텍스트
                // 아래쪽이 다음 줄에 가려 잘려 보였다.
                int baseHeight = item.IsMyMessage ? 25 : 35;

                // 이 메시지에 URL이 있고, 그 URL의 미리보기 카드가 이미
                // 로딩 완료됐으면 카드 높이만큼 더 확보한다. 아직 로딩
                // 중이거나 실패했으면 추가 공간을 안 잡는다 — 로딩 완료
                // 시점에 RefreshChatItem()이 다시 측정을 트리거해준다.
                int previewHeight = 0;
                Match urlMatch = UrlRegex.Match(item.Message);
                if (urlMatch.Success
                    && _linkPreviewCache.TryGetValue(urlMatch.Value, out var preview)
                    && !preview.IsLoading && !preview.Failed)
                {
                    previewHeight = kLinkPreviewCardHeight + 8;
                }

                e.ItemHeight = (int)textSize.Height + baseHeight + previewHeight;
            }
        }

        //***************************************************************************
        // @brief 실제 프로필 사진이 없는 발신자를 위한 대체 아바타를 그린다
        //        — 닉네임 기반으로 결정적으로 고른 색의 원 안에 첫 글자를
        //        넣는다(디스코드/슬랙 방식). 같은 닉네임은 항상 같은 색이
        //        나오므로, 실제 사진은 아니어도 "누가 누군지" 시각적으로
        //        구분하는 데는 도움이 된다.
        //***************************************************************************
        private static readonly Color[] AvatarPalette =
        {
            Color.FromArgb(230, 126, 34), Color.FromArgb(41, 128, 185), Color.FromArgb(39, 174, 96),
            Color.FromArgb(155, 89, 182), Color.FromArgb(231, 76, 60), Color.FromArgb(26, 188, 156),
            Color.FromArgb(243, 156, 18), Color.FromArgb(52, 73, 94),
        };

        //***************************************************************************
        // @brief 발신자 아바타를 그린다. profileImageUrl로 받아온 실제 이미지가
        //        캐시에 있으면 그것을 원형으로 잘라 그리고, 없으면(아직 못
        //        받아왔거나 URL 자체가 없거나 실패했으면) 닉네임 기반으로
        //        결정적으로 고른 색의 원 안에 첫 글자를 넣는 생성 아바타로
        //        대체한다(디스코드/슬랙 방식). 같은 닉네임은 항상 같은 색이
        //        나오므로, 실제 사진이 없어도 "누가 누군지" 시각적으로
        //        구분하는 데는 도움이 된다.
        //***************************************************************************
        private void DrawAvatar(Graphics g, string senderName, string profileImageUrl, Rectangle rect, int itemIndex)
        {
            Image avatarImage = null;
            if (!string.IsNullOrEmpty(profileImageUrl))
            {
                lock (_avatarImageCache)
                {
                    _avatarImageCache.TryGetValue(profileImageUrl, out avatarImage);
                }

                if (avatarImage == null)
                    RequestAvatarImage(profileImageUrl, itemIndex);
            }

            if (avatarImage != null)
            {
                using (var clipPath = new GraphicsPath())
                {
                    clipPath.AddEllipse(rect);
                    Region previousClip = g.Clip;
                    g.SetClip(clipPath, CombineMode.Intersect);
                    g.DrawImage(avatarImage, rect);
                    g.Clip = previousClip;
                }
                return;
            }

            string name = string.IsNullOrEmpty(senderName) ? "?" : senderName;
            Color color = AvatarPalette[(uint)name.GetHashCode() % (uint)AvatarPalette.Length];

            using (var brush = new SolidBrush(color))
            {
                g.FillEllipse(brush, rect);
            }

            string initial = name.Substring(0, 1).ToUpperInvariant();
            using (var format = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
            using (var avatarFont = new Font(_listBoxChat.Font.FontFamily, 10f, FontStyle.Bold))
            {
                g.DrawString(initial, avatarFont, Brushes.White, rect, format);
            }
        }

        //***************************************************************************
        // @brief 둥근 사각형 GraphicsPath를 만든다 — 말풍선 모서리를 둥글게
        //        그리기 위한 헬퍼. radius가 bounds의 절반보다 크면 안전하게 줄인다.
        //***************************************************************************
        private static GraphicsPath GetRoundedRectPath(Rectangle bounds, int radius)
        {
            int diameter = Math.Min(radius * 2, Math.Min(bounds.Width, bounds.Height));
            var arc = new Rectangle(bounds.Location, new Size(diameter, diameter));
            var path = new GraphicsPath();

            path.AddArc(arc, 180, 90);                                   // 좌상단
            arc.X = bounds.Right - diameter;
            path.AddArc(arc, 270, 90);                                   // 우상단
            arc.Y = bounds.Bottom - diameter;
            path.AddArc(arc, 0, 90);                                     // 우하단
            arc.X = bounds.Left;
            path.AddArc(arc, 90, 90);                                    // 좌하단
            path.CloseFigure();

            return path;
        }

        //***************************************************************************
        // @brief listBoxChat(채팅 로그) 전용 — 카카오톡 스타일 말풍선을 직접 그린다.
        //        내가 보낸 메시지는 우측(노란 계열), 상대는 좌측(흰색)에 배치하고,
        //        GetRoundedRectPath()로 모서리를 둥글게 그린다.
        //***************************************************************************
        private void ChatListBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _listBoxChat.Items.Count)
                return;

            // [수정] 배경을 사용자가 직접 색상/이미지로 고르는 기능은 제거하고
            // 스킨(ChatTheme.ChatBackground)에 포함시켰다 — e.DrawBackground()가
            // _listBoxChat.BackColor(스킨이 정해준 값)를 그대로 반영한다.
            e.DrawBackground();

            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            Rectangle bounds = e.Bounds;

            if (_listBoxChat.Items[e.Index] is ChatBubbleItem item)
            {
                const int kBubbleCornerRadius = 10;

                Font msgFont = _listBoxChat.Font;

                Brush myBubbleColor = _myBubbleBrush;
                Brush otherBubbleColor = Brushes.White;
                Brush textColor = item.IsMyMessage ? _myTextBrush : _otherTextBrush;
                Brush nameColor = _bubbleNameBrush;
                Brush timeColor = _bubbleTimeBrush;

                int maxBubbleWidth = (int)(bounds.Width * 0.65);
                SizeF textSize = g.MeasureString(item.Message, msgFont, maxBubbleWidth);

                int bubbleWidth = (int)textSize.Width + 16;
                int bubbleHeight = (int)textSize.Height + 10;

                string timeStr = item.Timestamp.ToString("tt h:mm");

                if (item.IsMyMessage)
                {
                    // ── 내가 보낸 메시지 — 우측 배치 ──────────────────────
                    // [추가] 상대 메시지와 대칭으로, 말풍선 오른쪽에 내
                    // 아바타를 그린다. 말풍선 X 위치도 그만큼 왼쪽으로 밀어서
                    // 아바타와 안 겹치게 한다.
                    const int kAvatarSize = 28;
                    const int kAvatarGap = 6;

                    int bubbleX = bounds.Right - bubbleWidth - kAvatarSize - kAvatarGap - 10;
                    int bubbleY = bounds.Top + 5;
                    Rectangle bubbleRect = new Rectangle(bubbleX, bubbleY, bubbleWidth, bubbleHeight);

                    DrawAvatar(g, item.SenderName, item.SenderProfileImageUrl,
                        new Rectangle(bounds.Right - kAvatarSize - 10, bounds.Top + 2, kAvatarSize, kAvatarSize), e.Index);

                    using (var path = GetRoundedRectPath(bubbleRect, kBubbleCornerRadius))
                    {
                        g.FillPath(myBubbleColor, path);
                    }

                    DrawMessageWithLinks(g, item.Message, msgFont, textColor, new RectangleF(bubbleX + 8, bubbleY + 5, maxBubbleWidth, textSize.Height), e.Index);

                    SizeF timeSize = g.MeasureString(timeStr, _timeFont);
                    g.DrawString(timeStr, _timeFont, timeColor, bubbleX - timeSize.Width - 5, bubbleY + bubbleHeight - timeSize.Height);

                    // 내 메시지는 우측 정렬이라, 미리보기 카드도 말풍선과 같은
                    // 오른쪽 기준선에 맞춘다(아바타 폭만큼 같이 밀어줌).
                    DrawLinkPreviewCardIfAny(g, item.Message, bounds.Right - Math.Min(maxBubbleWidth, 220) - kAvatarSize - kAvatarGap - 10,
                        bubbleY + bubbleHeight + 4, Math.Min(maxBubbleWidth, 220), e.Index);
                }
                else
                {
                    // ── 상대가 보낸 메시지 — 좌측 배치 ────────────────────
                    // [추가] 왼쪽에 발신자 아바타(색깔 원형 + 이니셜)를 그리고,
                    // 닉네임/말풍선은 그만큼 오른쪽으로 밀어서 배치한다. 실제
                    // 프로필 사진은 네트워크로 전송되지 않으므로, 닉네임을
                    // 기반으로 결정적으로 생성한 아바타를 대신 쓴다.
                    const int kAvatarSize = 28;
                    const int kAvatarGap = 6;
                    const int kNameHeight = 15;

                    DrawAvatar(g, item.SenderName, item.SenderProfileImageUrl, new Rectangle(bounds.Left + 10, bounds.Top + 2, kAvatarSize, kAvatarSize), e.Index);

                    int contentLeft = bounds.Left + 10 + kAvatarSize + kAvatarGap;
                    int bubbleX = contentLeft;
                    int bubbleY = bounds.Top + kNameHeight + 5;

                    g.DrawString(item.SenderName, _nameFont, nameColor, contentLeft, bounds.Top + 2);

                    Rectangle bubbleRect = new Rectangle(bubbleX, bubbleY, bubbleWidth, bubbleHeight);

                    using (var path = GetRoundedRectPath(bubbleRect, kBubbleCornerRadius))
                    {
                        g.FillPath(otherBubbleColor, path);
                        g.DrawPath(Pens.LightGray, path);
                    }

                    DrawMessageWithLinks(g, item.Message, msgFont, textColor, new RectangleF(bubbleX + 8, bubbleY + 5, maxBubbleWidth, textSize.Height), e.Index);

                    SizeF timeSize = g.MeasureString(timeStr, _timeFont);
                    g.DrawString(timeStr, _timeFont, timeColor, bubbleX + bubbleWidth + 5, bubbleY + bubbleHeight - timeSize.Height);

                    // 상대 메시지는 좌측 정렬이라, 미리보기 카드도 말풍선과 같은
                    // 왼쪽 기준선에 맞춘다.
                    DrawLinkPreviewCardIfAny(g, item.Message, bubbleX, bubbleY + bubbleHeight + 4,
                        Math.Min(maxBubbleWidth, 220), e.Index);
                }
            }
            else
            {
                // 시스템 안내성 문자열이 섞여 들어온 경우(현재는 안 쓰지만 방어적으로)
                string text = _listBoxChat.Items[e.Index].ToString();
                TextRenderer.DrawText(g, text, _listBoxChat.Font, bounds, Color.Gray, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }

            e.DrawFocusRectangle();
        }

        //***************************************************************************
        // @brief 메시지 텍스트를 그대로 그린 뒤, 그 안에 포함된 URL만 찾아서
        //        파란 밑줄로 덧그린다. 각 URL의 실제 픽셀 좌표(bounds)를
        //        _chatItemUrlRegions에 기록해둬서, 나중에 클릭/호버 판정에 쓴다.
        // @details MeasureCharacterRanges()로 원본 텍스트와 완전히 같은
        //          레이아웃(줄바꿈 포함)에서 URL 부분의 실제 그려진 위치를
        //          구한다 — 그래야 여러 줄로 줄바꿈된 메시지에서도 URL 위치가
        //          어긋나지 않는다.
        // @details [알려진 한계] URL 자체가 줄 중간에서 두 줄로 쪼개지는
        //          경우(긴 URL이 말풍선 너비를 넘어갈 때)는 완벽히 처리하지
        //          않는다 — Region.GetBounds()가 그 경우 두 줄을 합친 사각형을
        //          돌려주는데, 그 사각형 위에 URL 전체를 한 줄로 덧그려서
        //          시각적으로 어긋날 수 있다. 데모 범위에서는 드문 경우라
        //          감수했다.
        //***************************************************************************
        //***************************************************************************
        // @brief message에 URL이 있고 그 미리보기가 로딩 완료 상태면 카드를
        //        그린다. 로딩 중/실패/URL 없음이면 아무것도 안 그린다
        //        (MeasureItem이 그 경우엔 애초에 카드 공간을 안 잡아뒀음).
        //        카드 영역도 _chatItemUrlRegions에 추가해서, 카드를 클릭해도
        //        링크가 열리게 한다(카카오톡처럼).
        //***************************************************************************
        private void DrawLinkPreviewCardIfAny(Graphics g, string message, int x, int y, int width, int itemIndex)
        {
            Match urlMatch = UrlRegex.Match(message);
            if (!urlMatch.Success)
                return;

            string url = urlMatch.Value;
            if (!_linkPreviewCache.TryGetValue(url, out var preview) || preview.IsLoading || preview.Failed)
                return;

            Rectangle cardRect = DrawLinkPreviewCard(g, preview, x, y, width);

            if (!_chatItemUrlRegions.TryGetValue(itemIndex, out var regions))
            {
                regions = new List<(string Url, RectangleF Bounds)>();
                _chatItemUrlRegions[itemIndex] = regions;
            }
            regions.Add((url, cardRect));
        }

        //***************************************************************************
        // @brief 썸네일 + 제목 + 설명으로 구성된 미리보기 카드를 그린다.
        // @return 그려진 카드의 사각형(클릭 히트테스트용).
        //***************************************************************************
        private Rectangle DrawLinkPreviewCard(Graphics g, LinkPreviewData preview, int x, int y, int width)
        {
            var cardRect = new Rectangle(x, y, width, kLinkPreviewCardHeight);

            using (var path = GetRoundedRectPath(cardRect, 6))
            {
                g.FillPath(Brushes.WhiteSmoke, path);
                g.DrawPath(Pens.LightGray, path);
            }

            const int kPadding = 5;
            int textX = x + kPadding;

            if (preview.Thumbnail != null)
            {
                var thumbRect = new Rectangle(x + kPadding, y + kPadding, kLinkPreviewThumbnailSize, kLinkPreviewThumbnailSize);
                g.DrawImage(preview.Thumbnail, thumbRect);
                textX = thumbRect.Right + kPadding;
            }

            int textWidth = Math.Max(0, cardRect.Right - kPadding - textX);

            using (var titleFont = new Font(_listBoxChat.Font, FontStyle.Bold))
            using (var clipFormat = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.LineLimit })
            {
                var titleRect = new RectangleF(textX, y + kPadding, textWidth, 18);
                g.DrawString(preview.Title ?? string.Empty, titleFont, Brushes.Black, titleRect, clipFormat);

                if (!string.IsNullOrEmpty(preview.Description))
                {
                    var descRect = new RectangleF(textX, y + kPadding + 20, textWidth, kLinkPreviewCardHeight - kPadding - 20 - kPadding);
                    g.DrawString(preview.Description, _timeFont, Brushes.Gray, descRect, clipFormat);
                }
            }

            return cardRect;
        }

        //***************************************************************************
        // @brief 메시지 텍스트를 그대로 그린 뒤, 그 안에 포함된 URL만 찾아서
        //        파란 밑줄로 덧그린다. 각 URL의 실제 픽셀 좌표(bounds)를
        //        _chatItemUrlRegions에 기록해둬서, 나중에 클릭/호버 판정에 쓴다.
        //***************************************************************************
        private void DrawMessageWithLinks(Graphics g, string message, Font font, Brush textColor, RectangleF layoutRect, int itemIndex)
        {
            g.DrawString(message, font, textColor, layoutRect);

            MatchCollection matches = UrlRegex.Matches(message);
            if (matches.Count == 0)
            {
                _chatItemUrlRegions.Remove(itemIndex);
                return;
            }

            var regions = new List<(string Url, RectangleF Bounds)>();

            using (var format = new StringFormat())
            {
                // MeasureCharacterRanges()는 한 번에 최대 32개 범위까지만 지원한다
                // — 메시지 한 줄에 URL이 그렇게 많이 섞일 일은 데모 범위에서
                // 사실상 없어서 그냥 앞에서부터 32개만 처리한다.
                var ranges = new List<CharacterRange>();
                foreach (Match m in matches)
                {
                    if (ranges.Count >= 32)
                        break;
                    ranges.Add(new CharacterRange(m.Index, m.Length));
                }
                format.SetMeasurableCharacterRanges(ranges.ToArray());

                Region[] measuredRegions = g.MeasureCharacterRanges(message, font, layoutRect, format);

                using (var linkFont = new Font(font, FontStyle.Underline))
                {
                    for (int i = 0; i < measuredRegions.Length; i++)
                    {
                        RectangleF bounds = measuredRegions[i].GetBounds(g);
                        string url = matches[i].Value;

                        // 원래 텍스트 위에 정확히 겹쳐서 파란 밑줄로 덧그린다
                        // (같은 폰트/같은 위치라 글자 폭이 일치해서 자연스럽게 대체됨).
                        g.DrawString(url, linkFont, Brushes.Blue, bounds.Location);

                        regions.Add((url, bounds));
                    }
                }
            }

            _chatItemUrlRegions[itemIndex] = regions;
        }

        //***************************************************************************
        // @brief 채팅 로그에서 URL 위로 마우스를 올리면 손가락 커서로 바꾼다.
        //***************************************************************************
        private void ChatListBox_MouseMove(object sender, MouseEventArgs e)
        {
            int index = _listBoxChat.IndexFromPoint(e.Location);
            bool overLink = false;

            if (index >= 0 && _chatItemUrlRegions.TryGetValue(index, out var regions))
            {
                foreach (var region in regions)
                {
                    if (region.Bounds.Contains(e.Location))
                    {
                        overLink = true;
                        break;
                    }
                }
            }

            _listBoxChat.Cursor = overLink ? Cursors.Hand : Cursors.Default;
        }

        //***************************************************************************
        // @brief 채팅 로그에서 URL을 클릭하면 기본 브라우저로 연다.
        //***************************************************************************
        private void ChatListBox_MouseClick(object sender, MouseEventArgs e)
        {
            int index = _listBoxChat.IndexFromPoint(e.Location);
            if (index < 0 || !_chatItemUrlRegions.TryGetValue(index, out var regions))
                return;

            foreach (var region in regions)
            {
                if (region.Bounds.Contains(e.Location))
                {
                    OpenUrl(region.Url);
                    return;
                }
            }
        }

        //***************************************************************************
        // @brief 기본 브라우저로 URL을 연다. 실패해도(브라우저 실행 오류 등)
        //        데모 범위에서는 조용히 무시한다.
        //***************************************************************************
        private static void OpenUrl(string url)
        {
            try
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch
            {
            }
        }

        //***************************************************************************
        // @brief 채팅창 배경을 단색으로 설정한다. 기존에 이미지가 설정돼
        //        있었다면 먼저 정리한다(GDI 리소스 누수 방지).
        //***************************************************************************
        //***************************************************************************
        // @brief 프로필 이미지 파일을 골라 _picProfileImage에 표시하고, 프로필
        //        이름별로 경로를 로컬에 저장해둔다(다음 실행 때 다시 불러옴).
        // @details [알려진 한계] 이 이미지는 순수 로컬 개인화용이다 — 서버
        //          프로토콜에 이미지 전송 기능이 없어서 다른 사용자에게는
        //          전혀 보이지 않는다. 실제로 공유하려면 서버에 이미지
        //          업로드/전달 경로를 새로 만들어야 한다.
        //***************************************************************************
        private void SetMyProfileImage()
        {
            using (var dlg = new OpenFileDialog { Filter = "이미지 파일|*.png;*.jpg;*.jpeg;*.bmp;*.gif" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                try
                {
                    using (var original = Image.FromFile(dlg.FileName))
                    {
                        _picProfileImage.Image?.Dispose();
                        _picProfileImage.Image = new Bitmap(original); // 파일 핸들 의존성 제거
                    }
                    _picProfileImage.BackColor = Color.White;

                    if (!string.IsNullOrEmpty(_profileName))
                        SaveMyProfileImagePath(_profileName, dlg.FileName);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "이미지를 불러오지 못했습니다: " + ex.Message, "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        private static string ProfileImagePathFile(string profileName) =>
            Path.Combine(AppContext.BaseDirectory, "chat_profile_image_" + profileName + ".dat");

        private static void SaveMyProfileImagePath(string profileName, string imagePath)
        {
            try
            {
                File.WriteAllText(ProfileImagePathFile(profileName), imagePath);
            }
            catch
            {
                // 저장 실패해도 이번 세션 표시에는 지장 없음 — 다음 실행 때만 다시 물어보면 됨.
            }
        }

        //***************************************************************************
        // @brief 이전에 저장해둔 프로필 이미지가 있으면 불러와 표시한다.
        //        파일이 없거나 로드에 실패해도 조용히 무시(기본 회색 칸 유지).
        //***************************************************************************
        private void LoadMyProfileImage(string profileName)
        {
            try
            {
                string path = ProfileImagePathFile(profileName);
                if (!File.Exists(path))
                    return;

                string imagePath = File.ReadAllText(path).Trim();
                if (string.IsNullOrEmpty(imagePath) || !File.Exists(imagePath))
                    return;

                using (var original = Image.FromFile(imagePath))
                {
                    _picProfileImage.Image?.Dispose();
                    _picProfileImage.Image = new Bitmap(original);
                }
                _picProfileImage.BackColor = Color.White;
            }
            catch
            {
                // 손상된 파일 등 — 조용히 무시하고 기본 회색 칸 유지.
            }
        }

        //***************************************************************************
        // @brief 프로필 이미지를 URL로 설정 요청한다 — 성공하면 서버가 이
        //        URL을 계정에 저장하고, 이후 내가 보내는 모든 채팅 메시지에
        //        실려 다른 사람에게도 보인다(로컬 파일 방식과 달리 실제로 공유됨).
        //        서버 연결 전이면 조용히 무시한다.
        //***************************************************************************
        //***************************************************************************
        // @brief 서버가 알려준(또는 방금 내가 설정한) 내 프로필 이미지 URL을
        //        실제로 내려받아 _picProfileImage에 반영한다. 실패해도 조용히
        //        무시(기본 회색 칸 또는 이전 이미지 유지).
        //***************************************************************************
        private async void LoadMyProfileImageFromUrl(string url)
        {
            try
            {
                byte[] imageBytes = await FetchImageBytesAsync(url);
                if (imageBytes == null || imageBytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(imageBytes))
                using (var original = Image.FromStream(ms))
                {
                    _picProfileImage.Image?.Dispose();
                    _picProfileImage.Image = new Bitmap(original);
                }
                _picProfileImage.BackColor = Color.White;
            }
            catch
            {
                // 네트워크 오류 등 — 조용히 무시.
            }
        }

        private void SetMyProfileImageUrl()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 설정할 수 있습니다.", "알림");
                return;
            }

            string url = PromptForText(this, "프로필 이미지 URL", "이미지 URL (https://...)", _myProfileImageUrl);
            if (url == null) // 취소
                return;

            _pendingProfileImageUrlRequest = url;
            _client.RequestSetProfileImageUrl(url);
        }

        //***************************************************************************
        // @brief 프로필 이미지 URL 공유를 해제한다(빈 문자열로 설정 요청).
        //***************************************************************************
        private void ClearMyProfileImageUrl()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 해제할 수 있습니다.", "알림");
                return;
            }

            _pendingProfileImageUrlRequest = string.Empty;
            _client.RequestSetProfileImageUrl(string.Empty);
        }

        //***************************************************************************
        // @brief 파일을 골라 서버에 청크로 업로드하고, 성공하면 대표 이미지로
        //        지정한다(서버가 업로드 완료 처리 안에서 곧바로 대표로 등록).
        // @details Begin -> Chunk*N(응답 없음, 그냥 순서대로 다 보냄) -> End
        //          -> End 응답 대기 순으로 진행한다. 각 단계의 서버 응답은
        //          이벤트로 오는데, TaskCompletionSource로 감싸서 이 메서드
        //          안에서 await로 순서대로 기다리는 형태로 만들었다.
        //***************************************************************************
        private async void UploadMyProfileImage()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 업로드할 수 있습니다.", "알림");
                return;
            }

            using (var dlg = new OpenFileDialog { Filter = "이미지 파일|*.png;*.jpg;*.jpeg;*.bmp;*.gif" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                byte[] fileBytes;
                try
                {
                    fileBytes = File.ReadAllBytes(dlg.FileName);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "파일을 읽지 못했습니다: " + ex.Message, "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                AppendSystemLog("[시스템] 업로드 토큰 발급 중...", ColorSystemInfo);

                try
                {
                    _uploadTokenTcs = new TaskCompletionSource<RequestUploadTokenResData>();
                    _client.RequestUploadToken();

                    var tokenResult = await WaitWithTimeout(_uploadTokenTcs.Task, TimeSpan.FromSeconds(10));
                    if (tokenResult == null || !tokenResult.Success || string.IsNullOrEmpty(tokenResult.FileServerUrl))
                    {
                        AppendSystemLog("[시스템] 업로드 토큰 발급 실패 — 파일 서버가 설정돼 있지 않을 수 있습니다.", ColorSystemError);
                        return;
                    }

                    AppendSystemLog("[시스템] 파일 서버에 업로드 중...", ColorSystemInfo);

                    string uploadedUrl;
                    try
                    {
                        uploadedUrl = await UploadToFileServerAsync(tokenResult.FileServerUrl, tokenResult.UploadToken, dlg.FileName, fileBytes);
                    }
                    catch (Exception ex)
                    {
                        AppendSystemLog("[시스템] 파일 서버 업로드 실패: " + ex.Message, ColorSystemError);
                        return;
                    }

                    if (string.IsNullOrEmpty(uploadedUrl))
                    {
                        AppendSystemLog("[시스템] 파일 서버가 URL을 돌려주지 않았습니다.", ColorSystemError);
                        return;
                    }

                    // 업로드된 URL을 채팅 서버에 새 대표 이미지로 등록한다 —
                    // SetMyProfileImageUrl()과 동일한 등록 경로(SetProfileImageUrlReq)를
                    // 그대로 재사용한다. 성공/실패 결과 처리는
                    // OnSetProfileImageUrlResultReceived()가 공통으로 담당한다.
                    _pendingProfileImageUrlRequest = uploadedUrl;
                    _client.RequestSetProfileImageUrl(uploadedUrl);
                }
                finally
                {
                    _uploadTokenTcs = null;
                }
            }
        }

        //***************************************************************************
        // @brief 파일 서버에 이미지를 업로드하고, 결과로 접근 가능한 URL을 돌려받는다.
        // @details [파일 서버 API 계약] multipart/form-data POST
        //          {fileServerUrl}/upload — 폼 필드 "file"에 이미지 바이트,
        //          "token"에 채팅 서버가 발급한 업로드 토큰. 성공하면 본문에
        //          평문(text/plain)으로 접근 가능한 이미지 URL을 돌려준다.
        //          [설계] 파일 서버는 아직 별도로 구현되지 않았다 — 이 계약은
        //          채팅 서버 쪽 RequestUploadTokenRes 설계(토큰+파일서버주소
        //          발급, ChatServerMain::RequestUploadToken() 참고)와 짝을
        //          이루도록 클라이언트가 미리 정해둔 것이다. 실제 파일 서버
        //          구현이 이 계약과 다르게 나온다면 이 메서드만 고치면 되고,
        //          나머지 흐름(토큰 발급, 대표 이미지 등록)은 영향받지 않는다.
        //***************************************************************************
        private async Task<string> UploadToFileServerAsync(string fileServerUrl, string uploadToken, string localFilePath, byte[] fileBytes)
        {
            using (var content = new MultipartFormDataContent())
            {
                var fileContent = new ByteArrayContent(fileBytes);
                fileContent.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/octet-stream");
                content.Add(fileContent, "file", Path.GetFileName(localFilePath));
                content.Add(new StringContent(uploadToken), "token");

                string uploadUrl = fileServerUrl.TrimEnd('/') + "/upload";
                HttpResponseMessage response = await _uploadHttpClient.PostAsync(uploadUrl, content);
                response.EnsureSuccessStatusCode();

                return (await response.Content.ReadAsStringAsync()).Trim();
            }
        }

        //***************************************************************************
        // @brief task가 timeout 안에 끝나면 그 결과를, 아니면 null을 반환한다.
        //        서버가 응답을 영영 안 주는 경우(연결 끊김 등)에 이 메서드
        //        호출부가 무한정 멈춰있지 않게 하기 위함.
        //***************************************************************************
        private static async Task<T> WaitWithTimeout<T>(Task<T> task, TimeSpan timeout) where T : class
        {
            var completed = await Task.WhenAny(task, Task.Delay(timeout));
            return completed == task ? task.Result : null;
        }

        //***************************************************************************
        // @brief "갤러리 관리" 메뉴 클릭 시 — 별도 창을 띄우는 대신 갤러리
        //        탭으로 전환한다(카카오톡 스타일 통합).
        //***************************************************************************
        private void SwitchToGalleryTab()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 갤러리를 볼 수 있습니다.", "알림");
                return;
            }

            _tabControl.SelectedIndex = 1; // 0=대화, 1=갤러리
        }

        //***************************************************************************
        // @brief 갤러리 탭에서 대표 이미지가 바뀔 때마다(선택/삭제 성공) 호출된다.
        //        예전(모달 다이얼로그)엔 창이 닫힌 뒤에야 한 번 반영했는데,
        //        이제는 탭을 안 벗어나도 즉시 반영된다.
        //***************************************************************************
        private void OnGalleryActiveImageChanged(string newImageRef)
        {
            ApplyProfileImageUrl(newImageRef);
        }

        //***************************************************************************
        // @brief 간단한 한 줄 텍스트 입력 대화상자. WinForms에 내장 InputBox가
        //        없어서 최소 구성으로 직접 만들었다.
        // @return 확인을 누르면 입력한 문자열(트림됨), 취소하면 null.
        //***************************************************************************
        private static string PromptForText(IWin32Window owner, string title, string label, string initialValue)
        {
            using (var dlg = new Form())
            {
                dlg.Text = title;
                dlg.FormBorderStyle = FormBorderStyle.FixedDialog;
                dlg.StartPosition = FormStartPosition.CenterParent;
                dlg.ClientSize = new Size(360, 110);
                dlg.MaximizeBox = false;
                dlg.MinimizeBox = false;
                dlg.ShowInTaskbar = false;

                var lbl = new Label { Text = label, Left = 10, Top = 15, Width = 340 };
                var txt = new TextBox { Left = 10, Top = 35, Width = 340, Text = initialValue ?? string.Empty };
                var btnOk = new Button { Text = "확인", Left = 190, Top = 70, Width = 75, DialogResult = DialogResult.OK };
                var btnCancel = new Button { Text = "취소", Left = 275, Top = 70, Width = 75, DialogResult = DialogResult.Cancel };

                dlg.Controls.AddRange(new Control[] { lbl, txt, btnOk, btnCancel });
                dlg.AcceptButton = btnOk;
                dlg.CancelButton = btnCancel;

                return dlg.ShowDialog(owner) == DialogResult.OK ? txt.Text.Trim() : null;
            }
        }

        //***************************************************************************
        // @brief url의 프로필 이미지를 요청한다(아바타용). 이미 캐시에 있거나
        //        요청이 진행 중이면 새로 요청하지 않는다 — RequestLinkPreview()와
        //        동일한 "URL 하나당 한 번만" 원칙.
        //***************************************************************************
        private void RequestAvatarImage(string url, int itemIndex)
        {
            if (string.IsNullOrEmpty(url))
                return;

            lock (_avatarImageCache)
            {
                if (_avatarImageCache.ContainsKey(url) || _avatarFetchInProgress.Contains(url))
                    return;
                _avatarFetchInProgress.Add(url);
            }

            _ = FetchAvatarImageAsync(url, itemIndex);
        }

        //***************************************************************************
        // @brief imageRef로 이미지 바이트를 가져온다. 링크 미리보기 썸네일,
        //        아바타, 내 프로필 이미지 표시 등 이미지가 필요한 모든 곳이
        //        이 헬퍼 하나로 통일해서 쓴다.
        // @details [설계 변경] 예전엔 "local:" 접두사가 있으면 채팅 서버와의
        //          TCP 청크 다운로드 프로토콜을 썼는데, 이미지 저장/서빙을
        //          별도 파일 서버로 분리하면서 그 프로토콜 자체가 없어졌다 —
        //          이제 모든 이미지 참조는 실제로 HTTP(S)로 접근 가능한 URL
        //          (외부 호스팅이든, 우리 파일 서버가 돌려준 주소든)이라
        //          HttpClient 하나로 통일된다.
        //***************************************************************************
        private async Task<byte[]> FetchImageBytesAsync(string imageRef)
        {
            return await _httpClient.GetByteArrayAsync(imageRef);
        }


        private async Task FetchAvatarImageAsync(string url, int itemIndex)
        {
            try
            {
                byte[] imageBytes = await FetchImageBytesAsync(url);
                if (imageBytes == null || imageBytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(imageBytes))
                using (var original = Image.FromStream(ms))
                {
                    lock (_avatarImageCache)
                    {
                        _avatarImageCache[url] = new Bitmap(original);
                    }
                }
            }
            catch
            {
                // 네트워크 오류, 잘못된 URL, 이미지 디코딩 실패 등 — 조용히 무시.
            }
            finally
            {
                lock (_avatarImageCache)
                {
                    _avatarFetchInProgress.Remove(url);
                }
                RefreshChatItem(itemIndex);
            }
        }

        //***************************************************************************
        // @brief url의 미리보기 데이터를 요청한다. 이미 캐시에 있으면(로딩
        //        중이든 완료든) 새로 요청하지 않는다 — 같은 링크를 여러
        //        메시지가 참조해도 네트워크 요청은 한 번뿐이다.
        //***************************************************************************
        private void RequestLinkPreview(string url, int itemIndex)
        {
            lock (_linkPreviewCache)
            {
                if (_linkPreviewCache.ContainsKey(url))
                    return;

                _linkPreviewCache[url] = new LinkPreviewData();
            }

            _ = FetchLinkPreviewAsync(url, itemIndex);
        }

        //***************************************************************************
        // @brief url의 HTML을 받아와 제목/설명/썸네일을 정규식으로 뽑아낸다.
        //        HTML을 파싱/렌더링하는 게 아니라 메타 태그 몇 개만 문자열로
        //        추출하는 것이라 별도 HTML 파서 라이브러리 없이 구현했다.
        //***************************************************************************
        private async Task FetchLinkPreviewAsync(string url, int itemIndex)
        {
            LinkPreviewData data;
            lock (_linkPreviewCache)
            {
                data = _linkPreviewCache[url];
            }

            try
            {
                string html = await _httpClient.GetStringAsync(url);

                data.Title = ExtractMetaProperty(html, "og:title") ?? ExtractTitleTag(html) ?? url;
                data.Description = ExtractMetaProperty(html, "og:description") ?? ExtractMetaName(html, "description");

                string imageUrl = ExtractMetaProperty(html, "og:image");
                if (!string.IsNullOrEmpty(imageUrl))
                {
                    byte[] imageBytes = await _httpClient.GetByteArrayAsync(imageUrl);
                    using (var ms = new MemoryStream(imageBytes))
                    using (var original = Image.FromStream(ms))
                    {
                        // 스트림이 닫힌 뒤에도 쓸 수 있게 완전히 복사해둔다.
                        data.Thumbnail = new Bitmap(original);
                    }
                }
            }
            catch
            {
                // 네트워크 오류, 잘못된 URL, 이미지 디코딩 실패 등 — 데모
                // 범위에서는 그냥 "실패"로 표시하고 카드 없이 메시지만 남긴다.
                data.Failed = true;
            }
            finally
            {
                data.IsLoading = false;
                RefreshChatItem(itemIndex);
            }
        }

        //***************************************************************************
        // @brief 비동기 fetch가 끝난 뒤, 그 항목만 다시 측정/그리도록 강제한다.
        // @details WinForms ListBox(OwnerDrawVariable)는 "이 항목 하나만 다시
        //          측정해라" 같은 공개 API가 없다. Items[index]에 같은 값을
        //          다시 대입하면 내부적으로 "항목이 바뀌었다"고 인식해서
        //          MeasureItem/DrawItem을 다시 태우는 부작용을 이용한 관용적인
        //          트릭이다.
        //***************************************************************************
        private void RefreshChatItem(int itemIndex)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            Invoke((MethodInvoker)delegate
            {
                if (itemIndex >= 0 && itemIndex < _listBoxChat.Items.Count)
                {
                    var existing = _listBoxChat.Items[itemIndex];
                    _listBoxChat.Items[itemIndex] = existing;
                }
            });
        }

        //***************************************************************************
        // @brief <meta property="{property}" content="..."> 형태의 값을 뽑는다.
        //        속성 순서(content가 먼저 오는 경우)도 같이 지원한다.
        //***************************************************************************
        private static string ExtractMetaProperty(string html, string property)
        {
            var m = Regex.Match(html,
                $"<meta[^>]+property=[\"']{Regex.Escape(property)}[\"'][^>]+content=[\"']([^\"']*)[\"']",
                RegexOptions.IgnoreCase);
            if (!m.Success)
            {
                m = Regex.Match(html,
                    $"<meta[^>]+content=[\"']([^\"']*)[\"'][^>]+property=[\"']{Regex.Escape(property)}[\"']",
                    RegexOptions.IgnoreCase);
            }
            return m.Success ? WebUtility.HtmlDecode(m.Groups[1].Value) : null;
        }

        //***************************************************************************
        // @brief <meta name="{name}" content="..."> 형태의 값을 뽑는다.
        //***************************************************************************
        private static string ExtractMetaName(string html, string name)
        {
            var m = Regex.Match(html,
                $"<meta[^>]+name=[\"']{Regex.Escape(name)}[\"'][^>]+content=[\"']([^\"']*)[\"']",
                RegexOptions.IgnoreCase);
            if (!m.Success)
            {
                m = Regex.Match(html,
                    $"<meta[^>]+content=[\"']([^\"']*)[\"'][^>]+name=[\"']{Regex.Escape(name)}[\"']",
                    RegexOptions.IgnoreCase);
            }
            return m.Success ? WebUtility.HtmlDecode(m.Groups[1].Value) : null;
        }

        private static string ExtractTitleTag(string html)
        {
            var m = Regex.Match(html, "<title[^>]*>([^<]*)</title>", RegexOptions.IgnoreCase);
            return m.Success ? WebUtility.HtmlDecode(m.Groups[1].Value.Trim()) : null;
        }

        private void AppendChat(string senderName, string senderProfileImageUrl, string message, bool isMyMessage)
        {
            // [추가] 마지막 메시지와 날짜(일 단위)가 다르면(또는 첫 메시지면)
            // 구분선을 먼저 끼워 넣는다 — 카카오톡 등에서 흔히 보이는 패턴.
            DateTime now = DateTime.Now;
            DateTime? lastDate = null;
            for (int i = _listBoxChat.Items.Count - 1; i >= 0; i--)
            {
                if (_listBoxChat.Items[i] is ChatBubbleItem prevItem)
                {
                    lastDate = prevItem.Timestamp.Date;
                    break;
                }
            }
            if (lastDate == null || lastDate.Value != now.Date)
                _listBoxChat.Items.Add(new DateSeparatorItem { Date = now });

            _listBoxChat.Items.Add(new ChatBubbleItem(senderName, senderProfileImageUrl, message, isMyMessage));
            int newIndex = _listBoxChat.Items.Count - 1;
            _listBoxChat.TopIndex = newIndex;

            // 방금 추가한 메시지에 URL이 있으면 미리보기 카드용 데이터를
            // 비동기로 요청한다 — 완료되면 이 특정 항목만 다시 그려지도록
            // RefreshChatItem()이 트리거된다.
            Match urlMatch = UrlRegex.Match(message);
            if (urlMatch.Success)
                RequestLinkPreview(urlMatch.Value, newIndex);
        }

        private void AppendSystemLog(string text, Color color)
        {
            _listBoxMsg.Items.Add(new ColoredEntry { Text = text, Color = color, Timestamp = DateTime.Now });
            _listBoxMsg.TopIndex = _listBoxMsg.Items.Count - 1;
        }

        //***************************************************************************
        // @brief 현재 위치(로비/방 번호) 표시와 방 입장/나가기 버튼의
        //        활성화 상태를 한 번에 갱신한다.
        //***************************************************************************
        private void UpdateRoomStatusUI()
        {
            if (_currentRoomId < 0)
            {
                _lblCurrentRoom.Text = "위치: (로그인 전)";
                _btnRoomEnter.Enabled = false;
                _btnRoomLeave.Enabled = false;
                return;
            }

            bool inLobby = (_currentRoomId == ProtocolConstants.LobbyRoomId);
            _lblCurrentRoom.Text = inLobby ? "위치: 로비" : $"위치: {_currentRoomId}번 방";
            _btnRoomEnter.Enabled = true;
            _btnRoomLeave.Enabled = !inLobby;
        }

        //***************************************************************************
        // @brief "접속" 버튼 — 로컬에 저장된 계정이 있으면 재접속, 없으면
        //        입력한 프로필 이름을 원하는 닉네임으로 신규 가입을 시도합니다.
        //***************************************************************************
        //***************************************************************************
        // @brief 접속/끊기를 하나의 버튼으로 통합 — 현재 버튼 텍스트로 상태를
        //        판단한다("접속"이면 연결 시도, "끊기"면 연결 종료).
        //***************************************************************************
        private void BtnConnect_Click(object sender, EventArgs e)
        {
            if (_btnConnect.Text == "끊기")
            {
                _client?.Close();
                return;
            }

            string serverIp = _txtServerIp.Text.Trim();
            if (string.IsNullOrEmpty(serverIp))
            {
                MessageBox.Show(this, "서버 IP를 입력하세요.", "알림");
                _txtServerIp.Focus();
                return;
            }

            string serverPort = _txtServerPort.Text.Trim();
            if (string.IsNullOrEmpty(serverPort))
            {
                MessageBox.Show(this, "포트를 입력하세요.", "알림");
                _txtServerPort.Focus();
                return;
            }

            string profileName = _txtProfileName.Text.Trim();
            if (string.IsNullOrEmpty(profileName))
            {
                MessageBox.Show(this, "프로필 이름을 입력하세요.", "알림");
                _txtProfileName.Focus();
                return;
            }

            if (!int.TryParse(serverPort, out int port))
            {
                MessageBox.Show(this, "포트 번호가 올바르지 않습니다.", "알림");
                return;
            }

            _profileName = profileName;
            LoadMyProfileImage(profileName);
            _currentRoomId = -1;
            UpdateRoomStatusUI();

            bool hasToken = AccountStorage.TryLoad(profileName, out byte[] publicId, out byte[] token);

            AppendSystemLog("[디버그] 계정 파일 경로: " + AccountStorage.GetAccountFilePath(profileName)
                + " (존재함: " + hasToken + ")", ColorSystemInfo);

            _client = new ChatNetworkClient();
            _client.LoginResultReceived += OnLoginResultReceived;
            _client.ChatMessageReceived += OnChatMessageReceived;
            _client.RoomEnterResultReceived += OnRoomEnterResultReceived;
            _client.RoomLeaveResultReceived += OnRoomLeaveResultReceived;
            _client.RoomUserCountChanged += OnRoomUserCountChanged;
            _client.ServerUserCountReceived += OnServerUserCountReceived;
            _client.SetProfileImageUrlResultReceived += OnSetProfileImageUrlResultReceived;
            // [추가] 업로드 토큰 발급 응답은 UploadMyProfileImage()의 await 흐름과
            // TaskCompletionSource로 연결한다 — 진행 중인 요청이 없을 때
            // (필드가 null일 때) 도착하면 조용히 무시한다.
            _client.UploadTokenReceived += data => _uploadTokenTcs?.TrySetResult(data);
            _client.Disconnected += OnDisconnected;
            _client.ErrorOccurred += OnErrorOccurred;

            _client.Connect(_txtServerIp.Text.Trim(), port, hasToken, profileName, publicId, token);

            SetStatus(hasToken ? "재접속 중..." : "가입 중...", Color.Orange);
            AppendSystemLog(hasToken ? "[시스템] 저장된 계정으로 재접속을 시도합니다." : "[시스템] 신규 가입을 시도합니다.", ColorSystemInfo);

            // [수정] 접속/끊기를 하나의 버튼으로 통합 — 연결 시도 시작과 동시에
            // "끊기"로 바뀌어서 진행 중인 접속 시도도 바로 취소할 수 있게 한다.
            _btnConnect.Text = "끊기";

            // [추가] 접속 시도 중/접속된 동안엔 이 값들을 바꿀 수 없게 잠근다 —
            // 이미 시작된(또는 진행 중인) 연결의 대상을 몰래 바꾸는 걸 막기 위함.
            // Disconnected 콜백에서 다시 풀어준다.
            _txtServerIp.Enabled = false;
            _txtServerPort.Enabled = false;
            _txtProfileName.Enabled = false;

            ForceBorderRepaint(_txtServerIp);
            ForceBorderRepaint(_txtServerPort);
            ForceBorderRepaint(_txtProfileName);
        }

        //***************************************************************************
        // @brief TextBox의 BorderStyle.Fixed3D 테두리는 논클라이언트 영역이라
        //        Invalidate()/Refresh()로는 안 지워진다(특히 포커스가 있다가
        //        Enabled=false가 된 컨트롤에서 두드러짐). BorderStyle을
        //        순간적으로 바꿨다가 원래대로 되돌리면 Windows가 논클라이언트
        //        영역까지 포함해서 강제로 다시 계산하게 만들 수 있다.
        //***************************************************************************
        private static void ForceBorderRepaint(TextBox txt)
        {
            BorderStyle original = txt.BorderStyle;
            txt.BorderStyle = BorderStyle.None;
            txt.BorderStyle = original;
        }

        private void TxtMessage_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true;
                BtnSend_Click(sender, e);
            }
        }

        //***************************************************************************
        // @brief 메시지 전송. 서버가 발신자에게도 그대로 에코해주므로, 여기서
        //        미리 말풍선(우측, 내 메시지)으로 채팅 로그에 찍고
        //        _pendingSentEchoes에 등록해둔다 — 나중에 그 에코가 도착하면
        //        중복 표시하지 않기 위함이다.
        //***************************************************************************
        private void BtnSend_Click(object sender, EventArgs e)
        {
            string msg = _txtMessage.Text;
            if (string.IsNullOrEmpty(msg))
                return;

            lock (_pendingSentEchoesLock)
            {
                _pendingSentEchoes.Enqueue(msg);
            }

            AppendChat(_currentNickname ?? "나", _myProfileImageUrl, msg, isMyMessage: true);

            _client?.SendChat(msg);
            _txtMessage.Clear();
        }

        //***************************************************************************
        // @brief "닉네임 변경" 버튼 — 별도 팝업(NicknameChangeDialog)을 띄운다.
        //        그 안에 자동 생성/변경 버튼이 들어있다. 이 버튼 자체는
        //        로그인 성공 전까진 화면에 아예 안 보인다(Visible 토글은
        //        OnLoginResultReceived/OnDisconnected에서 처리).
        //***************************************************************************
        private void BtnOpenNicknameDialog_Click(object sender, EventArgs e)
        {
            if (_client == null)
                return;

            using (var dialog = new NicknameChangeDialog(_client, _currentNickname))
            {
                dialog.ShowDialog(this);

                if (!string.IsNullOrEmpty(dialog.AppliedNickname))
                {
                    _currentNickname = dialog.AppliedNickname;
                    AppendSystemLog("[시스템] 닉네임 변경 성공 - " + dialog.AppliedNickname, ColorSystemOk);
                }
            }
        }

        private void BtnRoomEnter_Click(object sender, EventArgs e)
        {
            if (_cbChatRoomId.SelectedItem == null)
                return;

            int roomId = (int)_cbChatRoomId.SelectedItem;
            _client?.RequestRoomEnter(roomId);
        }

        private void BtnRoomLeave_Click(object sender, EventArgs e)
        {
            _client?.RequestRoomLeave();
        }

        // ── 네트워크 콜백들 — 전부 백그라운드 수신 스레드에서 호출된다.
        // UI 컨트롤은 반드시 Invoke()로 UI 스레드에 넘긴 뒤에만 건드린다. ──

        private void OnLoginResultReceived(LoginResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    AccountStorage.Save(_profileName, res.PublicId, res.Token);

                    // [수정] 접속 성공 = 초록으로 되돌림("성공은 초록"이라는
                    // 일반적인 관례대로).
                    SetStatus("연결됨", Color.FromArgb(47, 184, 112));

                    _currentNickname = res.Nickname;
                    _myProfileImageUrl = res.ProfileImageUrl ?? string.Empty;
                    AppendSystemLog("[시스템] 로그인 성공 - 닉네임: " + res.Nickname, ColorSystemOk);

                    // 서버가 기억하고 있는(=다른 사람에게 보이는) 프로필
                    // 이미지가 있으면 내 화면에도 그 실제 이미지를 반영한다
                    // — 로컬에서 마지막으로 설정한 이미지와 다를 수 있으므로
                    // (다른 기기에서 URL로 설정했을 수 있음) 로그인 시점의
                    // 서버 값을 우선한다.
                    if (!string.IsNullOrEmpty(_myProfileImageUrl))
                        LoadMyProfileImageFromUrl(_myProfileImageUrl);

                    // 갤러리는 로그인된 세션이 있어야 조회 가능(서버가
                    // IsLoggedIn() 확인)하므로, TCP 연결 시점이 아니라
                    // 로그인 성공 시점에 붙인다.
                    _galleryPanel.AttachClient(_client);

                    // 서버가 로그인 직후 자동으로 로비에 배정한다 — 클라이언트도
                    // 그 전제로 현재 위치를 로비로 잡아둔다(서버의 RoomUserCountNotify가
                    // 곧이어 도착해 실제 인원수도 채워줄 것).
                    _currentRoomId = ProtocolConstants.LobbyRoomId;
                    UpdateRoomStatusUI();

                    _btnSend.Enabled = true;
                    _cbChatRoomId.Enabled = true;
                    _txtMessage.Enabled = true;
                    _btnOpenNicknameDialog.Visible = true;
                    _picProfileImage.Visible = true;

                    // 로그인 성공 시점부터 39초 주기 폴링 시작.
                    _serverUserCountPollTimer.Start();
                }
                else
                {
                    string nicknameSuffix = string.IsNullOrEmpty(res.Nickname) ? "" : $" (닉네임: {res.Nickname})";
                    string reasonText = DescribeLoginResult(res.Reason);

                    AppendSystemLog("[시스템] 로그인 실패 - " + reasonText + nicknameSuffix, ColorSystemError);

                    // [수정] "로그인 실패" 텍스트를 상태 라벨에 띄우는 대신 알림창으로 보여준다.
                    MessageBox.Show(this, reasonText + nicknameSuffix, "로그인 실패",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);

                    // [수정] 서버 접속 전(앱 실행 직후) 상태로 완전히 되돌린다.
                    // Close()를 호출하면 네트워크 스레드가 스트림이 끊긴 걸 감지하고
                    // Disconnected 이벤트를 발생시키는데, OnDisconnected()가 이미
                    // "IP/포트/프로필 재활성화, 방/채팅 컨트롤 비활성화, 닉네임 변경
                    // 버튼 숨김" 등 초기 상태 복원 로직을 전부 갖고 있으므로 여기서
                    // 따로 반복하지 않고 그대로 재사용한다.
                    _client?.Close();
                }
            });
        }

        //***************************************************************************
        // @brief 채팅 메시지 수신. _pendingSentEchoes의 맨 앞과 일치하면 방금
        //        내가 보낸 메시지의 서버 에코이므로 조용히 소비하고 화면엔
        //        찍지 않는다(BtnSend_Click에서 이미 "나: ..."로 찍었음).
        //        일치하지 않으면 다른 사람이 보낸 메시지이므로, 서버가
        //        채워 보낸 발신자 닉네임을 그대로 표시한다.
        //***************************************************************************
        private void OnChatMessageReceived(ChatPacketData data)
        {
            bool isOwnEcho = false;
            lock (_pendingSentEchoesLock)
            {
                if (_pendingSentEchoes.Count > 0 && _pendingSentEchoes.Peek() == data.Message)
                {
                    _pendingSentEchoes.Dequeue();
                    isOwnEcho = true;
                }
            }

            if (isOwnEcho)
                return;

            Invoke((MethodInvoker)delegate { AppendChat(data.SenderNickname, data.SenderProfileImageUrl, data.Message, isMyMessage: false); });
        }

        private void OnRoomEnterResultReceived(RoomEnterResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    _currentRoomId = res.RoomId;
                    _txtRoomUserCount.Text = res.RoomUserCount.ToString();
                    UpdateRoomStatusUI();
                    AppendSystemLog($"[시스템] {res.RoomId}번 방 입장 성공 (인원 {res.RoomUserCount}명)", ColorSystemOk);
                }
                else
                {
                    string reasonText = (res.Reason == RoomResult.InvalidRoomId)
                        ? $"유효하지 않은 방 번호입니다(1~{ProtocolConstants.MaxRoomId})."
                        : "알 수 없는 오류";
                    AppendSystemLog("[시스템] 방 입장 실패 - " + reasonText, ColorSystemError);
                }
            });
        }

        private void OnRoomLeaveResultReceived(RoomLeaveResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    AppendSystemLog($"[시스템] {res.RoomId}번 방에서 나감 (남은 인원 {res.RoomUserCount}명)", ColorSystemOk);

                    _currentRoomId = ProtocolConstants.LobbyRoomId;
                    _txtRoomUserCount.Text = ""; // 로비 자체 인원수는 이 라벨이 아니라 RoomUserCountNotify로 별도 관리하지 않음(단순화)
                    UpdateRoomStatusUI();
                }
                else
                {
                    AppendSystemLog("[시스템] 방 퇴장 실패", ColorSystemError);
                }
            });
        }

        //***************************************************************************
        // @brief 지금 있는 방(로비 포함)의 인원수가 바뀌었다는 서버의 자발적
        //        알림. 내가 방을 옮길 때도 오고, 같은 방의 "다른" 누군가가
        //        들고나도 온다 — 지금 내가 있는 방에 대한 알림일 때만 화면
        //        숫자를 갱신한다(다른 방 알림은 안 옴 — 서버가 애초에 그
        //        방 멤버에게만 보내므로 필터링이 딱히 더 필요하진 않지만,
        //        방어적으로 한 번 더 확인한다).
        //***************************************************************************
        private void OnRoomUserCountChanged(RoomUserCountNotifyData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.RoomId == _currentRoomId && data.RoomId != ProtocolConstants.LobbyRoomId)
                    _txtRoomUserCount.Text = data.UserCount.ToString();
            });
        }

        private void OnServerUserCountReceived(ServerUserCountResData data)
        {
            Invoke((MethodInvoker)delegate
            {
                _txtServerUserCount.Text = data.UserCount.ToString();
                _txtLobbyUserCount.Text = data.LobbyUserCount.ToString();
            });
        }

        //***************************************************************************
        // @brief 프로필 이미지 URL 설정/해제 요청에 대한 서버 응답 처리.
        //        응답 패킷 자체엔 URL이 없어서, 요청 시점에 이 폼이 기억해둔
        //        _pendingProfileImageUrlRequest를 그대로 참고한다.
        //***************************************************************************
        private void OnSetProfileImageUrlResultReceived(SetProfileImageUrlResPacketData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.Success)
                {
                    ApplyProfileImageUrl(_pendingProfileImageUrlRequest ?? string.Empty);

                    if (string.IsNullOrEmpty(_myProfileImageUrl))
                        AppendSystemLog("[시스템] 프로필 이미지가 해제되었습니다.", ColorSystemOk);
                    else
                        AppendSystemLog("[시스템] 프로필 이미지가 설정되었습니다 - " + _myProfileImageUrl, ColorSystemOk);
                }
                else
                {
                    AppendSystemLog("[시스템] 프로필 이미지 설정 실패 - 서버 오류", ColorSystemError);
                }
            });
        }

        //***************************************************************************
        // @brief 대표 프로필 이미지 URL이 바뀔 때(설정/업로드/해제/갤러리에서
        //        선택·삭제) 화면에 그 결과를 반영하는 단일 진입점. 채팅 탭의
        //        작은 프로필 사진과 갤러리 탭의 큰 대표 이미지, 두 군데를
        //        항상 같이 갱신해서 두 화면이 서로 어긋나지 않게 한다.
        //***************************************************************************
        private void ApplyProfileImageUrl(string url)
        {
            _myProfileImageUrl = url ?? string.Empty;

            if (string.IsNullOrEmpty(_myProfileImageUrl))
            {
                _picProfileImage.Image?.Dispose();
                _picProfileImage.Image = null;
                _picProfileImage.BackColor = ControlPaint.Light(AccentColor, 0.9f);
            }
            else
            {
                LoadMyProfileImageFromUrl(_myProfileImageUrl);
            }

            _galleryPanel.SetActiveImagePreview(_myProfileImageUrl);
        }

        private void OnDisconnected()
        {
            Invoke((MethodInvoker)delegate
            {
                SetStatus("연결 끊김", Color.Gray);
                _serverUserCountPollTimer.Stop();
                _txtServerUserCount.Text = "";
                _txtLobbyUserCount.Text = "";
                AppendSystemLog("[시스템] 서버와 연결이 끊어졌습니다.", ColorSystemError);
                _btnConnect.Enabled = true;
                _btnConnect.Text = "접속";
                _btnSend.Enabled = false;
                _cbChatRoomId.Enabled = false;
                _txtMessage.Enabled = false;
                _btnOpenNicknameDialog.Visible = false;
                _picProfileImage.Visible = false;

                // [추가] 접속 시도 때 잠갔던 필드를 다시 풀어준다.
                _txtServerIp.Enabled = true;
                _txtServerPort.Enabled = true;
                _txtProfileName.Enabled = true;

                _currentRoomId = -1;
                _txtRoomUserCount.Text = "";
                UpdateRoomStatusUI();

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Clear();
                }

                _galleryPanel.DetachClient();
            });
        }

        private void OnErrorOccurred(Exception ex)
        {
            Invoke((MethodInvoker)delegate { AppendSystemLog("[오류] " + ex.Message, ColorSystemError); });
        }

        private void SetStatus(string text, Color color)
        {
            _lblStatus.Text = text;
            _lblStatus.ForeColor = color;
            _pnlStatusDot?.Invalidate(); // 점은 _lblStatus.ForeColor를 그대로 참조해서 그리므로 다시 그리기만 하면 됨
        }

        private static string DescribeLoginResult(LoginResult reason)
        {
            switch (reason)
            {
                case LoginResult.NicknameTaken: return "이미 사용 중인 닉네임입니다.";
                case LoginResult.InvalidNickname: return "닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).";
                case LoginResult.AccountNotFound: return "저장된 계정을 찾을 수 없습니다.";
                case LoginResult.TokenMismatch: return "토큰이 일치하지 않습니다(다른 기기의 잔여 토큰이거나 손상됨).";
                case LoginResult.DbError: return "서버 오류 - 잠시 후 다시 시도해주세요.";
                default: return reason.ToString();
            }
        }
    }
}