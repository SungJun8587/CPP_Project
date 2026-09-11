
//***************************************************************************
// ChatClientForm.cs : 메인 창 — 접속/채팅/닉네임 변경/방 입퇴장 UI.
//
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
            public override string ToString() => Text; // 접근성 등 폴백 경로용
        }

        //***************************************************************************
        // @brief listBoxChat(채팅 로그)에 카카오톡 스타일 말풍선으로 그리기 위한 항목.
        //        내가 보낸 메시지는 우측 정렬(노란 계열), 상대는 좌측 정렬(흰색)로 그린다.
        //***************************************************************************
        private class ChatBubbleItem
        {
            public string SenderName;
            public string Message;
            public bool IsMyMessage;
            public DateTime Timestamp;

            public ChatBubbleItem(string senderName, string message, bool isMyMessage)
            {
                SenderName = senderName;
                Message = message;
                IsMyMessage = isMyMessage;
                Timestamp = DateTime.Now;
            }
        }

        private TextBox _txtServerIp;
        private TextBox _txtServerPort;
        private TextBox _txtProfileName;
        private Button _btnConnect;
        private Button _btnDisconnect;
        private Label _lblStatus;
        private TextBox _txtServerUserCount;
        private System.Windows.Forms.Timer _serverUserCountPollTimer;

        private string _currentNickname; // 화면에 별도로 표시하지 않고, 닉네임 변경 다이얼로그에 넘겨줄 용도로만 보관
        private Button _btnOpenNicknameDialog;

        private ComboBox _cbChatRoomId;
        private Button _btnRoomEnter;
        private Button _btnRoomLeave;
        private TextBox _txtLobbyUserCount;
        private TextBox _txtRoomUserCount;
        private Label _lblCurrentRoom;

        private TextBox _txtMessage;
        private Button _btnSend;
        private Button _btnChatBackground;
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
        private readonly Dictionary<string, LinkPreviewData> _linkPreviewCache = new Dictionary<string, LinkPreviewData>();

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

        private void InitializeComponents()
        {
            Text = "채팅 클라이언트 (WinForms)";
            ClientSize = new Size(577, 630);
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            DoubleBuffered = true;

            // ── 그룹박스 1: 서버 접속 ──────────────────────────────────────
            var groupBox1 = new GroupBox { Text = "서버 접속", Left = 10, Top = 9, Width = 557, Height = 85 };

            var lblIp = new Label { Text = "서버 IP", Left = 11, Top = 25, Width = 45 };
            _txtServerIp = new TextBox { Left = 60, Top = 21, Width = 120, Text = "127.0.0.1" };

            var lblPort = new Label { Text = "포트", Left = 190, Top = 25, Width = 35 };
            _txtServerPort = new TextBox { Left = 225, Top = 21, Width = 60, Text = "30201" };

            var lblProfile = new Label { Text = "프로필 이름", Left = 11, Top = 51, Width = 80 };
            _txtProfileName = new TextBox { Left = 90, Top = 47, Width = 150 };

            // [추가] 프로필 이름 옆의 닉네임 변경 버튼 — 서버에 로그인 성공한
            // 뒤에만 보인다(Visible, 단순 Enabled가 아님 — 로그인 전에는
            // 아예 존재를 드러내지 않는다는 요구사항). 누르면 별도 팝업
            // (NicknameChangeDialog)이 뜨고, 그 안에 자동 생성/변경 버튼이 있다.
            _btnOpenNicknameDialog = new Button { Text = "닉네임 변경", Left = 245, Top = 46, Width = 90, Height = 25, Visible = false };
            _btnOpenNicknameDialog.Click += BtnOpenNicknameDialog_Click;

            _btnConnect = new Button { Text = "접속", Left = 390, Top = 19, Width = 52, Height = 25 };
            _btnConnect.Click += BtnConnect_Click;

            _btnDisconnect = new Button { Text = "끊기", Left = 445, Top = 19, Width = 52, Height = 25, Enabled = false };
            _btnDisconnect.Click += BtnDisconnect_Click;

            _lblStatus = new Label { Text = "연결 안 됨", Left = 390, Top = 47, Width = 160, ForeColor = Color.Gray };

            groupBox1.Controls.AddRange(new Control[]
            {
                lblIp, _txtServerIp, lblPort, _txtServerPort, lblProfile, _txtProfileName, _btnOpenNicknameDialog,
                _btnConnect, _btnDisconnect, _lblStatus,
            });

            // ── 그룹박스 2: 채팅방(로비/룸) ──────────────────────────────
            var groupBox2 = new GroupBox { Text = "채팅방", Left = 10, Top = 100, Width = 557, Height = 374 };

            _cbChatRoomId = new ComboBox { Left = 11, Top = 23, Width = 71, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = false };
            for (int roomId = 1; roomId <= ProtocolConstants.MaxRoomId; roomId++)
                _cbChatRoomId.Items.Add(roomId);
            if (_cbChatRoomId.Items.Count > 0)
                _cbChatRoomId.SelectedIndex = 0;

            _btnRoomEnter = new Button { Text = "방 입장", Left = 88, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomEnter.Click += BtnRoomEnter_Click;

            _btnRoomLeave = new Button { Text = "방 나가기", Left = 163, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomLeave.Click += BtnRoomLeave_Click;

            // [추가] 채팅창 배경 설정 — 색상/이미지 선택 또는 기본값 초기화를
            // 컨텍스트 메뉴로 고르게 한다. 방 선택 줄 오른쪽에 남는 공간에 배치.
            var chatBackgroundMenu = new ContextMenuStrip();
            chatBackgroundMenu.Items.Add("색상으로 설정", null, (s, e) => SetChatBackgroundColor());
            chatBackgroundMenu.Items.Add("이미지로 설정", null, (s, e) => SetChatBackgroundImage());
            chatBackgroundMenu.Items.Add("기본값으로 초기화", null, (s, e) => ResetChatBackground());

            _btnChatBackground = new Button { Text = "배경 설정", Left = 480, Top = 22, Width = 67, Height = 25 };
            _btnChatBackground.Click += (s, e) => chatBackgroundMenu.Show(_btnChatBackground, new Point(0, _btnChatBackground.Height));

            // [수정] 서버 동접자수를 로비 유저수 왼쪽에 배치 — 프로필 이름과
            // 같은 형태(정적 라벨 + 읽기전용 텍스트박스)로 통일했다. 그룹박스1이
            // 아니라 여기(그룹박스2)로 옮긴 이유는 "로비 유저수 왼쪽"이라는
            // 배치 요구를 만족하려면 로비/방 유저수와 같은 줄에 있어야 하기
            // 때문이다.
            var lblServerUserCount = new Label { Text = "서버 동접자수 : ", Left = 11, Top = 58, Width = 94 };
            _txtServerUserCount = new TextBox { Left = 105, Top = 54, Width = 45, ReadOnly = true, TextAlign = HorizontalAlignment.Center };

            var lblLobbyUserCount = new Label { Text = "로비 유저수 : ", Left = 155, Top = 58, Width = 81 };
            _txtLobbyUserCount = new TextBox { Left = 236, Top = 54, Width = 45, ReadOnly = true, TextAlign = HorizontalAlignment.Center };

            var lblRoomUserCount = new Label { Text = "방 유저수 : ", Left = 286, Top = 58, Width = 68 };
            _txtRoomUserCount = new TextBox { Left = 354, Top = 54, Width = 45, ReadOnly = true, TextAlign = HorizontalAlignment.Center };

            _lblCurrentRoom = new Label { Text = "위치: (로그인 전)", Left = 404, Top = 58, Width = 150, ForeColor = Color.Gray };

            _txtMessage = new TextBox { Left = 10, Top = 88, Width = 465, Height = 26, Enabled = false, BorderStyle = BorderStyle.FixedSingle, Multiline = true };
            _txtMessage.KeyDown += TxtMessage_KeyDown;

            _btnSend = new Button { Text = "채팅", Left = 480, Top = 88, Width = 67, Height = 26, Enabled = false, Visible = true };
            // [추가] 기본 시스템 버튼 스타일은 Enabled=false일 때 테두리가
            // 테마에 따라 거의 안 보이게 그려지는 경우가 있다. FlatStyle로
            // 직접 그리게 하면 활성/비활성 상태와 무관하게 테두리가 항상
            // 보인다.
            _btnSend.FlatStyle = FlatStyle.Flat;
            _btnSend.FlatAppearance.BorderSize = 1;
            _btnSend.FlatAppearance.BorderColor = Color.Gray;
            _btnSend.Click += BtnSend_Click;

            _listBoxChat = new ListBox
            {
                Left = 10,
                Top = 123,
                Width = 537,
                Height = 233,
                DrawMode = DrawMode.OwnerDrawVariable,
                HorizontalScrollbar = false, // 말풍선 너비를 자동으로 줄바꿈하려면 가로 스크롤은 꺼둬야 함
                ScrollAlwaysVisible = true,
            };
            _listBoxChat.MeasureItem += ChatListBox_MeasureItem;
            _listBoxChat.DrawItem += ChatListBox_DrawItem;
            _listBoxChat.MouseClick += ChatListBox_MouseClick;
            _listBoxChat.MouseMove += ChatListBox_MouseMove;

            // 말풍선의 닉네임/시간 표시용 폰트 — DrawItem에서 재사용(캐시).
            _nameFont = new Font(_listBoxChat.Font.FontFamily, 8.5f, FontStyle.Bold);
            _timeFont = new Font(_listBoxChat.Font.FontFamily, 7.5f, FontStyle.Regular);

            groupBox2.Controls.AddRange(new Control[]
            {
                _cbChatRoomId, _btnRoomEnter, _btnRoomLeave, _btnChatBackground,
                lblServerUserCount, _txtServerUserCount, lblLobbyUserCount, _txtLobbyUserCount,
                lblRoomUserCount, _txtRoomUserCount, _lblCurrentRoom,
                _txtMessage, _btnSend, _listBoxChat,
            });

            // ── 하단: 시스템 로그(로그인/닉네임 변경/방 입퇴장 결과/연결 끊김/오류 등) ──
            _listBoxMsg = new ListBox
            {
                Left = 10,
                Top = 480,
                Width = 557,
                Height = 139,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 16,
                ScrollAlwaysVisible = true,
            };
            _listBoxMsg.DrawItem += ColoredListBox_DrawItem;

            Controls.AddRange(new Control[] { groupBox1, groupBox2, _listBoxMsg });

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
            using (var brush = new SolidBrush(entry.Color))
            {
                e.Graphics.DrawString(entry.Text, listBox.Font, brush, e.Bounds);
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

            // [수정 — 배경 설정 기능] e.DrawBackground()를 부르면 이 아이템
            // 영역이 시스템 기본색(또는 선택 하이라이트)으로 덮어써져서,
            // _listBoxChat.BackgroundImage/BackColor로 설정한 사용자 배경이
            // 안 보이게 된다. 컨트롤 자신의 배경(이미지 또는 BackColor)은
            // 이 콜백이 불리기 전에 이미 그려져 있으므로, 여기선 그냥
            // 그 위에 말풍선/텍스트만 얹으면 된다 — 별도로 지울 필요 없음.

            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            Rectangle bounds = e.Bounds;

            if (_listBoxChat.Items[e.Index] is ChatBubbleItem item)
            {
                const int kBubbleCornerRadius = 10;

                Font msgFont = _listBoxChat.Font;

                Brush myBubbleColor = Brushes.Khaki;
                Brush otherBubbleColor = Brushes.White;
                Brush textColor = Brushes.Black;
                Brush nameColor = Brushes.DimGray;
                Brush timeColor = Brushes.Gray;

                int maxBubbleWidth = (int)(bounds.Width * 0.65);
                SizeF textSize = g.MeasureString(item.Message, msgFont, maxBubbleWidth);

                int bubbleWidth = (int)textSize.Width + 16;
                int bubbleHeight = (int)textSize.Height + 10;

                string timeStr = item.Timestamp.ToString("tt h:mm");

                if (item.IsMyMessage)
                {
                    // ── 내가 보낸 메시지 — 우측 배치 ──────────────────────
                    int bubbleX = bounds.Right - bubbleWidth - 10;
                    int bubbleY = bounds.Top + 5;
                    Rectangle bubbleRect = new Rectangle(bubbleX, bubbleY, bubbleWidth, bubbleHeight);

                    using (var path = GetRoundedRectPath(bubbleRect, kBubbleCornerRadius))
                    {
                        g.FillPath(myBubbleColor, path);
                        g.DrawPath(Pens.DarkKhaki, path);
                    }

                    DrawMessageWithLinks(g, item.Message, msgFont, textColor, new RectangleF(bubbleX + 8, bubbleY + 5, maxBubbleWidth, textSize.Height), e.Index);

                    SizeF timeSize = g.MeasureString(timeStr, _timeFont);
                    g.DrawString(timeStr, _timeFont, timeColor, bubbleX - timeSize.Width - 5, bubbleY + bubbleHeight - timeSize.Height);

                    // 내 메시지는 우측 정렬이라, 미리보기 카드도 말풍선과 같은
                    // 오른쪽 기준선에 맞춘다.
                    DrawLinkPreviewCardIfAny(g, item.Message, bounds.Right - Math.Min(maxBubbleWidth, 220) - 10,
                        bubbleY + bubbleHeight + 4, Math.Min(maxBubbleWidth, 220), e.Index);
                }
                else
                {
                    // ── 상대가 보낸 메시지 — 좌측 배치 ────────────────────
                    const int kNameHeight = 15;
                    int bubbleX = bounds.Left + 10;
                    int bubbleY = bounds.Top + kNameHeight + 5;

                    g.DrawString(item.SenderName, _nameFont, nameColor, bounds.Left + 10, bounds.Top + 2);

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
        private void SetChatBackgroundColor()
        {
            using (var dlg = new ColorDialog())
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                _listBoxChat.BackgroundImage?.Dispose();
                _listBoxChat.BackgroundImage = null;
                _listBoxChat.BackColor = dlg.Color;
                _listBoxChat.Invalidate();
            }
        }

        //***************************************************************************
        // @brief 채팅창 배경을 이미지 파일로 설정한다.
        // @details [알려진 한계] ListBox는 항목을 직접 그리는(OwnerDraw) 방식이라,
        //          배경 이미지는 스크롤과 무관하게 항상 컨트롤 클라이언트
        //          영역 기준으로 고정 배치된다(카카오톡의 "고정 배경화면"과
        //          비슷한 느낌 — 메시지가 스크롤돼도 배경 자체는 안 움직임).
        //          말풍선(불투명 배경)이 그려지는 부분은 당연히 이미지가
        //          가려진다.
        //***************************************************************************
        private void SetChatBackgroundImage()
        {
            using (var dlg = new OpenFileDialog { Filter = "이미지 파일|*.png;*.jpg;*.jpeg;*.bmp;*.gif" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                try
                {
                    // 파일을 직접 스트림으로 열어둔 채로 Image를 만들면 그 파일이
                    // 잠겨서 나중에 못 지우거나 다시 못 여는 경우가 있다 — 메모리로
                    // 복사한 뒤(Bitmap 생성자) 파일 핸들 의존성을 끊는다.
                    using (var original = Image.FromFile(dlg.FileName))
                    {
                        _listBoxChat.BackgroundImage?.Dispose();
                        _listBoxChat.BackgroundImage = new Bitmap(original);
                    }

                    _listBoxChat.BackgroundImageLayout = ImageLayout.Stretch;
                    _listBoxChat.Invalidate();
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "이미지를 불러오지 못했습니다: " + ex.Message, "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        //***************************************************************************
        // @brief 채팅창 배경을 기본값(흰색, 이미지 없음)으로 되돌린다.
        //***************************************************************************
        private void ResetChatBackground()
        {
            _listBoxChat.BackgroundImage?.Dispose();
            _listBoxChat.BackgroundImage = null;
            _listBoxChat.BackColor = SystemColors.Window;
            _listBoxChat.Invalidate();
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

        private void AppendChat(string senderName, string message, bool isMyMessage)
        {
            _listBoxChat.Items.Add(new ChatBubbleItem(senderName, message, isMyMessage));
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
            _listBoxMsg.Items.Add(new ColoredEntry { Text = text, Color = color });
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
        private void BtnConnect_Click(object sender, EventArgs e)
        {
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
            _client.Disconnected += OnDisconnected;
            _client.ErrorOccurred += OnErrorOccurred;

            _client.Connect(_txtServerIp.Text.Trim(), port, hasToken, profileName, publicId, token);

            SetStatus(hasToken ? "재접속 중..." : "가입 중...", Color.Orange);
            AppendSystemLog(hasToken ? "[시스템] 저장된 계정으로 재접속을 시도합니다." : "[시스템] 신규 가입을 시도합니다.", ColorSystemInfo);

            _btnConnect.Enabled = false;
            _btnDisconnect.Enabled = true;

            // [추가] 접속 시도 중/접속된 동안엔 이 값들을 바꿀 수 없게 잠근다 —
            // 이미 시작된(또는 진행 중인) 연결의 대상을 몰래 바꾸는 걸 막기 위함.
            // Disconnected 콜백에서 다시 풀어준다.
            _txtServerIp.Enabled = false;
            _txtServerPort.Enabled = false;
            _txtProfileName.Enabled = false;
        }

        private void BtnDisconnect_Click(object sender, EventArgs e)
        {
            _client?.Close();
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

            AppendChat(_currentNickname ?? "나", msg, isMyMessage: true);

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

                    // [수정] "로그인 성공" 텍스트를 따로 띄우지 않는다 — 채팅/방
                    // 관련 컨트롤이 활성화되는 것 자체가 성공의 신호다.
                    SetStatus(string.Empty, Color.Gray);

                    _currentNickname = res.Nickname;
                    AppendSystemLog("[시스템] 로그인 성공 - 닉네임: " + res.Nickname, ColorSystemOk);

                    // 서버가 로그인 직후 자동으로 로비에 배정한다 — 클라이언트도
                    // 그 전제로 현재 위치를 로비로 잡아둔다(서버의 RoomUserCountNotify가
                    // 곧이어 도착해 실제 인원수도 채워줄 것).
                    _currentRoomId = ProtocolConstants.LobbyRoomId;
                    UpdateRoomStatusUI();

                    _btnSend.Enabled = true;
                    _cbChatRoomId.Enabled = true;
                    _txtMessage.Enabled = true;
                    _btnOpenNicknameDialog.Visible = true;

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

            Invoke((MethodInvoker)delegate { AppendChat(data.SenderNickname, data.Message, isMyMessage: false); });
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
                    _txtRoomUserCount.Clear(); // 로비 자체 인원수는 이 텍스트박스가 아니라 RoomUserCountNotify로 별도 관리하지 않음(단순화)
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

        private void OnDisconnected()
        {
            Invoke((MethodInvoker)delegate
            {
                SetStatus("연결 끊김", Color.Gray);
                _serverUserCountPollTimer.Stop();
                _txtServerUserCount.Clear();
                _txtLobbyUserCount.Clear();
                AppendSystemLog("[시스템] 서버와 연결이 끊어졌습니다.", ColorSystemError);
                _btnConnect.Enabled = true;
                _btnDisconnect.Enabled = false;
                _btnSend.Enabled = false;
                _cbChatRoomId.Enabled = false;
                _txtMessage.Enabled = false;
                _btnOpenNicknameDialog.Visible = false;

                // [추가] 접속 시도 때 잠갔던 필드를 다시 풀어준다.
                _txtServerIp.Enabled = true;
                _txtServerPort.Enabled = true;
                _txtProfileName.Enabled = true;

                _currentRoomId = -1;
                _txtRoomUserCount.Clear();
                UpdateRoomStatusUI();

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Clear();
                }
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