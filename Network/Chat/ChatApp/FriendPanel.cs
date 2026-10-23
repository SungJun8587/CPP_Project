
//***************************************************************************
// FriendPanel.cs : 대화 탭의 채팅 카드 오른쪽에 붙는 친구 패널.
//
// 세 개의 하위 탭으로 구성된다.
//   - 친구 : 접속 상태가 표시되는 친구 목록 + 닉네임으로 친구 추가.
//            행을 오른쪽 클릭하면 친구 삭제 / 차단 메뉴가 나온다.
//   - 요청 : 받은 친구 요청 목록(페이지 단위) + 수락/거절.
//   - 차단 : 차단한 사용자 목록 + 닉네임으로 차단 + 해제.
//
// 서버와의 통신은 ChatNetworkClient의 친구 관련 이벤트/요청 메서드를 그대로
// 쓰며, AttachClient()/DetachClient()로 로그인/연결 종료 시점에 구독을
// 시작/해제한다(RoomListPanel/RoomMemberListPanel과 같은 패턴).
// 상태 변화(요청 도착, 접속/종료, 삭제 등)는 Notice 이벤트로 알리고, 호스트
// 폼이 시스템 로그에 기록한다. 패널 하단에도 마지막 알림을 잠시 보여준다.
//
// [스레드] ChatNetworkClient의 이벤트는 수신 스레드에서 호출되므로 모든
// 핸들러가 Post()로 UI 스레드에 넘긴 뒤에만 컨트롤을 건드린다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading.Tasks;
using System.Windows.Forms;
using Font = System.Drawing.Font;
using Image = System.Drawing.Image;

namespace ChatApp
{
    public enum FriendNoticeLevel
    {
        Info,
        Warning,
        Error,
    }

    public class FriendPanel : UserControl
    {
        private enum FriendTab
        {
            Friends,
            Requests,
            Blocks,
        }

        private enum RowKind
        {
            Friend,
            Request,
            Block,
        }

        private const int PendingPageSize = 20;
        private const int RowHeight = 52;

        private static readonly Color[] AvatarPalette =
        {
            Color.FromArgb(255, 107, 107), Color.FromArgb(78, 205, 196), Color.FromArgb(69, 183, 209),
            Color.FromArgb(150, 206, 180), Color.FromArgb(255, 195, 113), Color.FromArgb(162, 155, 254),
            Color.FromArgb(253, 121, 168), Color.FromArgb(129, 236, 236),
        };

        //***************************************************************************
        // @brief 목록의 한 사람. 친구/받은 요청/차단 목록이 같은 형태를 쓴다
        //        (차단 목록은 ProfileImageUrl이, 요청/차단 목록은 Online이 의미 없다).
        //***************************************************************************
        private class PersonEntry
        {
            public byte[] PublicId;
            public string Key;
            public string Nickname;
            public string ProfileImageUrl;
            public bool Online;
        }

        private ChatNetworkClient _client;

        private FriendTab _tab = FriendTab.Friends;

        // 친구 목록 — 서버 스트림이 끝나면(End) 한 번에 교체한다.
        private readonly Dictionary<string, PersonEntry> _friends = new Dictionary<string, PersonEntry>();
        private List<PersonEntry> _incomingFriends = new List<PersonEntry>();

        // 받은 요청 — 현재 페이지의 항목과 서버가 알려준 전체 개수.
        private List<PersonEntry> _pending = new List<PersonEntry>();
        private List<PersonEntry> _incomingPending = new List<PersonEntry>();
        private int _pendingTotal;
        private int _pendingPage;
        private int _pendingRequestId;
        private readonly HashSet<string> _respondInFlight = new HashSet<string>();

        // 차단 목록.
        private List<PersonEntry> _blocks = new List<PersonEntry>();
        private List<PersonEntry> _incomingBlocks = new List<PersonEntry>();
        private readonly HashSet<string> _unblockInFlight = new HashSet<string>();

        // 입력창(친구 추가/차단) 요청은 한 번에 하나만 보낸다 — 응답에 닉네임이
        // 없어서, 보낸 닉네임과 응답을 정확히 짝지으려면 하나씩 처리해야 한다.
        private bool _inputInFlight;
        private string _inputInFlightNickname;

        // 우클릭 메뉴로 보낸 차단 요청 — 입력창 요청과 동시에 두 개를 보내지 않는다
        // (BlockRes에 실패 시 대상 식별자가 없어서, 하나씩만 보내야 응답을 짝지을 수 있다).
        private bool _menuBlockInFlight;
        private string _menuBlockNickname;

        private PersonEntry _menuEntry;

        private Label _lblTitle;
        private Label _lblCount;
        private Button _btnRefresh;
        private Button _btnTabFriends;
        private Button _btnTabRequests;
        private Button _btnTabBlocks;
        private Panel _inputRow;
        private TextBox _txtInput;
        private Panel _inputHost;
        private Button _btnInput;
        private Label _lblStatus;
        private FlowLayoutPanel _flow;
        private Label _lblNotice;
        private System.Windows.Forms.Timer _noticeTimer;
        private Panel _pagerPanel;
        private Button _btnPrev;
        private Button _btnNext;
        private Label _lblPage;
        private ContextMenuStrip _friendMenu;

        private static readonly HttpClient _thumbnailHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        private readonly Dictionary<string, Image> _thumbnailCache = new Dictionary<string, Image>();
        private readonly Dictionary<string, List<FriendRow>> _thumbnailPendingRows = new Dictionary<string, List<FriendRow>>();

        //***************************************************************************
        // @brief 상태 변화 알림(요청 도착, 접속/종료, 결과 등). 호스트 폼이 시스템
        //        로그에 기록한다. UI 스레드에서 호출된다.
        //***************************************************************************
        public event Action<string, FriendNoticeLevel> Notice;
        // 친구 우클릭 메뉴의 "대화하기" — 호스트가 메신저 탭으로 전환해 1:1 대화방을 연다.
        public event Action<byte[], string> ChatRequested;

        public FriendPanel()
        {
            InitializeComponents();
            ApplyConnectedState();
            Rebuild();
        }

