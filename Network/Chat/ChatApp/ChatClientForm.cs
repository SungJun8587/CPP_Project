
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
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Font = System.Drawing.Font;
using Image = System.Drawing.Image;

namespace ChatApp
{
    // [분리] 이 파일은 필드/생성자/테마 적용/레이아웃 구성(InitializeComponents)만
    // 담당한다. 렌더링/네트워크 이벤트/파일 업로드/링크 미리보기 등은 같은
    // partial class의 다른 파일(ChatClientForm.*.cs)로 나뉘어 있다 — 전부
    // 하나의 클래스이므로 필드/메서드는 어느 파일에서든 그대로 접근 가능하다.
    public partial class ChatClientForm : Form
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
            // [추가] 서버가 부여한 고유 메시지 ID — 삭제 요청 시 이 값을
            // 그대로 실어 보낸다. 내가 방금 보낸 메시지는 서버 에코가
            // 오기 전까지 0(미확정) 상태다(ApplyServerMessageId() 참고).
            public long MessageId;

            public ChatBubbleItem(string senderName, string senderProfileImageUrl, string message, bool isMyMessage, long messageId = 0)
            {
                SenderName = senderName;
                SenderProfileImageUrl = senderProfileImageUrl;
                Message = message;
                IsMyMessage = isMyMessage;
                Timestamp = DateTime.Now;
                MessageId = messageId;
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
        // [추가] 낙관적 업데이트(서버 응답 전에 미리 화면에 반영)가 실패로
        // 판명되면 되돌릴 "요청 직전 값". OnSetProfileImageUrlResultReceived()의
        // 실패 분기에서만 쓴다.
        private string _profileImageUrlBeforeRequest;

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
        private Button _btnAttachFile;
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

        //***************************************************************************
        // @brief [추가] 채팅 파일 첨부 메시지 감지용 — "[FILE:파일명:바이트수]URL"
        //        형식을 일반 텍스트 메시지에 실어 보낸다(새 패킷 타입 없이
        //        기존 ChatPacket을 그대로 재사용). 이 형식과 일치하면 일반
        //        텍스트/링크 미리보기 대신 파일 카드로 그린다. 파일명에
        //        콜론(:)이 있으면 파싱이 깨지므로, 보낼 때 미리 치환해둔다
        //        (SendFileAttachment() 참고).
        //***************************************************************************
        private static readonly Regex FileAttachmentRegex = new Regex(
            @"^\[FILE:(?<name>[^:]+):(?<size>\d+)\](?<url>https?://\S+)$",
            RegexOptions.Compiled);
        private readonly Dictionary<int, List<(string Url, RectangleF Bounds)>> _chatItemUrlRegions
            = new Dictionary<int, List<(string Url, RectangleF Bounds)>>();
        // [추가] 파일 첨부 카드의 × 삭제 버튼 클릭 영역(항목 인덱스 → 사각형).
        // 내가 보낸 파일에만 그려지고, ChatListBox_MouseClick에서 일반
        // "카드 클릭(파일 열기)" 판정보다 먼저 확인한다.
        private readonly Dictionary<int, RectangleF> _chatItemDeleteButtonRegions
            = new Dictionary<int, RectangleF>();
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
        // [수정] 대용량 파일(1GB+) 업로드를 지원하려면 60초는 턱없이 부족하다
        // — HttpClient.Timeout은 요청 전체(본문 전송 포함)에 적용되므로,
        // 느린 회선에서 5GB 파일을 보내면 몇 분 이상 걸릴 수 있다. 2시간으로
        // 넉넉히 잡았다(그래도 무한정은 아니게 — 정말로 응답이 영영 안
        // 오는 상황까지 무한 대기하진 않도록).
        private static readonly HttpClient _uploadHttpClient = new HttpClient { Timeout = TimeSpan.FromHours(2) };
        private readonly Dictionary<string, LinkPreviewData> _linkPreviewCache = new Dictionary<string, LinkPreviewData>();

        // [추가] 프로필 이미지 — 채팅 메시지에 실려오는 발신자의 profileImageUrl로
        // 실제 이미지를 내려받아 아바타 자리에 표시한다(링크 미리보기와 동일한
        // fetch 인프라/HttpClient를 재사용). URL 하나당 한 번만 요청(캐시).
        // 아직 못 받아왔거나 실패했으면 DrawAvatar()가 색깔 원형+이니셜로 대체한다.
        private readonly Dictionary<string, Image> _avatarImageCache = new Dictionary<string, Image>();
        private readonly HashSet<string> _avatarFetchInProgress = new HashSet<string>();

        private const int kLinkPreviewCardHeight = 66;
        private const int kFileCardHeight = 50; // 파일 첨부 카드(아이콘+파일명+용량) 고정 높이
        private const int kLinkPreviewThumbnailSize = 56;

        private ListBox _listBoxMsg;

        private ChatNetworkClient _client;
        private string _profileName;
        private int _currentRoomId = -1; // -1: 아직 로그인 전(로비 배정 전). 로그인하면 서버가 0(로비)으로 넣어준다.

        // 보낸/받은 메시지 구분용 — UI 스레드(Send)와 네트워크 수신 스레드
        // (OnChatMessageReceived, Invoke() 이전) 양쪽에서 건드리므로 락 필요.
        private readonly Queue<string> _pendingSentEchoes = new Queue<string>();
        private readonly object _pendingSentEchoesLock = new object();

        // [수정] 로그 레벨별 색상 팔레트 — 시스템 로그 카드 우측 범례
        // (_pnlLogLegend, InitializeComponents 참고)와 반드시 같은 순서/색을
        // 유지해야 한다. Debug/Trace/Warning은 아직 실제로 호출하는 곳이
        // 없지만(현재는 Ok/Error/Info만 씀), 범례에는 항상 다섯 개 전부
        // 표시해서 나중에 로그를 세분화할 때 바로 쓸 수 있게 해뒀다.
        private static readonly Color ColorSystemDebug = Color.White;
        private static readonly Color ColorSystemTrace = Color.Blue;
        private static readonly Color ColorSystemInfo = Color.Green;
        private static readonly Color ColorSystemWarning = Color.Gold; // 순수 Yellow는 흰 배경에서 거의 안 보여서 조금 더 진한 톤을 씀
        private static readonly Color ColorSystemError = Color.Red;
        private static readonly Color ColorSystemOk = Color.Green;

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
        // @brief TextBox 등 일부 컨트롤은 Enabled 전환 시 테두리가 비클라이언트
        //        (프레임) 영역에 그려져서, Control.Invalidate()/Refresh()(클라이언트
        //        영역만 다시 그림)로는 갱신이 안 되는 경우가 있다 — 포커스가
        //        있던 컨트롤에서 특히 두드러졌다(Focus() 이동만으로는 완전히
        //        해결 안 됨). Win32 RedrawWindow()를 RDW_FRAME과 함께 호출하면
        //        프레임(테두리)까지 확실히 강제로 다시 그린다.
        //***************************************************************************
        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool RedrawWindow(IntPtr hWnd, IntPtr lprcUpdate, IntPtr hrgnUpdate, uint flags);

        private const uint RDW_INVALIDATE = 0x0001;
        private const uint RDW_FRAME = 0x0400;
        private const uint RDW_UPDATENOW = 0x0100;
        private const uint RDW_ALLCHILDREN = 0x0080;

        private static void ForceFullRedraw(Control control)
        {
            if (control != null && control.IsHandleCreated)
                RedrawWindow(control.Handle, IntPtr.Zero, IntPtr.Zero, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }

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
            RefreshDynamicButtonColors(_btnAttachFile);
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

            // [추가] 파일 첨부 — 입력창 왼쪽에 작은 버튼으로 배치. 클릭하면
            // 파일 선택 창이 뜨고, 업로드가 끝나면 자동으로 채팅 메시지로 전송된다.
            _btnAttachFile = new Button { Text = "📎", Left = 10, Top = 338, Width = 36, Height = 24, Enabled = false };
            _btnAttachFile.Click += (s, e) => AttachAndSendFile();
            StyleDynamicButton(_btnAttachFile);

            _txtMessage = new TextBox { Left = 52, Top = 338, Width = 432, Height = 24, Enabled = false, BorderStyle = BorderStyle.FixedSingle, Multiline = true };
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

            // [추가] 파일을 마우스로 끌어다 놓으면 바로 첨부되도록 — 이벤트
            // 자체는 ChatListBox_DragEnter/DragDrop(ChatClientForm.Display.cs)에서
            // 처리하고, 실제 업로드는 SendFileAttachmentAsync(ChatClientForm.Media.cs,
            // 파일 선택 대화상자 경로와 공유)가 담당한다.
            _listBoxChat.AllowDrop = true;
            _listBoxChat.DragEnter += ChatListBox_DragEnter;
            _listBoxChat.DragDrop += ChatListBox_DragDrop;

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
                _btnAttachFile, _txtMessage, _btnSend, _listBoxChat,
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

            // [추가] 로그 색상 범례 — "□DEBUG □TRACE □INFO □WARN □ERROR" 형태로
            // 작은 색상 사각형 + 라벨을 제목 오른쪽에 나란히 그린다. 항목/색은
            // 위쪽 ColorSystem* 필드들과 반드시 같은 순서·색을 유지해야 한다.
            var legendItems = new (string Label, Color Color)[]
            {
                ("DEBUG", ColorSystemDebug),
                ("TRACE", ColorSystemTrace),
                ("INFO", ColorSystemInfo),
                ("WARN", ColorSystemWarning),
                ("ERROR", ColorSystemError),
            };
            var pnlLogLegend = new Panel { Left = 288, Top = 3, Width = 260, Height = 15 };
            pnlLogLegend.Paint += (s, e) =>
            {
                e.Graphics.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                using (var legendFont = new Font(Font.FontFamily, 7f))
                using (var textBrush = new SolidBrush(TextSecondaryColor))
                {
                    const int kSquareSize = 8;
                    int x = 0;
                    foreach (var item in legendItems)
                    {
                        var squareRect = new Rectangle(x, (pnlLogLegend.Height - kSquareSize) / 2, kSquareSize, kSquareSize);
                        using (var squareBrush = new SolidBrush(item.Color))
                            e.Graphics.FillRectangle(squareBrush, squareRect);
                        e.Graphics.DrawRectangle(Pens.Gray, squareRect);

                        x += kSquareSize + 3;
                        SizeF textSize = e.Graphics.MeasureString(item.Label, legendFont);
                        e.Graphics.DrawString(item.Label, legendFont, textBrush, x, (pnlLogLegend.Height - textSize.Height) / 2);
                        x += (int)textSize.Width + 8;
                    }
                }
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
            card3.Controls.AddRange(new Control[] { lblCard3Title, pnlLogLegend, _listBoxMsg });

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
    }
}