        //***************************************************************************
        // @brief 로그인 성공 시점에 호출 — 이벤트 구독을 시작하고 세 목록을 불러온다.
        //***************************************************************************
        public void AttachClient(ChatNetworkClient client)
        {
            DetachClient();

            _client = client;
            _client.FriendRequestResultReceived += OnFriendRequestResult;
            _client.FriendRespondResultReceived += OnFriendRespondResult;
            _client.FriendRemoveResultReceived += OnFriendRemoveResult;
            _client.FriendListItemReceived += OnFriendListItem;
            _client.FriendListEndReceived += OnFriendListEnd;
            _client.FriendPendingItemReceived += OnFriendPendingItem;
            _client.FriendPendingEndReceived += OnFriendPendingEnd;
            _client.BlockResultReceived += OnBlockResult;
            _client.UnblockResultReceived += OnUnblockResult;
            _client.BlockListItemReceived += OnBlockListItem;
            _client.BlockListEndReceived += OnBlockListEnd;
            _client.FriendRequestNotified += OnFriendRequestNotify;
            _client.FriendAcceptedNotified += OnFriendAcceptedNotify;
            _client.FriendRemovedNotified += OnFriendRemovedNotify;
            _client.FriendProfileChangedNotified += OnFriendProfileChangedNotify;
            _client.FriendStatusNotified += OnFriendStatusNotify;

            ApplyConnectedState();
            ReloadAll();
        }

        //***************************************************************************
        // @brief 연결이 끊겼을 때 호출 — 구독을 해제하고 모든 상태를 비운다.
        //***************************************************************************
        public void DetachClient()
        {
            if (_client != null)
            {
                _client.FriendRequestResultReceived -= OnFriendRequestResult;
                _client.FriendRespondResultReceived -= OnFriendRespondResult;
                _client.FriendRemoveResultReceived -= OnFriendRemoveResult;
                _client.FriendListItemReceived -= OnFriendListItem;
                _client.FriendListEndReceived -= OnFriendListEnd;
                _client.FriendPendingItemReceived -= OnFriendPendingItem;
                _client.FriendPendingEndReceived -= OnFriendPendingEnd;
                _client.BlockResultReceived -= OnBlockResult;
                _client.UnblockResultReceived -= OnUnblockResult;
                _client.BlockListItemReceived -= OnBlockListItem;
                _client.BlockListEndReceived -= OnBlockListEnd;
                _client.FriendRequestNotified -= OnFriendRequestNotify;
                _client.FriendAcceptedNotified -= OnFriendAcceptedNotify;
                _client.FriendRemovedNotified -= OnFriendRemovedNotify;
                _client.FriendProfileChangedNotified -= OnFriendProfileChangedNotify;
                _client.FriendStatusNotified -= OnFriendStatusNotify;
                _client = null;
            }

            _friends.Clear();
            _incomingFriends = new List<PersonEntry>();
            _pending = new List<PersonEntry>();
            _incomingPending = new List<PersonEntry>();
            _pendingTotal = 0;
            _pendingPage = 0;
            _pendingRequestId = 0;
            _respondInFlight.Clear();
            _blocks = new List<PersonEntry>();
            _incomingBlocks = new List<PersonEntry>();
            _unblockInFlight.Clear();
            _inputInFlight = false;
            _inputInFlightNickname = null;
            _menuBlockInFlight = false;
            _menuBlockNickname = null;
            _menuEntry = null;
            _txtInput.Text = string.Empty;
            _lblNotice.Text = string.Empty;

            ApplyConnectedState();
            Rebuild();
        }

        //***************************************************************************
        // @brief 스킨이 바뀔 때 호출 — 생성 시점에 굳어진 색을 다시 입힌다.
        //***************************************************************************
        public void RefreshTheme()
        {
            _lblTitle.ForeColor = ChatTheme.Current.Accent;
            _lblNotice.ForeColor = ChatTheme.Current.Accent;
            UpdateTabButtons();
            StyleFilledButton(_btnInput);
            StyleOutlineButton(_btnRefresh);
            StyleOutlineButton(_btnPrev);
            StyleOutlineButton(_btnNext);
            Rebuild();
            Invalidate(true);
        }

        //***************************************************************************
        // @brief 처음 연결된 뒤 / 새로고침 버튼 — 세 목록을 모두 다시 요청한다.
        //***************************************************************************
        private void ReloadAll()
        {
            if (_client == null)
                return;

            _client.RequestFriendList();
            RequestPendingPage(0);
            _client.RequestBlockList();
            Rebuild();
        }

        private void RequestPendingPage(int page)
        {
            if (_client == null)
                return;

            _pendingRequestId = _client.RequestFriendPendingList(Math.Max(0, page), PendingPageSize);
        }

        #region UI 구성

        private void InitializeComponents()
        {
            BackColor = Color.White;
            Padding = new Padding(1);
            DoubleBuffered = true;

            // 제목 줄: "친구" + 인원 수 + 새로고침
            var titleBar = new Panel { Dock = DockStyle.Top, Height = 34 };
            _lblTitle = new Label
            {
                Text = "친구",
                Left = 12,
                Top = 8,
                Width = 50,
                Height = 20,
                Font = new Font(Font.FontFamily, 10.5f, FontStyle.Bold),
                ForeColor = ChatTheme.Current.Accent,
            };
            _lblCount = new Label
            {
                Left = 62,
                Top = 10,
                Width = 110,
                Height = 16,
                ForeColor = ChatTheme.Current.TextSecondary,
                Font = new Font(Font.FontFamily, 8f),
            };
            _btnRefresh = new Button { Text = "⟳", Width = 28, Height = 24, Top = 5 };
            _btnRefresh.Click += (s, e) => ReloadAll();
            StyleOutlineButton(_btnRefresh);
            titleBar.Controls.AddRange(new Control[] { _lblTitle, _lblCount, _btnRefresh });
            titleBar.Resize += (s, e) => _btnRefresh.Left = titleBar.ClientSize.Width - _btnRefresh.Width - 8;

            // 하위 탭 버튼 줄
            var tabRow = new Panel { Dock = DockStyle.Top, Height = 34, Padding = new Padding(8, 2, 8, 2) };
            _btnTabFriends = new Button { Text = "친구", Height = 26, Top = 4 };
            _btnTabRequests = new Button { Text = "요청", Height = 26, Top = 4 };
            _btnTabBlocks = new Button { Text = "차단", Height = 26, Top = 4 };
            _btnTabFriends.Click += (s, e) => SwitchTab(FriendTab.Friends);
            _btnTabRequests.Click += (s, e) => SwitchTab(FriendTab.Requests);
            _btnTabBlocks.Click += (s, e) => SwitchTab(FriendTab.Blocks);
            tabRow.Controls.AddRange(new Control[] { _btnTabFriends, _btnTabRequests, _btnTabBlocks });
            tabRow.Resize += (s, e) => LayoutTabButtons(tabRow);

            // 입력 줄: 닉네임으로 친구 추가 / 차단 (요청 탭에서는 숨김)
            _inputRow = new Panel { Dock = DockStyle.Top, Height = 38, Padding = new Padding(8, 4, 8, 4) };
            _inputHost = new Panel { Top = 5, Height = 26, BackColor = Color.White };
            _txtInput = new TextBox { BorderStyle = BorderStyle.None, Left = 6, Top = 5, MaxLength = 16 };
            _inputHost.Controls.Add(_txtInput);
            _inputHost.Paint += (s, e) =>
            {
                Color borderColor = _txtInput.Focused ? ChatTheme.Current.Accent : ChatTheme.Current.Border;
                using (var pen = new Pen(borderColor))
                    e.Graphics.DrawRectangle(pen, 0, 0, _inputHost.Width - 1, _inputHost.Height - 1);
            };
            _txtInput.Enter += (s, e) => _inputHost.Invalidate();
            _txtInput.Leave += (s, e) => _inputHost.Invalidate();
            _txtInput.KeyDown += (s, e) =>
            {
                if (e.KeyCode == Keys.Enter)
                {
                    e.SuppressKeyPress = true;
                    SubmitInput();
                }
            };
            _btnInput = new Button { Text = "추가", Width = 52, Height = 26, Top = 5 };
            _btnInput.Click += (s, e) => SubmitInput();
            StyleFilledButton(_btnInput);
            _inputRow.Controls.AddRange(new Control[] { _inputHost, _btnInput });
            _inputRow.Resize += (s, e) => LayoutInputRow();

            _lblStatus = new Label
            {
                Dock = DockStyle.Top,
                Height = 22,
                ForeColor = ChatTheme.Current.TextSecondary,
                Font = new Font(Font.FontFamily, 8f),
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(10, 0, 0, 0),
            };

            // 마지막 알림을 잠시 보여주는 줄(몇 초 뒤 자동으로 지워진다)
            _lblNotice = new Label
            {
                Dock = DockStyle.Bottom,
                Height = 34,
                ForeColor = ChatTheme.Current.Accent,
                Font = new Font(Font.FontFamily, 8f),
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(10, 0, 8, 0),
                AutoEllipsis = true,
            };
            _noticeTimer = new System.Windows.Forms.Timer { Interval = 7000 };
            _noticeTimer.Tick += (s, e) =>
            {
                _noticeTimer.Stop();
                _lblNotice.Text = string.Empty;
            };

            // 받은 요청 페이지 이동 줄 (요청 탭에서만 보임)
            _pagerPanel = new Panel { Dock = DockStyle.Bottom, Height = 36, Visible = false };
            _btnPrev = new Button { Text = "◀ 이전", Width = 70, Height = 26 };
            _btnNext = new Button { Text = "다음 ▶", Width = 70, Height = 26 };
            _lblPage = new Label
            {
                Text = "1 / 1",
                Width = 60,
                Height = 26,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font(Font.FontFamily, 9f, FontStyle.Bold),
            };
            _btnPrev.Enabled = false;
            _btnNext.Enabled = false;
            _btnPrev.Click += (s, e) => RequestPendingPage(_pendingPage - 1);
            _btnNext.Click += (s, e) => RequestPendingPage(_pendingPage + 1);
            StyleOutlineButton(_btnPrev);
            StyleOutlineButton(_btnNext);
            _pagerPanel.Controls.AddRange(new Control[] { _btnPrev, _lblPage, _btnNext });
            _pagerPanel.Resize += (s, e) => LayoutPager();

            _flow = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                Padding = new Padding(6, 4, 6, 4),
            };

            // 패널 폭이 바뀌면(또는 처음 배치될 때) 이미 만들어진 행들의 폭을 맞춘다.
            _flow.SizeChanged += (s, e) =>
            {
                int width = RowWidth();
                foreach (Control c in _flow.Controls)
                    c.Width = width;
            };

            _friendMenu = new ContextMenuStrip();
            var miChat = new ToolStripMenuItem("대화하기");
            var miRemove = new ToolStripMenuItem("친구 삭제");
            var miBlock = new ToolStripMenuItem("차단");
            miChat.Click += (s, e) =>
            {
                if (_menuEntry != null)
                    ChatRequested?.Invoke(_menuEntry.PublicId, _menuEntry.Nickname);
            };
            miRemove.Click += (s, e) => ConfirmRemoveFriend(_menuEntry);
            miBlock.Click += (s, e) => ConfirmBlock(_menuEntry);
            _friendMenu.Items.AddRange(new ToolStripItem[] { miChat, new ToolStripSeparator(), miRemove, miBlock });

            // Dock 순서: 나중에 추가한 것이 바깥(위)부터 자리를 차지한다.
            Controls.Add(_flow);
            Controls.Add(_pagerPanel);
            Controls.Add(_lblNotice);
            Controls.Add(_lblStatus);
            Controls.Add(_inputRow);
            Controls.Add(tabRow);
            Controls.Add(titleBar);

            LayoutTabButtons(tabRow);
            LayoutInputRow();
            LayoutPager();
            UpdateTabButtons();
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            using (var pen = new Pen(ChatTheme.Current.Accent))
                e.Graphics.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
        }

        private void LayoutTabButtons(Panel tabRow)
        {
            int total = tabRow.ClientSize.Width - tabRow.Padding.Horizontal;
            int gap = 4;
            int w = Math.Max(30, (total - gap * 2) / 3);
            int left = tabRow.Padding.Left;
            _btnTabFriends.SetBounds(left, 4, w, 26);
            _btnTabRequests.SetBounds(left + w + gap, 4, w, 26);
            _btnTabBlocks.SetBounds(left + (w + gap) * 2, 4, w, 26);
        }

        private void LayoutInputRow()
        {
            int total = _inputRow.ClientSize.Width - _inputRow.Padding.Horizontal;
            _btnInput.Left = _inputRow.Padding.Left + total - _btnInput.Width;
            _inputHost.Left = _inputRow.Padding.Left;
            _inputHost.Width = Math.Max(40, total - _btnInput.Width - 6);
            _txtInput.Width = _inputHost.Width - 12;
        }

        private void LayoutPager()
        {
            int totalWidth = _btnPrev.Width + _lblPage.Width + _btnNext.Width + 12;
            int left = Math.Max(0, (_pagerPanel.ClientSize.Width - totalWidth) / 2);
            int top = Math.Max(0, (_pagerPanel.ClientSize.Height - _btnPrev.Height) / 2);
            _btnPrev.Location = new Point(left, top);
            _lblPage.Location = new Point(left + _btnPrev.Width + 6, top);
            _btnNext.Location = new Point(left + _btnPrev.Width + 6 + _lblPage.Width + 6, top);
        }

        private static void StyleFilledButton(Button btn)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.Cursor = Cursors.Hand;
            btn.FlatAppearance.BorderSize = 1;
            btn.FlatAppearance.BorderColor = ChatTheme.Current.Accent;
            btn.BackColor = ChatTheme.Current.Accent;
            btn.ForeColor = Color.White;
        }

        private static void StyleOutlineButton(Button btn, bool danger = false)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.Cursor = Cursors.Hand;
            btn.FlatAppearance.BorderSize = 1;
            btn.FlatAppearance.BorderColor = ChatTheme.Current.Border;
            btn.BackColor = Color.White;
            btn.ForeColor = danger ? ChatTheme.Current.Danger : ChatTheme.Current.Accent;
        }

        //***************************************************************************
        // @brief 하위 탭 버튼의 선택 표시와 "요청" 버튼의 대기 개수 표시를 갱신한다.
        //***************************************************************************
        private void UpdateTabButtons()
        {
            StyleTabButton(_btnTabFriends, _tab == FriendTab.Friends);
            StyleTabButton(_btnTabRequests, _tab == FriendTab.Requests);
            StyleTabButton(_btnTabBlocks, _tab == FriendTab.Blocks);

            _btnTabRequests.Text = _pendingTotal > 0 ? $"요청 ({_pendingTotal})" : "요청";
            if (_pendingTotal > 0 && _tab != FriendTab.Requests)
                _btnTabRequests.ForeColor = ChatTheme.Current.Danger; // 확인하지 않은 요청이 있다는 표시
        }

        private static void StyleTabButton(Button btn, bool selected)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.Cursor = Cursors.Hand;
            btn.FlatAppearance.BorderSize = 1;
            if (selected)
            {
                btn.BackColor = ChatTheme.Current.Accent;
                btn.ForeColor = Color.White;
                btn.FlatAppearance.BorderColor = ChatTheme.Current.Accent;
            }
            else
            {
                btn.BackColor = Color.White;
                btn.ForeColor = Color.Black;
                btn.FlatAppearance.BorderColor = ChatTheme.Current.Border;
            }
        }

        //***************************************************************************
        // @brief 연결 여부에 따라 입력/버튼을 켜고 끈다.
        //***************************************************************************
        private void ApplyConnectedState()
        {
            bool connected = _client != null;
            _txtInput.Enabled = connected;
            _btnInput.Enabled = connected;
            _btnRefresh.Enabled = connected;
            _btnTabFriends.Enabled = connected;
            _btnTabRequests.Enabled = connected;
            _btnTabBlocks.Enabled = connected;
        }

        #endregion

        #region 목록 표시

        private void SwitchTab(FriendTab tab)
        {
            if (_tab == tab)
                return;

            _tab = tab;
            _txtInput.Text = string.Empty;
            Rebuild();
        }

        private static string Key(byte[] publicId)
        {
            return publicId == null ? string.Empty : BitConverter.ToString(publicId).Replace("-", string.Empty).ToLowerInvariant();
        }

        private int RowWidth()
        {
            return Math.Max(120, _flow.Width - SystemInformation.VerticalScrollBarWidth - _flow.Padding.Horizontal - 4);
        }

        //***************************************************************************
        // @brief 현재 하위 탭에 맞는 행들과 상태 줄/입력 줄/페이지 줄을 다시 만든다.
        //***************************************************************************
        private void Rebuild()
        {
            if (IsDisposed)
                return;

            UpdateTabButtons();

            // 입력 줄은 친구/차단 탭에서만, 페이지 줄은 요청 탭에서만 보인다.
            _inputRow.Visible = _tab != FriendTab.Requests;
            _pagerPanel.Visible = _tab == FriendTab.Requests;
            _btnInput.Text = _tab == FriendTab.Blocks ? "차단" : "추가";
            if (_tab == FriendTab.Blocks)
                StyleOutlineButton(_btnInput, danger: true);
            else
                StyleFilledButton(_btnInput);

            int onlineCount = _friends.Values.Count(f => f.Online);
            _lblCount.Text = _client == null ? string.Empty : $"{_friends.Count}/{FriendConstants.MaxFriends}";

            _flow.SuspendLayout();
            List<Control> oldRows = _flow.Controls.Cast<Control>().ToList();
            _flow.Controls.Clear();
            foreach (Control c in oldRows)
                c.Dispose();

            int width = RowWidth();

            if (_client == null)
            {
                _lblStatus.Text = string.Empty;
                AddEmptyLabel("로그인하면 친구 기능을 사용할 수 있습니다.", width);
            }
            else if (_tab == FriendTab.Friends)
            {
                _lblStatus.Text = _friends.Count == 0 ? string.Empty : $"친구 {_friends.Count}명 (접속 중 {onlineCount}명)";

                if (_friends.Count == 0)
                {
                    AddEmptyLabel("아직 친구가 없습니다.\n닉네임으로 친구를 추가해 보세요.", width);
                }
                else
                {
                    // 접속 중인 친구를 위로, 같은 상태끼리는 닉네임 순.
                    foreach (PersonEntry f in _friends.Values
                        .OrderByDescending(x => x.Online)
                        .ThenBy(x => x.Nickname, StringComparer.CurrentCultureIgnoreCase))
                    {
                        _flow.Controls.Add(new FriendRow(this, RowKind.Friend, f, width));
                    }
                }
            }
            else if (_tab == FriendTab.Requests)
            {
                int pageSize = PendingPageSize;
                int totalPages = Math.Max(1, (_pendingTotal + pageSize - 1) / pageSize);
                _lblStatus.Text = _pendingTotal == 0 ? string.Empty : $"받은 요청 {_pendingTotal}건";
                _lblPage.Text = $"{_pendingPage + 1} / {totalPages}";
                _btnPrev.Enabled = _pendingPage > 0;
                _btnNext.Enabled = _pendingPage + 1 < totalPages;

                if (_pending.Count == 0)
                {
                    AddEmptyLabel("받은 친구 요청이 없습니다.", width);
                }
                else
                {
                    foreach (PersonEntry p in _pending)
                    {
                        var row = new FriendRow(this, RowKind.Request, p, width);
                        row.SetBusy(_respondInFlight.Contains(p.Key));
                        _flow.Controls.Add(row);
                    }
                }
            }
            else
            {
                _lblStatus.Text = _blocks.Count == 0 ? string.Empty : $"차단 {_blocks.Count}명";

                if (_blocks.Count == 0)
                {
                    AddEmptyLabel("차단한 사용자가 없습니다.", width);
                }
                else
                {
                    foreach (PersonEntry b in _blocks)
                    {
                        var row = new FriendRow(this, RowKind.Block, b, width);
                        row.SetBusy(_unblockInFlight.Contains(b.Key));
                        _flow.Controls.Add(row);
                    }
                }
            }

            _flow.ResumeLayout();
        }

        private void AddEmptyLabel(string text, int width)
        {
            _flow.Controls.Add(new Label
            {
                Text = text,
                Width = width,
                Height = 64,
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = ChatTheme.Current.TextMuted,
                Margin = new Padding(0, 20, 0, 0),
            });
        }

        private void ShowNotice(string text, FriendNoticeLevel level)
        {
            _lblNotice.Text = text;
            _lblNotice.ForeColor = level == FriendNoticeLevel.Error ? ChatTheme.Current.Danger
                : level == FriendNoticeLevel.Warning ? Color.FromArgb(200, 140, 0)
                : ChatTheme.Current.Accent;
            _noticeTimer.Stop();
            _noticeTimer.Start();

            Notice?.Invoke(text, level);
        }

        #endregion

        #region 사용자 동작

        //***************************************************************************
        // @brief 입력 줄의 확인 동작 — 친구 탭이면 친구 요청, 차단 탭이면 차단.
        //***************************************************************************
        private void SubmitInput()
        {
            if (_client == null || _inputInFlight || _menuBlockInFlight)
                return;

            string nickname = _txtInput.Text.Trim();
            if (nickname.Length == 0)
                return;

            _inputInFlight = true;
            _inputInFlightNickname = nickname;
            _btnInput.Enabled = false;

            if (_tab == FriendTab.Blocks)
                _client.RequestBlock(nickname);
            else
                _client.RequestFriend(nickname);
        }

        private void EndInputRequest(bool clearText)
        {
            _inputInFlight = false;
            _btnInput.Enabled = _client != null;
            if (clearText)
                _txtInput.Text = string.Empty;
        }

        private void AcceptRequest(PersonEntry p)
        {
            RespondRequest(p, true);
        }

        private void RejectRequest(PersonEntry p)
        {
            RespondRequest(p, false);
        }

        private void RespondRequest(PersonEntry p, bool accept)
        {
            if (_client == null || p == null || !_respondInFlight.Add(p.Key))
                return;

            SetRowBusy(p, true); // 클릭 핸들러 안에서 행을 없애지 않도록 버튼만 잠근다
            _client.RespondFriendRequest(p.PublicId, accept);
        }

        private void ConfirmRemoveFriend(PersonEntry f)
        {
            if (_client == null || f == null)
                return;

            if (MessageBox.Show(FindForm(), $"'{f.Nickname}'님을 친구 목록에서 삭제할까요?", "친구 삭제",
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;

            _client.RequestFriendRemove(f.PublicId);
        }

        private void ConfirmBlock(PersonEntry f)
        {
            if (_client == null || f == null)
                return;

            if (MessageBox.Show(FindForm(),
                $"'{f.Nickname}'님을 차단할까요?\n\n친구 관계와 받은 요청이 삭제되고, 이 사용자의 채팅 메시지가 표시되지 않습니다.",
                "차단", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
                return;

            if (_inputInFlight || _menuBlockInFlight)
            {
                ShowNotice("진행 중인 요청이 끝난 뒤 다시 시도하세요.", FriendNoticeLevel.Warning);
                return;
            }

            _menuBlockInFlight = true;
            _menuBlockNickname = f.Nickname;
            _client.RequestBlock(f.Nickname);
        }

        private void Unblock(PersonEntry b)
        {
            if (_client == null || b == null || !_unblockInFlight.Add(b.Key))
                return;

            SetRowBusy(b, true);
            _client.RequestUnblock(b.PublicId);
        }

        private void SetRowBusy(PersonEntry entry, bool busy)
        {
            foreach (FriendRow row in _flow.Controls.OfType<FriendRow>())
            {
                if (ReferenceEquals(row.Entry, entry))
                    row.SetBusy(busy);
            }
        }

        private void ShowFriendMenu(PersonEntry f, Control row, Point location)
        {
            _menuEntry = f;
            _friendMenu.Show(row, location);
        }

        #endregion

        #region 서버 이벤트 (수신 스레드 -> UI 스레드)

        //***************************************************************************
        // @brief 수신 스레드에서 호출되는 이벤트를 UI 스레드로 넘긴다. 창이 이미
        //        닫혔거나 핸들이 없으면 조용히 버린다(예외가 수신 루프까지 올라가
        //        연결이 끊기는 것을 막기 위함).
        //***************************************************************************
        private void Post(Action action)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            try
            {
                BeginInvoke((MethodInvoker)delegate
                {
                    if (!IsDisposed)
                        action();
                });
            }
            catch (ObjectDisposedException)
            {
            }
            catch (InvalidOperationException)
            {
            }
        }

        private static string Describe(FriendResult reason)
        {
            switch (reason)
            {
                case FriendResult.DbError: return "서버 오류가 발생했습니다. 잠시 후 다시 시도하세요.";
                case FriendResult.UserNotFound: return "해당 닉네임의 사용자를 찾을 수 없습니다.";
                case FriendResult.SelfTarget: return "자기 자신은 대상으로 지정할 수 없습니다.";
                case FriendResult.AlreadyFriends: return "이미 친구입니다.";
                case FriendResult.AlreadyRequested: return "이미 친구 요청을 보냈습니다.";
                case FriendResult.NoPendingRequest: return "처리할 친구 요청이 없습니다.";
                case FriendResult.NotFriends: return "친구가 아닙니다.";
                case FriendResult.BlockedByMe: return "차단한 사용자입니다. 먼저 차단을 해제하세요.";
                case FriendResult.FriendLimit: return $"친구 수 상한({FriendConstants.MaxFriends}명)에 도달해 처리할 수 없습니다(본인 또는 상대).";
                case FriendResult.AlreadyBlocked: return "이미 차단한 사용자입니다.";
                case FriendResult.NotBlocked: return "차단 목록에 없는 사용자입니다.";
                default: return "알 수 없는 오류(" + (int)reason + ")";
            }
        }

        private string NicknameOf(string key)
        {
            if (_friends.TryGetValue(key, out PersonEntry f))
                return f.Nickname;

            PersonEntry p = _pending.FirstOrDefault(x => x.Key == key);
            if (p != null)
                return p.Nickname;

            PersonEntry b = _blocks.FirstOrDefault(x => x.Key == key);
            return b != null ? b.Nickname : "상대";
        }

        private void OnFriendRequestResult(FriendRequestResData data)
        {
            Post(() =>
            {
                string nickname = _inputInFlightNickname ?? "상대";
                EndInputRequest(clearText: data.Success);

                if (!data.Success)
                {
                    ShowNotice($"친구 요청 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                    return;
                }

                if (data.AutoAccepted)
                {
                    ShowNotice($"'{nickname}'님과 친구가 되었습니다. (서로 요청이 맞물렸습니다)", FriendNoticeLevel.Info);
                    _client?.RequestFriendList();
                    RequestPendingPage(_pendingPage);
                }
                else
                {
                    ShowNotice($"'{nickname}'님에게 친구 요청을 보냈습니다.", FriendNoticeLevel.Info);
                }
            });
        }

        private void OnFriendRespondResult(FriendRespondResData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                _respondInFlight.Remove(key);

                PersonEntry entry = _pending.FirstOrDefault(x => x.Key == key);
                string nickname = entry != null ? entry.Nickname : "상대";

                // 요청이 이미 사라졌다면(상대가 취소했거나 차단 등) 목록에서도 치운다.
                bool gone = data.Success || data.Reason == FriendResult.NoPendingRequest;
                if (gone && entry != null)
                {
                    _pending.Remove(entry);
                    _pendingTotal = Math.Max(0, _pendingTotal - 1);
                }

                if (data.Success)
                {
                    if (data.Accepted)
                    {
                        _friends[key] = new PersonEntry
                        {
                            PublicId = data.PublicId,
                            Key = key,
                            Nickname = nickname,
                            ProfileImageUrl = entry != null ? entry.ProfileImageUrl : string.Empty,
                            Online = data.Online,
                        };
                        ShowNotice($"'{nickname}'님과 친구가 되었습니다.", FriendNoticeLevel.Info);
                    }
                    else
                    {
                        ShowNotice($"'{nickname}'님의 친구 요청을 거절했습니다.", FriendNoticeLevel.Info);
                    }
                }
                else
                {
                    ShowNotice($"요청 처리 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                }

                // 현재 페이지가 비었는데 뒤에 더 있으면 서버에서 보정된 페이지를 다시 받는다.
                if (_pending.Count == 0 && _pendingTotal > 0)
                    RequestPendingPage(Math.Min(_pendingPage, (_pendingTotal - 1) / PendingPageSize));

                Rebuild();
            });
        }

        private void OnFriendRemoveResult(FriendTargetResData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                string nickname = NicknameOf(key);

                if (data.Success || data.Reason == FriendResult.NotFriends)
                {
                    _friends.Remove(key);
                    if (data.Success)
                        ShowNotice($"'{nickname}'님을 친구 목록에서 삭제했습니다.", FriendNoticeLevel.Info);
                    else
                        ShowNotice($"삭제 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                    Rebuild();
                }
                else
                {
                    ShowNotice($"삭제 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                }
            });
        }

        private void OnFriendListItem(FriendItemData data)
        {
            Post(() =>
            {
                _incomingFriends.Add(new PersonEntry
                {
                    PublicId = data.PublicId,
                    Key = Key(data.PublicId),
                    Nickname = data.Nickname,
                    ProfileImageUrl = data.ProfileImageUrl,
                    Online = data.Online,
                });
            });
        }

        private void OnFriendListEnd(FriendListEndData data)
        {
            Post(() =>
            {
                List<PersonEntry> incoming = _incomingFriends;
                _incomingFriends = new List<PersonEntry>();

                if (!data.Success)
                {
                    ShowNotice($"친구 목록을 불러오지 못했습니다 - {Describe(data.Reason)}", FriendNoticeLevel.Error);
                    return;
                }

                _friends.Clear();
                foreach (PersonEntry f in incoming)
                    _friends[f.Key] = f;

                Rebuild();
            });
        }

        private void OnFriendPendingItem(FriendPendingItemData data)
        {
            Post(() =>
            {
                if (data.RequestId != _pendingRequestId)
                    return; // 더 새로운 요청이 나갔으므로 오래된 응답은 버린다

                _incomingPending.Add(new PersonEntry
                {
                    PublicId = data.PublicId,
                    Key = Key(data.PublicId),
                    Nickname = data.Nickname,
                    ProfileImageUrl = data.ProfileImageUrl,
                });
            });
        }

        private void OnFriendPendingEnd(FriendPendingEndData data)
        {
            Post(() =>
            {
                if (data.RequestId != _pendingRequestId)
                    return;

                List<PersonEntry> incoming = _incomingPending;
                _incomingPending = new List<PersonEntry>();

                if (!data.Success)
                {
                    ShowNotice($"받은 요청을 불러오지 못했습니다 - {Describe(data.Reason)}", FriendNoticeLevel.Error);
                    return;
                }

                _pending = incoming;
                _pendingTotal = data.TotalCount;
                _pendingPage = data.Page; // 서버가 범위를 넘은 페이지를 보정한 값
                Rebuild();
            });
        }

        private void OnBlockResult(FriendTargetResData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                string nickname;
                if (_menuBlockInFlight)
                {
                    nickname = _menuBlockNickname;
                    _menuBlockInFlight = false;
                    _menuBlockNickname = null;
                }
                else
                {
                    nickname = _inputInFlightNickname;
                    if (_inputInFlight)
                        EndInputRequest(clearText: data.Success || data.Reason == FriendResult.AlreadyBlocked);
                }

                if (string.IsNullOrEmpty(nickname))
                    nickname = NicknameOf(key);

                if (data.Success || data.Reason == FriendResult.AlreadyBlocked)
                {
                    ShowNotice(data.Success ? $"'{nickname}'님을 차단했습니다." : $"'{nickname}'님은 이미 차단되어 있습니다.", FriendNoticeLevel.Info);

                    // 서버가 친구 관계와 받은 요청을 함께 지우므로 세 목록을 모두 다시 받는다.
                    _friends.Remove(key);
                    ReloadAll();
                }
                else
                {
                    ShowNotice($"차단 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                }
            });
        }

        private void OnUnblockResult(FriendTargetResData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                _unblockInFlight.Remove(key);
                string nickname = NicknameOf(key);

                if (data.Success || data.Reason == FriendResult.NotBlocked)
                {
                    _blocks.RemoveAll(x => x.Key == key);
                    if (data.Success)
                        ShowNotice($"'{nickname}'님의 차단을 해제했습니다.", FriendNoticeLevel.Info);
                }
                else
                {
                    ShowNotice($"차단 해제 실패 - {Describe(data.Reason)}", FriendNoticeLevel.Warning);
                }

                Rebuild();
            });
        }

        private void OnBlockListItem(BlockItemData data)
        {
            Post(() =>
            {
                _incomingBlocks.Add(new PersonEntry
                {
                    PublicId = data.PublicId,
                    Key = Key(data.PublicId),
                    Nickname = data.Nickname,
                });
            });
        }

        private void OnBlockListEnd(FriendListEndData data)
        {
            Post(() =>
            {
                List<PersonEntry> incoming = _incomingBlocks;
                _incomingBlocks = new List<PersonEntry>();

                if (!data.Success)
                {
                    ShowNotice($"차단 목록을 불러오지 못했습니다 - {Describe(data.Reason)}", FriendNoticeLevel.Error);
                    return;
                }

                _blocks = incoming.OrderBy(x => x.Nickname, StringComparer.CurrentCultureIgnoreCase).ToList();
                Rebuild();
            });
        }

        private void OnFriendRequestNotify(FriendRequestNotifyData data)
        {
            Post(() =>
            {
                ShowNotice($"'{data.Nickname}'님이 친구 요청을 보냈습니다.", FriendNoticeLevel.Info);
                RequestPendingPage(_pendingPage); // 개수와 목록을 서버 기준으로 다시 맞춘다
            });
        }

        private void OnFriendAcceptedNotify(FriendAcceptedNotifyData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                _friends[key] = new PersonEntry
                {
                    PublicId = data.PublicId,
                    Key = key,
                    Nickname = data.Nickname,
                    ProfileImageUrl = data.ProfileImageUrl,
                    Online = data.Online,
                };
                ShowNotice($"'{data.Nickname}'님과 친구가 되었습니다.", FriendNoticeLevel.Info);

                // 요청이 맞물려 자동 수락된 경우 받은 요청 목록에도 남아 있을 수 있다.
                if (_pending.Any(x => x.Key == key))
                    RequestPendingPage(_pendingPage);

                Rebuild();
            });
        }

        private void OnFriendRemovedNotify(FriendRemovedNotifyData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);
                if (!_friends.TryGetValue(key, out PersonEntry f))
                    return;

                _friends.Remove(key);
                ShowNotice($"'{f.Nickname}'님이 친구 목록에서 제외되었습니다.", FriendNoticeLevel.Warning);
                Rebuild();
            });
        }

        private void OnFriendProfileChangedNotify(FriendProfileChangedNotifyData data)
        {
            Post(() =>
            {
                string key = Key(data.PublicId);

                if (_friends.TryGetValue(key, out PersonEntry f))
                {
                    if (f.Nickname != data.Nickname)
                        ShowNotice($"'{f.Nickname}'님이 닉네임을 '{data.Nickname}'(으)로 바꿨습니다.", FriendNoticeLevel.Info);

                    f.Nickname = data.Nickname;
                    f.ProfileImageUrl = data.ProfileImageUrl;
                }

                foreach (PersonEntry p in _pending.Where(x => x.Key == key))
                {
                    p.Nickname = data.Nickname;
                    p.ProfileImageUrl = data.ProfileImageUrl;
                }

                Rebuild();
            });
        }

        private void OnFriendStatusNotify(FriendStatusNotifyData data)
        {
            Post(() =>
            {
                if (!_friends.TryGetValue(Key(data.PublicId), out PersonEntry f))
                    return;

                if (f.Online == data.Online)
                    return;

                f.Online = data.Online;
                ShowNotice(data.Online ? $"'{f.Nickname}'님이 접속했습니다." : $"'{f.Nickname}'님이 접속을 종료했습니다.", FriendNoticeLevel.Info);

                // 정렬 순서(접속 중 우선)가 바뀌므로 친구 탭이면 다시 만든다.
                if (_tab == FriendTab.Friends)
                    Rebuild();
            });
        }

        #endregion

        #region 썸네일

        //***************************************************************************
        // @brief RoomMemberListPanel.GetThumbnail()과 같은 패턴 — 캐시에 있으면
        //        즉시 반환, 없으면 비동기로 받아오고 그 동안은 null(기본 아바타).
        //***************************************************************************
        private Image GetThumbnail(string url, FriendRow row)
        {
            if (string.IsNullOrEmpty(url))
                return null;

            bool startFetch = false;

            lock (_thumbnailCache)
            {
                if (_thumbnailCache.TryGetValue(url, out Image cached))
                    return cached;

                if (_thumbnailPendingRows.TryGetValue(url, out List<FriendRow> waiters))
                {
                    if (!waiters.Contains(row))
                        waiters.Add(row);
                }
                else
                {
                    _thumbnailPendingRows[url] = new List<FriendRow> { row };
                    startFetch = true;
                }
            }

            if (startFetch)
                _ = FetchThumbnailAsync(url);

            return null;
        }

        private async Task FetchThumbnailAsync(string url)
        {
            try
            {
                byte[] imageBytes = await _thumbnailHttpClient.GetByteArrayAsync(url);
                if (imageBytes == null || imageBytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(imageBytes))
                using (var original = Image.FromStream(ms))
                {
                    lock (_thumbnailCache)
                    {
                        _thumbnailCache[url] = new Bitmap(original);
                    }
                }
            }
            catch
            {
                // 네트워크 오류, 잘못된 URL, 디코딩 실패 등 — 그 사람은 계속 기본 아바타로 보인다.
            }
            finally
            {
                List<FriendRow> waiters;
                lock (_thumbnailCache)
                {
                    _thumbnailPendingRows.TryGetValue(url, out waiters);
                    _thumbnailPendingRows.Remove(url);
                }

                if (waiters != null && !IsDisposed && IsHandleCreated)
                {
                    try
                    {
                        BeginInvoke((MethodInvoker)delegate
                        {
                            foreach (FriendRow row in waiters)
                            {
                                if (!row.IsDisposed)
                                    row.Invalidate();
                            }
                        });
                    }
                    catch (InvalidOperationException)
                    {
                    }
                }
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _noticeTimer?.Dispose();
                _friendMenu?.Dispose();

                lock (_thumbnailCache)
                {
                    foreach (Image img in _thumbnailCache.Values)
                        img.Dispose();
                    _thumbnailCache.Clear();
                }
            }
            base.Dispose(disposing);
        }

        #endregion

        //***************************************************************************
        // @brief 한 사람(아바타 + 닉네임 + 상태 줄)을 그리는 행. 친구 행은 오른쪽
        //        클릭 메뉴를, 요청 행은 [수락][거절]을, 차단 행은 [해제] 버튼을 가진다.
        //***************************************************************************
        private class FriendRow : Panel
        {
            private const int ButtonWidth = 44;

            private readonly FriendPanel _owner;
            private readonly RowKind _kind;
            private readonly PersonEntry _entry;
            private readonly Button _btnPrimary;   // 수락 / 해제
            private readonly Button _btnSecondary; // 거절

            public FriendRow(FriendPanel owner, RowKind kind, PersonEntry entry, int width)
            {
                _owner = owner;
                _kind = kind;
                _entry = entry;

                DoubleBuffered = true;
                Size = new Size(width, RowHeight);
                Margin = new Padding(0, 0, 0, 2);

                if (kind == RowKind.Request)
                {
                    _btnPrimary = new Button { Text = "수락", Width = ButtonWidth, Height = 24 };
                    _btnSecondary = new Button { Text = "거절", Width = ButtonWidth, Height = 24 };
                    StyleFilledButton(_btnPrimary);
                    StyleOutlineButton(_btnSecondary, danger: true);
                    _btnPrimary.Click += (s, e) => _owner.AcceptRequest(_entry);
                    _btnSecondary.Click += (s, e) => _owner.RejectRequest(_entry);
                    Controls.Add(_btnPrimary);
                    Controls.Add(_btnSecondary);
                }
                else if (kind == RowKind.Block)
                {
                    _btnPrimary = new Button { Text = "해제", Width = ButtonWidth, Height = 24 };
                    StyleOutlineButton(_btnPrimary);
                    _btnPrimary.Click += (s, e) => _owner.Unblock(_entry);
                    Controls.Add(_btnPrimary);
                }

                if (kind == RowKind.Friend)
                {
                    Cursor = Cursors.Default;
                    MouseUp += (s, e) =>
                    {
                        if (e.Button == MouseButtons.Right)
                            _owner.ShowFriendMenu(_entry, this, e.Location);
                    };
                }

                LayoutButtons();
            }

            public PersonEntry Entry => _entry;

            public void SetBusy(bool busy)
            {
                if (_btnPrimary != null) _btnPrimary.Enabled = !busy;
                if (_btnSecondary != null) _btnSecondary.Enabled = !busy;
            }

            protected override void OnResize(EventArgs eventargs)
            {
                base.OnResize(eventargs);
                LayoutButtons();
            }

            private void LayoutButtons()
            {
                // 생성자에서 Size를 지정할 때 OnResize가 먼저 불리므로, 버튼이 아직 없을 수 있다.
                if (_btnPrimary == null)
                    return;

                int top = (Height - 24) / 2;
                if (_kind == RowKind.Request)
                {
                    _btnSecondary.Location = new Point(Width - ButtonWidth - 6, top);
                    _btnPrimary.Location = new Point(Width - ButtonWidth * 2 - 10, top);
                }
                else if (_kind == RowKind.Block)
                {
                    _btnPrimary.Location = new Point(Width - ButtonWidth - 6, top);
                }
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaint(e);
                var g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;

                const int avatarSize = 36;
                var avatarRect = new Rectangle(6, (Height - avatarSize) / 2, avatarSize, avatarSize);
                Image thumbnail = _kind == RowKind.Block || string.IsNullOrEmpty(_entry.ProfileImageUrl)
                    ? null
                    : _owner.GetThumbnail(_entry.ProfileImageUrl, this);

                if (thumbnail != null)
                {
                    using (var clipPath = new GraphicsPath())
                    {
                        clipPath.AddEllipse(avatarRect);
                        Region previousClip = g.Clip;
                        g.SetClip(clipPath, CombineMode.Intersect);
                        g.DrawImage(thumbnail, avatarRect);
                        g.Clip = previousClip;
                    }
                }
                else
                {
                    string name = string.IsNullOrEmpty(_entry.Nickname) ? "?" : _entry.Nickname;
                    Color color = AvatarPalette[(uint)name.GetHashCode() % (uint)AvatarPalette.Length];
                    using (var brush = new SolidBrush(color))
                        g.FillEllipse(brush, avatarRect);

                    string initial = name.Substring(0, 1).ToUpperInvariant();
                    using (var initialFont = new Font(Font.FontFamily, 12f, FontStyle.Bold))
                    using (var initialBrush = new SolidBrush(Color.White))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(initial, initialFont, initialBrush, avatarRect, fmt);
                }

                bool showOnline = _kind == RowKind.Friend;
                if (showOnline)
                {
                    // 아바타 오른쪽 아래의 접속 표시 점(흰 테두리로 아바타와 분리)
                    const int dot = 11;
                    var dotRect = new Rectangle(avatarRect.Right - dot + 2, avatarRect.Bottom - dot + 2, dot, dot);
                    using (var ring = new SolidBrush(Color.White))
                        g.FillEllipse(ring, Rectangle.Inflate(dotRect, 2, 2));
                    using (var fill = new SolidBrush(_entry.Online ? ChatTheme.Current.Success : ChatTheme.Current.TextMuted))
                        g.FillEllipse(fill, dotRect);
                }

                int textLeft = avatarRect.Right + 10;
                int buttonsWidth = _kind == RowKind.Request ? ButtonWidth * 2 + 16 : _kind == RowKind.Block ? ButtonWidth + 12 : 4;
                var textRect = new RectangleF(textLeft, 0, Math.Max(20, Width - textLeft - buttonsWidth), Height);

                string subtitle;
                Color subtitleColor;
                if (_kind == RowKind.Friend)
                {
                    subtitle = _entry.Online ? "접속 중" : "오프라인";
                    subtitleColor = _entry.Online ? ChatTheme.Current.Success : ChatTheme.Current.TextMuted;
                }
                else if (_kind == RowKind.Request)
                {
                    subtitle = "친구 요청";
                    subtitleColor = ChatTheme.Current.TextSecondary;
                }
                else
                {
                    subtitle = "차단됨";
                    subtitleColor = ChatTheme.Current.Danger;
                }

                bool dim = _kind == RowKind.Friend && !_entry.Online;
                using (var nameFont = new Font(Font.FontFamily, 10f, _kind == RowKind.Friend && _entry.Online ? FontStyle.Bold : FontStyle.Regular))
                using (var nameBrush = new SolidBrush(dim ? ChatTheme.Current.TextSecondary : Color.Black))
                using (var fmt = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
                    g.DrawString(_entry.Nickname ?? string.Empty, nameFont, nameBrush, new RectangleF(textRect.X, 8, textRect.Width, 20), fmt);

                using (var subFont = new Font(Font.FontFamily, 8f))
                using (var subBrush = new SolidBrush(subtitleColor))
                    g.DrawString(subtitle, subFont, subBrush, new PointF(textRect.X, 28));
            }
        }
    }
}