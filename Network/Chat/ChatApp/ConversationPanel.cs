
//***************************************************************************
// ConversationPanel.cs : "메신저" 탭 — 카카오톡식 1:1/그룹 대화.
//
// 왼쪽에 내 대화방 목록(상대/그룹 이름, 마지막 메시지, 시각, 안 읽은 수), 오른쪽에
// 선택한 대화방의 말풍선 대화창과 입력창이 있다. 한 사용자가 여러 대화방에 동시에
// 속하며, 서버는 접속 중인 모든 멤버에게 새 메시지를 푸시한다. 모든 메시지는 서버 DB에
// 저장되므로 오프라인 동안 온 메시지는 다음 접속 때 목록의 안 읽은 수와 기록 조회로 받는다.
//
// 말풍선 옆의 노란 숫자는 "그 메시지를 아직 읽지 않은 사람 수"다. 대화방의 멤버 목록이
// 담은 멤버별 읽음 위치(lastReadMsgId)와 ConvReadNotify로 계산한다.
//
// 메시지는 텍스트/사진을 보낼 수 있고, 말풍선을 우클릭해 답장·복사·삭제(내 메시지)를 할 수 있다.
// 삭제는 모두에게서 지워지며 "삭제된 메시지입니다."로 남는다. 사진은 호스트 폼의 ImageUploader로
// 파일 서버에 올린 뒤 그 URL을 이미지 메시지로 보내고, 말풍선을 누르면 원본 크기로 본다.
//
// 서버와의 통신은 ChatNetworkClient의 Conv* 이벤트/요청 메서드를 그대로 쓰며,
// AttachClient()/DetachClient()로 로그인/연결 종료 시점에 구독을 시작/해제한다
// (FriendPanel/RoomListPanel과 같은 패턴). 친구 목록은 대화 상대를 고르는 용도로,
// 친구 관련 이벤트를 구독해 이 패널이 따로 캐시한다.
//
// [스레드] ChatNetworkClient의 이벤트는 수신 스레드에서 호출되므로 모든 핸들러가
// Post()로 UI 스레드에 넘긴 뒤에만 컨트롤을 건드린다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Font = System.Drawing.Font;
using Image = System.Drawing.Image;

namespace ChatApp
{
    public class ConversationPanel : UserControl
    {
        private const int ListRowHeight = 64;
        private const int AvatarSize = 40;
        private const int BubbleAvatarSize = 34;
        private const int HistoryPageSize = ConvConstants.MaxHistoryPage;
        private const int MaxInputChars = 500;

        private static readonly Color[] AvatarPalette =
        {
            Color.FromArgb(255, 107, 107), Color.FromArgb(78, 205, 196), Color.FromArgb(69, 183, 209),
            Color.FromArgb(150, 206, 180), Color.FromArgb(255, 195, 113), Color.FromArgb(162, 155, 254),
            Color.FromArgb(253, 121, 168), Color.FromArgb(129, 236, 236),
        };

        private const string DeletedText = "삭제된 메시지입니다.";
        private const int ImageMaxSide = 240;           // 사진 말풍선 표시 최대 변
        private const int ImageCacheSide = 360;         // 캐시에 보관하는 사진의 최대 변(원본보다 작게 줄여 메모리 절약)
        private const long MaxImageBytes = 10L * 1024 * 1024;

        private static readonly Color UnreadReaderColor = Color.FromArgb(240, 180, 0);
        private static readonly Color BadgeColor = Color.FromArgb(235, 68, 90);

        //***************************************************************************
        // @brief 대화방 하나의 클라이언트 상태. 서버 요약 + 불러온 메시지/멤버.
        //***************************************************************************
        private class ConvEntry
        {
            public long ConvId;
            public ConvType Type;
            public string Name = string.Empty;
            public int MemberCount;
            public int UnreadCount;
            public long LastMsgId;
            public long LastMsgTime;
            public ConvMessageKind LastKind;
            public string LastMessage = string.Empty;
            public long MyLastReadMsgId;
            public byte[] PeerPublicId;
            public string PeerNickname = string.Empty;
            public string PeerImageUrl = string.Empty;
            public long SortTime;

            public List<ConvMessageData> Messages = new List<ConvMessageData>();   // MsgId 오름차순
            public List<ConvMessageData> IncomingHistory = new List<ConvMessageData>();
            public bool HistoryLoaded;
            public bool HistoryLoading;
            public bool HasMore;
            public int HistoryRequestId;
            public bool HistoryIsInitial;

            public Dictionary<string, ConvMemberData> Members = new Dictionary<string, ConvMemberData>();
            public List<ConvMemberData> IncomingMembers = new List<ConvMemberData>();
            public bool MembersLoaded;

            public bool IsGroup => Type == ConvType.Group;
        }

        private enum RowKind
        {
            More,
            Date,
            System,
            Text,
        }

        //***************************************************************************
        // @brief 대화창 한 줄. 메시지 목록을 그리기 좋은 형태로 풀어낸 것(날짜 구분선 포함).
        //***************************************************************************
        private class MsgRow
        {
            public RowKind Kind;
            public ConvMessageData Msg;
            public string Text;
            public bool Mine;
            public bool ShowSender;     // 연속된 같은 사람의 메시지 중 첫 줄에만 아바타/이름을 그린다
            public Rectangle BubbleBounds;  // 마지막으로 그린 말풍선 영역(클릭 판정용)
        }

        private class FriendInfo
        {
            public byte[] PublicId;
            public string Nickname;
            public string ProfileImageUrl;
            public bool Online;
        }

        private class BufferedListBox : ListBox
        {
            public BufferedListBox()
            {
                DoubleBuffered = true;
            }
        }

        private static readonly HttpClient _thumbnailHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        private static readonly Dictionary<string, Image> _thumbnailCache = new Dictionary<string, Image>();
        private static readonly HashSet<string> _thumbnailPending = new HashSet<string>();
        private static readonly Dictionary<string, Image> _imageCache = new Dictionary<string, Image>();
        private static readonly HashSet<string> _imagePending = new HashSet<string>();
        private static readonly HashSet<string> _imageFailed = new HashSet<string>();

        private ChatNetworkClient _client;
        private byte[] _myPublicId;
        private string _myHex = string.Empty;

        private readonly Dictionary<long, ConvEntry> _convs = new Dictionary<long, ConvEntry>();
        private List<ConvEntry> _incomingConvs = new List<ConvEntry>();
        private ConvEntry _selected;
        private bool _suppressSelect;
        private bool _listRefreshPending;

        private readonly Dictionary<string, FriendInfo> _friends = new Dictionary<string, FriendInfo>();
        private List<FriendInfo> _incomingFriends = new List<FriendInfo>();

        private List<MsgRow> _rows = new List<MsgRow>();

        private Panel _left;
        private Panel _leftHeader;
        private Label _lblListTitle;
        private Button _btnNew;
        private BufferedListBox _listConvs;
        private ContextMenuStrip _convMenu;

        private Panel _right;
        private Panel _rightHeader;
        private Label _lblTitle;
        private Label _lblSub;
        private Button _btnMembers;
        private Button _btnInvite;
        private Button _btnRename;
        private Button _btnLeave;
        private BufferedListBox _listMsgs;
        private Panel _inputPanel;
        private TextBox _txtInput;
        private Button _btnSend;
        private Label _lblEmpty;
        private Label _lblNotice;
        private Timer _noticeTimer;
        private Button _btnImage;
        private Panel _replyBar;
        private Label _lblReply;
        private Button _btnReplyCancel;
        private ContextMenuStrip _msgMenu;
        private ToolStripMenuItem _miReply;
        private ToolStripMenuItem _miCopy;
        private ToolStripMenuItem _miDelete;
        private MsgRow _menuRow;
        private ConvMessageData _replyTarget;       // 답장 중인 메시지(없으면 null)

        // 창이 비활성일 때 새 메시지가 오면 호스트가 데스크톱 알림(작업 표시줄 깜박임/풍선/알림음)을 띄우도록 알린다.
        // 인자: convId, 제목, 본문, 알림음 여부.
        public event Action<long, string, string, bool> MessageNotifyRequested;

        private MessengerSettings _settings = new MessengerSettings();
        private ToolStripMenuItem _miMuteRoom;
        private Button _btnNotify;
        private ContextMenuStrip _notifyMenu;
        private ToolStripMenuItem _miNotifyOn;
        private ToolStripMenuItem _miNotifyPreview;
        private ToolStripMenuItem _miNotifySound;

        // 사진 파일을 파일 서버에 올리고 URL을 돌려준다(실패하면 null). 업로드 토큰/파일 서버 호출은 호스트 폼이 갖고 있다.
        public Func<string, Task<string>> ImageUploader;

        // 서버 이벤트로 알리는 마지막 상태 — 호스트(폼)가 탭 제목의 안 읽은 수 배지와 시스템 로그에 쓴다.
        public event Action<int> TotalUnreadChanged;
        public event Action<string, FriendNoticeLevel> Notice;
        // 새 대화방이 열려 이 패널로 포커스가 와야 할 때(친구 패널의 "대화하기" 등).
        public event Action ActivationRequested;

        public byte[] MyPublicId
        {
            get { return _myPublicId; }
            set
            {
                _myPublicId = value;
                _myHex = value == null ? string.Empty : ToHex(value);
            }
        }

        public ConversationPanel()
        {
            InitializeComponents();
            ApplyConnectedState();
        }

        #region 연결 / 해제

        //***************************************************************************
        // @brief 로그인 성공 시점에 호출 — 이벤트 구독을 시작하고 대화방 목록과 친구 목록을 불러온다.
        //***************************************************************************
        public void AttachClient(ChatNetworkClient client)
        {
            DetachClient();

            _client = client;
            _client.ConvOpenResultReceived += OnConvOpenResult;
            _client.ConvInviteResultReceived += OnConvInviteResult;
            _client.ConvLeaveResultReceived += OnConvLeaveResult;
            _client.ConvListItemReceived += OnConvListItem;
            _client.ConvListEndReceived += OnConvListEnd;
            _client.ConvSendResultReceived += OnConvSendResult;
            _client.ConvMessageReceived += OnConvMessage;
            _client.ConvHistoryItemReceived += OnConvHistoryItem;
            _client.ConvHistoryEndReceived += OnConvHistoryEnd;
            _client.ConvReadNotified += OnConvReadNotify;
            _client.ConvMemberItemReceived += OnConvMemberItem;
            _client.ConvMembersEndReceived += OnConvMembersEnd;
            _client.ConvUpdatedNotified += OnConvUpdatedNotify;
            _client.ConvDeleteResultReceived += OnConvDeleteResult;
            _client.ConvDeletedNotified += OnConvDeletedNotify;
            _client.ConvRenameResultReceived += OnConvRenameResult;
            _client.ConvRenamedNotified += OnConvRenamedNotify;

            _client.FriendListItemReceived += OnFriendListItem;
            _client.FriendListEndReceived += OnFriendListEnd;
            _client.FriendAcceptedNotified += OnFriendAccepted;
            _client.FriendRemovedNotified += OnFriendRemoved;
            _client.FriendProfileChangedNotified += OnFriendProfileChanged;
            _client.FriendStatusNotified += OnFriendStatus;

            _settings = MessengerSettings.Load(_myHex);
            SyncNotifyMenu();

            ApplyConnectedState();
            _client.RequestFriendList();
            _client.RequestConvList();
        }

        //***************************************************************************
        // @brief 연결이 끊겼을 때 호출 — 구독을 해제하고 모든 상태를 비운다.
        //***************************************************************************
        public void DetachClient()
        {
            if (_client != null)
            {
                _client.ConvOpenResultReceived -= OnConvOpenResult;
                _client.ConvInviteResultReceived -= OnConvInviteResult;
                _client.ConvLeaveResultReceived -= OnConvLeaveResult;
                _client.ConvListItemReceived -= OnConvListItem;
                _client.ConvListEndReceived -= OnConvListEnd;
                _client.ConvSendResultReceived -= OnConvSendResult;
                _client.ConvMessageReceived -= OnConvMessage;
                _client.ConvHistoryItemReceived -= OnConvHistoryItem;
                _client.ConvHistoryEndReceived -= OnConvHistoryEnd;
                _client.ConvReadNotified -= OnConvReadNotify;
                _client.ConvMemberItemReceived -= OnConvMemberItem;
                _client.ConvMembersEndReceived -= OnConvMembersEnd;
                _client.ConvUpdatedNotified -= OnConvUpdatedNotify;
                _client.ConvDeleteResultReceived -= OnConvDeleteResult;
                _client.ConvDeletedNotified -= OnConvDeletedNotify;
                _client.ConvRenameResultReceived -= OnConvRenameResult;
                _client.ConvRenamedNotified -= OnConvRenamedNotify;

                _client.FriendListItemReceived -= OnFriendListItem;
                _client.FriendListEndReceived -= OnFriendListEnd;
                _client.FriendAcceptedNotified -= OnFriendAccepted;
                _client.FriendRemovedNotified -= OnFriendRemoved;
                _client.FriendProfileChangedNotified -= OnFriendProfileChanged;
                _client.FriendStatusNotified -= OnFriendStatus;
                _client = null;
            }

            _convs.Clear();
            _incomingConvs = new List<ConvEntry>();
            _friends.Clear();
            _incomingFriends = new List<FriendInfo>();
            _selected = null;
            _rows = new List<MsgRow>();
            _listRefreshPending = false;
            CancelReply();
            if (_txtInput != null)
                _txtInput.Text = string.Empty;

            ApplyConnectedState();
            RefreshConvList();
            ShowSelected();
            RaiseTotalUnread();
        }

        //***************************************************************************
        // @brief 스킨이 바뀔 때 호출 — 생성 시점에 굳어진 색을 다시 입힌다.
        //***************************************************************************
        public void RefreshTheme()
        {
            _lblListTitle.ForeColor = ChatTheme.Current.Accent;
            StyleFilledButton(_btnNew);
            StyleFilledButton(_btnSend);
            StyleOutlineButton(_btnImage);
            StyleOutlineButton(_btnNotify);
            StyleOutlineButton(_btnMembers);
            StyleOutlineButton(_btnInvite);
            StyleOutlineButton(_btnRename);
            StyleOutlineButton(_btnLeave, danger: true);
            _lblNotice.ForeColor = ChatTheme.Current.Accent;
            Invalidate(true);
        }

        private void ApplyConnectedState()
        {
            bool connected = _client != null;
            _btnNew.Enabled = connected;
            if (_btnImage != null)
                _btnImage.Enabled = connected;
        }

        #endregion

        #region 공개 동작

        //***************************************************************************
        // @brief 친구와의 1:1 대화방을 연다(없으면 서버가 만든다). 응답이 오면 그 방이 선택된다.
        //***************************************************************************
        public void OpenDirect(byte[] publicId)
        {
            if (_client == null || publicId == null)
                return;

            // 이미 목록에 있으면 서버 왕복 없이 바로 연다.
            string hex = ToHex(publicId);
            ConvEntry existing = _convs.Values.FirstOrDefault(c => c.Type == ConvType.Direct && c.PeerPublicId != null && ToHex(c.PeerPublicId) == hex);
            if (existing != null)
            {
                SelectConv(existing.ConvId);
                ActivationRequested?.Invoke();
                return;
            }

            _client.RequestConvOpenDirect(publicId);
        }

        #endregion

        #region UI 구성

        private void InitializeComponents()
        {
            SuspendLayout();
            DoubleBuffered = true;
            BackColor = ChatTheme.Current.PageBack;
            Font = new Font("맑은 고딕", 9f);

            // ── 오른쪽(대화창) ──────────────────────────────────────────────
            _right = new Panel { Dock = DockStyle.Fill, BackColor = ChatTheme.FixedChatBackground };

            _listMsgs = new BufferedListBox
            {
                Dock = DockStyle.Fill,
                BorderStyle = BorderStyle.None,
                DrawMode = DrawMode.OwnerDrawVariable,
                SelectionMode = SelectionMode.None,
                IntegralHeight = false,
                BackColor = ChatTheme.FixedChatBackground,
            };
            _listMsgs.MeasureItem += ListMsgs_MeasureItem;
            _listMsgs.DrawItem += ListMsgs_DrawItem;
            _listMsgs.MouseDown += ListMsgs_MouseDown;
            _listMsgs.MouseUp += ListMsgs_MouseUp;
            _listMsgs.Resize += (s, e) => RebuildMessageItems(keepPosition: true);

            _inputPanel = new Panel { Dock = DockStyle.Bottom, Height = 64, BackColor = Color.White, Padding = new Padding(10, 8, 10, 8) };
            _btnSend = new Button { Text = "전송", Dock = DockStyle.Right, Width = 70 };
            StyleFilledButton(_btnSend);
            _btnSend.Click += (s, e) => SendInput();
            _txtInput = new TextBox
            {
                Dock = DockStyle.Fill,
                Multiline = true,
                MaxLength = MaxInputChars,
                BorderStyle = BorderStyle.FixedSingle,
                ScrollBars = ScrollBars.Vertical,
            };
            _txtInput.KeyDown += (s, e) =>
            {
                // Enter는 전송, Shift+Enter는 줄바꿈.
                if (e.KeyCode == Keys.Enter && !e.Shift)
                {
                    e.SuppressKeyPress = true;
                    e.Handled = true;
                    SendInput();
                }
            };
            var gap = new Panel { Dock = DockStyle.Right, Width = 8, BackColor = Color.White };
            var gapLeft = new Panel { Dock = DockStyle.Left, Width = 8, BackColor = Color.White };
            _btnImage = new Button { Text = "사진", Dock = DockStyle.Left, Width = 54 };
            StyleOutlineButton(_btnImage);
            _btnImage.Click += (s, e) => PickAndSendImage();
            // Dock은 나중에 추가한 것이 바깥부터 자리를 차지한다: [사진][간격][입력창][간격][전송]
            _inputPanel.Controls.Add(_txtInput);
            _inputPanel.Controls.Add(gap);
            _inputPanel.Controls.Add(_btnSend);
            _inputPanel.Controls.Add(gapLeft);
            _inputPanel.Controls.Add(_btnImage);

            _replyBar = new Panel { Dock = DockStyle.Bottom, Height = 34, BackColor = Color.FromArgb(244, 246, 250), Visible = false, Padding = new Padding(12, 0, 6, 0) };
            _btnReplyCancel = new Button { Text = "✕", Dock = DockStyle.Right, Width = 32, FlatStyle = FlatStyle.Flat, Cursor = Cursors.Hand, TabStop = false };
            _btnReplyCancel.FlatAppearance.BorderSize = 0;
            _btnReplyCancel.Click += (s, e) => CancelReply();
            _lblReply = new Label { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true, ForeColor = ChatTheme.Current.TextSecondary };
            _replyBar.Controls.Add(_lblReply);
            _replyBar.Controls.Add(_btnReplyCancel);

            _msgMenu = new ContextMenuStrip();
            _miReply = new ToolStripMenuItem("답장");
            _miCopy = new ToolStripMenuItem("복사");
            _miDelete = new ToolStripMenuItem("삭제");
            _miReply.Click += (s, e) => BeginReply(_menuRow);
            _miCopy.Click += (s, e) => CopyMessage(_menuRow);
            _miDelete.Click += (s, e) => DeleteMessage(_menuRow);
            _msgMenu.Items.AddRange(new ToolStripItem[] { _miReply, _miCopy, new ToolStripSeparator(), _miDelete });

            _rightHeader = new Panel { Dock = DockStyle.Top, Height = 52, BackColor = Color.White };
            _rightHeader.Paint += (s, e) =>
            {
                using (var pen = new Pen(ChatTheme.Current.Border))
                    e.Graphics.DrawLine(pen, 0, _rightHeader.Height - 1, _rightHeader.Width, _rightHeader.Height - 1);
            };
            _lblTitle = new Label
            {
                Left = 14,
                Top = 8,
                Width = 330,
                Height = 20,
                Font = new Font(Font.FontFamily, 10.5f, FontStyle.Bold),
                AutoEllipsis = true,
            };
            _lblSub = new Label
            {
                Left = 14,
                Top = 29,
                Width = 330,
                Height = 16,
                Font = new Font(Font.FontFamily, 8f),
                AutoEllipsis = true,
            };
            _btnLeave = new Button { Text = "나가기", Width = 62, Height = 28, Top = 12, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            _btnInvite = new Button { Text = "초대", Width = 54, Height = 28, Top = 12, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            _btnRename = new Button { Text = "이름", Width = 54, Height = 28, Top = 12, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            StyleOutlineButton(_btnRename);
            _btnRename.Click += (s, e) => RenameSelected();
            _btnMembers = new Button { Text = "멤버", Width = 54, Height = 28, Top = 12, Anchor = AnchorStyles.Top | AnchorStyles.Right };
            StyleOutlineButton(_btnLeave, danger: true);
            StyleOutlineButton(_btnInvite);
            StyleOutlineButton(_btnMembers);
            _btnLeave.Click += (s, e) => LeaveSelected();
            _btnInvite.Click += (s, e) => InviteToSelected();
            _btnMembers.Click += (s, e) => ShowMembers();
            _rightHeader.Controls.AddRange(new Control[] { _lblTitle, _lblSub, _btnMembers, _btnInvite, _btnRename, _btnLeave });
            _rightHeader.Resize += (s, e) => LayoutHeaderButtons();

            _lblNotice = new Label
            {
                Dock = DockStyle.Bottom,
                Height = 20,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(10, 0, 0, 0),
                ForeColor = ChatTheme.Current.Accent,
                BackColor = Color.White,
                Font = new Font(Font.FontFamily, 8f),
            };
            _noticeTimer = new Timer { Interval = 5000 };
            _noticeTimer.Tick += (s, e) =>
            {
                _noticeTimer.Stop();
                _lblNotice.Text = string.Empty;
            };

            // Dock 순서: 나중에 추가한 것이 바깥부터 자리를 차지한다.
            _right.Controls.Add(_listMsgs);
            _right.Controls.Add(_lblNotice);
            _right.Controls.Add(_replyBar);
            _right.Controls.Add(_inputPanel);
            _right.Controls.Add(_rightHeader);

            _lblEmpty = new Label
            {
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
                Text = "대화방을 선택하거나\r\n[새 대화]로 친구와 대화를 시작하세요.",
                ForeColor = ChatTheme.Current.TextMuted,
                BackColor = ChatTheme.FixedChatBackground,
            };

            // ── 왼쪽(대화방 목록) ───────────────────────────────────────────
            _left = new Panel { Dock = DockStyle.Left, Width = 272, BackColor = Color.White };
            _listConvs = new BufferedListBox
            {
                Dock = DockStyle.Fill,
                BorderStyle = BorderStyle.None,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = ListRowHeight,
                IntegralHeight = false,
                BackColor = Color.White,
            };
            _listConvs.DrawItem += ListConvs_DrawItem;
            _listConvs.SelectedIndexChanged += ListConvs_SelectedIndexChanged;
            _listConvs.MouseUp += (s, e) =>
            {
                if (e.Button != MouseButtons.Right)
                    return;
                int index = _listConvs.IndexFromPoint(e.Location);
                if (index >= 0)
                {
                    _listConvs.SelectedIndex = index;
                    _convMenu.Show(_listConvs, e.Location);
                }
            };

            _convMenu = new ContextMenuStrip();
            var miLeave = new ToolStripMenuItem("대화방 나가기");
            miLeave.Click += (s, e) => LeaveSelected();
            _miMuteRoom = new ToolStripMenuItem("이 방 알림 끄기");
            _miMuteRoom.Click += (s, e) => ToggleMuteSelected();
            _convMenu.Items.Add(_miMuteRoom);
            _convMenu.Items.Add(miLeave);
            _convMenu.Opening += (s, e) =>
            {
                ConvEntry entry = _listConvs.SelectedItem as ConvEntry;
                _miMuteRoom.Text = (entry != null && _settings.IsMuted(entry.ConvId)) ? "이 방 알림 켜기" : "이 방 알림 끄기";
            };

            _notifyMenu = new ContextMenuStrip();
            _miNotifyOn = new ToolStripMenuItem("새 메시지 알림") { CheckOnClick = true };
            _miNotifyPreview = new ToolStripMenuItem("알림에 내용 표시") { CheckOnClick = true };
            _miNotifySound = new ToolStripMenuItem("알림음") { CheckOnClick = true };
            _miNotifyOn.Click += (s, e) => { _settings.NotifyEnabled = _miNotifyOn.Checked; SaveSettings(); SyncNotifyMenu(); };
            _miNotifyPreview.Click += (s, e) => { _settings.ShowPreview = _miNotifyPreview.Checked; SaveSettings(); };
            _miNotifySound.Click += (s, e) => { _settings.Sound = _miNotifySound.Checked; SaveSettings(); };
            _notifyMenu.Items.AddRange(new ToolStripItem[] { _miNotifyOn, _miNotifyPreview, _miNotifySound });

            _leftHeader = new Panel { Dock = DockStyle.Top, Height = 52, BackColor = Color.White };
            _lblListTitle = new Label
            {
                Text = "대화방",
                Left = 14,
                Top = 15,
                Width = 100,
                Height = 22,
                Font = new Font(Font.FontFamily, 11f, FontStyle.Bold),
                ForeColor = ChatTheme.Current.Accent,
            };
            _btnNotify = new Button { Text = "알림", Width = 54, Height = 28, Top = 12, Left = 128 };
            StyleOutlineButton(_btnNotify);
            _btnNotify.Click += (s, e) => _notifyMenu.Show(_btnNotify, new Point(0, _btnNotify.Height));
            _btnNew = new Button { Text = "새 대화", Width = 70, Height = 28, Top = 12, Left = 188 };
            StyleFilledButton(_btnNew);
            _btnNew.Click += (s, e) => NewConversation();
            _leftHeader.Controls.Add(_lblListTitle);
            _leftHeader.Controls.Add(_btnNew);
            _leftHeader.Controls.Add(_btnNotify);
            _leftHeader.Paint += (s, e) =>
            {
                using (var pen = new Pen(ChatTheme.Current.Border))
                    e.Graphics.DrawLine(pen, 0, _leftHeader.Height - 1, _leftHeader.Width, _leftHeader.Height - 1);
            };

            var divider = new Panel { Dock = DockStyle.Left, Width = 1, BackColor = ChatTheme.Current.Border };

            _left.Controls.Add(_listConvs);
            _left.Controls.Add(_leftHeader);

            Controls.Add(_lblEmpty);
            Controls.Add(_right);
            Controls.Add(divider);
            Controls.Add(_left);

            ResumeLayout(false);

            VisibleChanged += (s, e) => MarkReadIfViewing();
            HandleCreated += (s, e) =>
            {
                Form form = FindForm();
                if (form != null)
                    form.Activated += (fs, fe) => MarkReadIfViewing();
            };

            LayoutHeaderButtons();
            ShowSelected();
        }

        private void LayoutHeaderButtons()
        {
            int right = _rightHeader.Width - 12;
            _btnLeave.Left = right - _btnLeave.Width;
            _btnInvite.Left = _btnLeave.Left - 6 - _btnInvite.Width;
            _btnRename.Left = _btnInvite.Left - 6 - _btnRename.Width;
            _btnMembers.Left = (_btnRename.Visible ? _btnRename.Left : _btnInvite.Left) - 6 - _btnMembers.Width;
            int textWidth = Math.Max(80, _btnMembers.Left - 24);
            _lblTitle.Width = textWidth;
            _lblSub.Width = textWidth;
        }

        private static void StyleFilledButton(Button btn)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.FlatAppearance.BorderColor = ChatTheme.Current.Accent;
            btn.BackColor = ChatTheme.Current.Accent;
            btn.ForeColor = Color.White;
            btn.Cursor = Cursors.Hand;
        }

        private static void StyleOutlineButton(Button btn, bool danger = false)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.FlatAppearance.BorderColor = danger ? ChatTheme.Current.Danger : ChatTheme.Current.Border;
            btn.BackColor = Color.White;
            btn.ForeColor = danger ? ChatTheme.Current.Danger : ChatTheme.Current.Accent;
            btn.Cursor = Cursors.Hand;
        }

        private void ShowNotice(string text, FriendNoticeLevel level)
        {
            _lblNotice.ForeColor = level == FriendNoticeLevel.Error ? ChatTheme.Current.Danger : ChatTheme.Current.Accent;
            _lblNotice.Text = text;
            _noticeTimer.Stop();
            _noticeTimer.Start();
            Notice?.Invoke(text, level);
        }

        #endregion

        #region 대화방 목록

        private static string ToHex(byte[] id)
        {
            var sb = new StringBuilder(id.Length * 2);
            foreach (byte b in id)
                sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        private bool IsMe(byte[] id)
        {
            return id != null && _myHex.Length > 0 && ToHex(id) == _myHex;
        }

        private static bool IsEmptyId(byte[] id)
        {
            return id == null || id.All(b => b == 0);
        }

        private string DisplayTitle(ConvEntry c)
        {
            if (c.Type == ConvType.Direct)
                return string.IsNullOrEmpty(c.PeerNickname) ? "(알 수 없음)" : c.PeerNickname;

            if (!string.IsNullOrEmpty(c.Name))
                return c.Name;

            if (c.MembersLoaded)
            {
                List<string> others = c.Members.Values.Where(m => !IsMe(m.PublicId)).Select(m => m.Nickname).Take(3).ToList();
                if (others.Count > 0)
                    return string.Join(", ", others);
            }
            return "그룹 대화";
        }

        private static void ApplySummary(ConvEntry e, ConvSummaryData s)
        {
            e.ConvId = s.ConvId;
            e.Type = s.Type;
            e.Name = s.Name ?? string.Empty;
            e.MemberCount = s.MemberCount;
            e.UnreadCount = s.UnreadCount;
            e.LastMsgId = s.LastMsgId;
            e.LastMsgTime = s.LastMsgTime;
            e.LastKind = s.LastKind;
            e.LastMessage = s.LastMessage ?? string.Empty;
            e.MyLastReadMsgId = s.MyLastReadMsgId;
            e.PeerPublicId = IsEmptyId(s.PeerPublicId) ? null : s.PeerPublicId;
            e.PeerNickname = s.PeerNickname ?? string.Empty;
            e.PeerImageUrl = s.PeerImageUrl ?? string.Empty;
            if (s.LastMsgTime > 0)
                e.SortTime = s.LastMsgTime;
        }

        //***************************************************************************
        // @brief 서버가 준 요약으로 대화방을 추가하거나 갱신한다. 불러둔 메시지/멤버는 유지한다.
        //***************************************************************************
        private ConvEntry UpsertConv(ConvSummaryData summary)
        {
            ConvEntry entry;
            if (!_convs.TryGetValue(summary.ConvId, out entry))
            {
                entry = new ConvEntry();
                _convs[summary.ConvId] = entry;
            }
            ApplySummary(entry, summary);
            return entry;
        }

        private List<ConvEntry> SortedConvs()
        {
            return _convs.Values
                .OrderByDescending(c => c.SortTime)
                .ThenByDescending(c => c.ConvId)
                .ToList();
        }

        private void RefreshConvList()
        {
            long selectedId = _selected != null ? _selected.ConvId : 0;
            List<ConvEntry> sorted = SortedConvs();

            _suppressSelect = true;
            _listConvs.BeginUpdate();
            _listConvs.Items.Clear();
            foreach (ConvEntry c in sorted)
                _listConvs.Items.Add(c);
            if (selectedId != 0)
            {
                int index = sorted.FindIndex(c => c.ConvId == selectedId);
                if (index >= 0)
                    _listConvs.SelectedIndex = index;
            }
            _listConvs.EndUpdate();
            _suppressSelect = false;

            _listConvs.Invalidate();
        }

        private void RaiseTotalUnread()
        {
            TotalUnreadChanged?.Invoke(_convs.Values.Sum(c => c.UnreadCount));
        }

        private void ListConvs_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (_suppressSelect)
                return;

            ConvEntry entry = _listConvs.SelectedItem as ConvEntry;
            if (entry == null || (_selected != null && _selected.ConvId == entry.ConvId))
                return;

            _selected = entry;
            OnSelectedChanged();
        }

        private void SelectConv(long convId)
        {
            ConvEntry entry;
            if (!_convs.TryGetValue(convId, out entry))
                return;

            _selected = entry;
            RefreshConvList();
            OnSelectedChanged();
        }

        private void OnSelectedChanged()
        {
            CancelReply();
            if (_selected != null)
            {
                // 처음 여는 방이면 최근 메시지와 멤버를 불러온다.
                if (!_selected.HistoryLoaded && !_selected.HistoryLoading)
                    LoadHistory(_selected, 0);
                if (!_selected.MembersLoaded)
                    _client?.RequestConvMembers(_selected.ConvId);
            }

            ShowSelected();
            RebuildMessageItems(keepPosition: false);
            MarkReadIfViewing();
            if (_selected != null && Visible)
                _txtInput.Focus();
        }

        private void ShowSelected()
        {
            bool has = _selected != null;
            _right.Visible = has;
            _lblEmpty.Visible = !has;
            if (!has)
                return;

            _lblTitle.Text = DisplayTitle(_selected);
            _lblSub.Text = _selected.IsGroup ? ("멤버 " + _selected.MemberCount + "명") : "1:1 대화";
            _btnInvite.Visible = _selected.IsGroup;
            _btnRename.Visible = _selected.IsGroup;
            LayoutHeaderButtons();
        }

        private void UpdateHeader()
        {
            if (_selected == null)
                return;
            _lblTitle.Text = DisplayTitle(_selected);
            _lblSub.Text = _selected.IsGroup ? ("멤버 " + _selected.MemberCount + "명") : "1:1 대화";
        }

        #endregion

        #region 목록 그리기

        private static string FormatListTime(long unix)
        {
            if (unix <= 0)
                return string.Empty;

            DateTime dt = DateTimeOffset.FromUnixTimeSeconds(unix).LocalDateTime;
            DateTime today = DateTime.Today;
            if (dt.Date == today)
                return dt.ToString("tt h:mm", CultureInfo.GetCultureInfo("ko-KR"));
            if (dt.Date == today.AddDays(-1))
                return "어제";
            if (dt.Year == today.Year)
                return dt.ToString("M월 d일", CultureInfo.GetCultureInfo("ko-KR"));
            return dt.ToString("yyyy.MM.dd", CultureInfo.GetCultureInfo("ko-KR"));
        }

        private static string FormatMessageTime(long unix)
        {
            if (unix <= 0)
                return string.Empty;
            return DateTimeOffset.FromUnixTimeSeconds(unix).LocalDateTime.ToString("tt h:mm", CultureInfo.GetCultureInfo("ko-KR"));
        }

        private static string PreviewText(ConvEntry c)
        {
            if (c.LastMsgId == 0)
                return "대화를 시작해 보세요.";
            switch (c.LastKind)
            {
                case ConvMessageKind.Image: return "(사진)";
                case ConvMessageKind.Deleted: return DeletedText;
                default: return FirstLine(c.LastMessage);
            }
        }

        private static string FirstLine(string text)
        {
            if (string.IsNullOrEmpty(text))
                return string.Empty;
            int cut = text.IndexOfAny(new[] { '\r', '\n' });
            return cut >= 0 ? text.Substring(0, cut) : text;
        }

        private void ListConvs_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _listConvs.Items.Count)
                return;

            var c = (ConvEntry)_listConvs.Items[e.Index];
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            bool selected = (e.State & DrawItemState.Selected) != 0;

            using (var back = new SolidBrush(selected ? Color.FromArgb(235, 241, 255) : Color.White))
                g.FillRectangle(back, e.Bounds);

            var avatar = new Rectangle(e.Bounds.Left + 12, e.Bounds.Top + (e.Bounds.Height - AvatarSize) / 2, AvatarSize, AvatarSize);
            DrawAvatar(g, avatar, c.Type == ConvType.Direct ? c.PeerImageUrl : null, DisplayTitle(c), c.IsGroup);

            int textLeft = avatar.Right + 10;
            int rightEdge = e.Bounds.Right - 12;
            string time = FormatListTime(c.LastMsgTime);

            using (var titleFont = new Font(Font.FontFamily, 9.5f, FontStyle.Bold))
            using (var subFont = new Font(Font.FontFamily, 8.5f))
            using (var metaFont = new Font(Font.FontFamily, 7.5f))
            using (var titleBrush = new SolidBrush(Color.FromArgb(30, 30, 30)))
            using (var subBrush = new SolidBrush(ChatTheme.Current.TextSecondary))
            using (var metaBrush = new SolidBrush(ChatTheme.Current.TextMuted))
            using (var fmt = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
            {
                int timeWidth = (int)g.MeasureString(time, metaFont).Width + 2;
                bool muted = _settings.IsMuted(c.ConvId);
                int muteWidth = muted ? 34 : 0;
                var titleRect = new RectangleF(textLeft, e.Bounds.Top + 12, rightEdge - textLeft - timeWidth - 6 - muteWidth, 20);
                string title = DisplayTitle(c);
                if (c.IsGroup && c.MemberCount > 0)
                    title += "  " + c.MemberCount;
                g.DrawString(title, titleFont, titleBrush, titleRect, fmt);
                g.DrawString(time, metaFont, metaBrush, new PointF(rightEdge - timeWidth, e.Bounds.Top + 15));
                if (muted)
                    g.DrawString("알림끔", metaFont, metaBrush, new PointF(rightEdge - timeWidth - muteWidth, e.Bounds.Top + 15));

                int badgeWidth = 0;
                if (c.UnreadCount > 0)
                {
                    string badge = c.UnreadCount > 99 ? "99+" : c.UnreadCount.ToString();
                    using (var badgeFont = new Font(Font.FontFamily, 7.5f, FontStyle.Bold))
                    {
                        int w = Math.Max(20, (int)g.MeasureString(badge, badgeFont).Width + 8);
                        var badgeRect = new Rectangle(rightEdge - w, e.Bounds.Top + 34, w, 18);
                        using (var path = RoundedRect(badgeRect, 9))
                        using (var brush = new SolidBrush(BadgeColor))
                            g.FillPath(brush, path);
                        using (var centered = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                            g.DrawString(badge, badgeFont, Brushes.White, badgeRect, centered);
                        badgeWidth = w + 6;
                    }
                }

                string preview = PreviewText(c);
                var subRect = new RectangleF(textLeft, e.Bounds.Top + 35, rightEdge - textLeft - badgeWidth, 18);
                g.DrawString(preview, subFont, subBrush, subRect, fmt);
            }

            using (var pen = new Pen(Color.FromArgb(240, 241, 244)))
                g.DrawLine(pen, e.Bounds.Left + 12, e.Bounds.Bottom - 1, e.Bounds.Right, e.Bounds.Bottom - 1);
        }

        private static GraphicsPath RoundedRect(Rectangle r, int radius)
        {
            int d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            var path = new GraphicsPath();
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        //***************************************************************************
        // @brief 원형 아바타를 그린다. 이미지가 있으면 이미지, 없으면 이름 첫 글자 + 이름 기반 색.
        //        그룹이면 이미지 없이 둥근 사각형에 첫 글자를 그린다.
        //***************************************************************************
        private void DrawAvatar(Graphics g, Rectangle r, string url, string name, bool group)
        {
            Image image = string.IsNullOrEmpty(url) ? null : GetThumbnail(url);

            GraphicsState state = g.Save();
            using (GraphicsPath path = group ? RoundedRect(r, r.Width / 3) : CirclePath(r))
            {
                g.SetClip(path, CombineMode.Replace);
                if (image != null)
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.DrawImage(image, r);
                }
                else
                {
                    int hash = 0;
                    foreach (char ch in name ?? string.Empty)
                        hash = hash * 31 + ch;
                    Color color = AvatarPalette[Math.Abs(hash % AvatarPalette.Length)];
                    using (var brush = new SolidBrush(color))
                        g.FillRectangle(brush, r);
                    string initial = string.IsNullOrEmpty(name) ? "?" : name.Substring(0, 1);
                    using (var font = new Font(Font.FontFamily, r.Width * 0.38f, FontStyle.Bold, GraphicsUnit.Pixel))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(initial, font, Brushes.White, r, fmt);
                }
            }
            g.Restore(state);
        }

        private static GraphicsPath CirclePath(Rectangle r)
        {
            var path = new GraphicsPath();
            path.AddEllipse(r);
            return path;
        }

        #endregion

        #region 썸네일

        //***************************************************************************
        // @brief 캐시에 있으면 즉시 반환, 없으면 비동기로 받아오고 그 동안은 null(기본 아바타).
        //***************************************************************************
        private Image GetThumbnail(string url)
        {
            bool startFetch = false;
            lock (_thumbnailCache)
            {
                Image cached;
                if (_thumbnailCache.TryGetValue(url, out cached))
                    return cached;
                if (_thumbnailPending.Add(url))
                    startFetch = true;
            }

            if (startFetch)
                _ = FetchThumbnailAsync(url);
            return null;
        }

        private async Task FetchThumbnailAsync(string url)
        {
            try
            {
                byte[] bytes = await _thumbnailHttpClient.GetByteArrayAsync(url);
                if (bytes != null && bytes.Length > 0)
                {
                    using (var ms = new MemoryStream(bytes))
                    using (var original = Image.FromStream(ms))
                    {
                        var thumb = new Bitmap(original, new Size(96, 96));
                        lock (_thumbnailCache)
                            _thumbnailCache[url] = thumb;
                    }
                }
            }
            catch
            {
                // 네트워크 오류/디코딩 실패 — 계속 기본 아바타로 보인다.
            }
            finally
            {
                lock (_thumbnailCache)
                    _thumbnailPending.Remove(url);
                Post(() =>
                {
                    _listConvs.Invalidate();
                    _listMsgs.Invalidate();
                });
            }
        }

        #endregion

        #region 대화창(말풍선)

        private int BubbleMaxWidth()
        {
            return Math.Max(160, (int)((_listMsgs.ClientSize.Width - 20) * 0.62));
        }

        private Font MessageFont()
        {
            return Font;
        }

        private Size MeasureText(Graphics g, string text, int maxWidth)
        {
            return TextRenderer.MeasureText(g, text, MessageFont(), new Size(maxWidth, int.MaxValue),
                TextFormatFlags.WordBreak | TextFormatFlags.NoPadding | TextFormatFlags.TextBoxControl);
        }

        private struct BubbleLayout
        {
            public Size Content;        // 본문(텍스트) 또는 사진의 표시 크기
            public int QuoteH;          // 답장 인용 블록 높이(없으면 0)
            public int PadX;
            public int PadY;
            public int Width;
            public int Height;
            public bool IsImage;
        }

        //***************************************************************************
        // @brief 말풍선의 크기를 계산한다(MeasureItem과 DrawItem이 같은 값을 쓰도록 한 곳에 모았다).
        //***************************************************************************
        private BubbleLayout LayoutBubble(Graphics g, MsgRow row)
        {
            ConvMessageData m = row.Msg;
            bool deleted = m.Kind == ConvMessageKind.Deleted;
            var layout = new BubbleLayout();
            layout.IsImage = m.Kind == ConvMessageKind.Image;
            layout.PadX = layout.IsImage ? 4 : 10;
            layout.PadY = layout.IsImage ? 4 : 7;

            int maxContent = BubbleMaxWidth() - 2 * layout.PadX;
            layout.Content = layout.IsImage ? ImageDisplaySize(m.Message) : MeasureText(g, deleted ? DeletedText : row.Text, maxContent);

            bool quote = !deleted && m.ReplyToMsgId > 0;
            layout.QuoteH = quote ? 38 : 0;
            int contentWidth = layout.Content.Width;
            if (quote)
                contentWidth = Math.Max(contentWidth, Math.Min(maxContent, 150));

            layout.Width = contentWidth + 2 * layout.PadX;
            layout.Height = layout.QuoteH + layout.Content.Height + 2 * layout.PadY;
            return layout;
        }

        private static Size FitInside(int width, int height, int maxSide)
        {
            double scale = Math.Min(1.0, (double)maxSide / Math.Max(width, height));
            return new Size(Math.Max(24, (int)(width * scale)), Math.Max(24, (int)(height * scale)));
        }

        private Size ImageDisplaySize(string url)
        {
            Image image = GetMessageImage(url);
            if (image != null)
                return FitInside(image.Width, image.Height, ImageMaxSide);
            return IsImageFailed(url) ? new Size(200, 80) : new Size(200, 150);
        }

        private static bool IsImageFailed(string url)
        {
            lock (_imageCache)
                return _imageFailed.Contains(url);
        }

        //***************************************************************************
        // @brief 사진 메시지의 이미지. 캐시에 있으면 반환, 없으면 비동기로 받아오고 그 동안 null.
        //        받아오면 줄 높이가 달라지므로 대화창을 다시 구성한다.
        //***************************************************************************
        private Image GetMessageImage(string url)
        {
            if (string.IsNullOrEmpty(url))
                return null;

            bool startFetch = false;
            lock (_imageCache)
            {
                Image cached;
                if (_imageCache.TryGetValue(url, out cached))
                    return cached;
                if (!_imageFailed.Contains(url) && _imagePending.Add(url))
                    startFetch = true;
            }

            if (startFetch)
                _ = FetchMessageImageAsync(url);
            return null;
        }

        private async Task FetchMessageImageAsync(string url)
        {
            bool ok = false;
            try
            {
                byte[] bytes = await _thumbnailHttpClient.GetByteArrayAsync(url);
                using (var ms = new MemoryStream(bytes))
                using (var original = Image.FromStream(ms))
                {
                    Size size = FitInside(original.Width, original.Height, ImageCacheSide);
                    var scaled = new Bitmap(size.Width, size.Height);
                    using (var g = Graphics.FromImage(scaled))
                    {
                        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                        g.DrawImage(original, 0, 0, size.Width, size.Height);
                    }
                    lock (_imageCache)
                        _imageCache[url] = scaled;
                    ok = true;
                }
            }
            catch
            {
                // 네트워크 오류/디코딩 실패 — 자리표시 상자에 안내 문구를 보여 준다.
            }
            finally
            {
                lock (_imageCache)
                {
                    _imagePending.Remove(url);
                    if (!ok)
                        _imageFailed.Add(url);
                }
                Post(OnMessageImageLoaded);
            }
        }

        private void OnMessageImageLoaded()
        {
            if (_selected == null)
                return;
            bool stick = IsNearBottom();
            RebuildMessageItems(keepPosition: !stick);
        }

        //***************************************************************************
        // @brief 메시지 목록을 대화창 줄 목록으로 풀어 다시 채운다(날짜 구분선, "이전 대화 더 보기" 포함).
        //***************************************************************************
        private void RebuildMessageItems(bool keepPosition)
        {
            if (_selected == null)
            {
                _rows = new List<MsgRow>();
                _listMsgs.Items.Clear();
                return;
            }

            ConvEntry c = _selected;
            int oldCount = _rows.Count;
            int oldTop = _listMsgs.TopIndex;

            var rows = new List<MsgRow>();
            if (c.HasMore)
                rows.Add(new MsgRow { Kind = RowKind.More, Text = c.HistoryLoading ? "불러오는 중..." : "이전 대화 더 보기" });

            DateTime lastDate = DateTime.MinValue;
            byte[] lastSender = null;
            bool lastWasText = false;
            foreach (ConvMessageData m in c.Messages)
            {
                DateTime local = DateTimeOffset.FromUnixTimeSeconds(m.Timestamp).LocalDateTime;
                if (local.Date != lastDate)
                {
                    rows.Add(new MsgRow
                    {
                        Kind = RowKind.Date,
                        Text = local.ToString("yyyy년 M월 d일 dddd", CultureInfo.GetCultureInfo("ko-KR")),
                    });
                    lastDate = local.Date;
                    lastWasText = false;
                }

                if (m.Kind == ConvMessageKind.System)
                {
                    rows.Add(new MsgRow { Kind = RowKind.System, Msg = m, Text = m.Message });
                    lastWasText = false;
                    continue;
                }

                bool mine = IsMe(m.SenderPublicId);
                bool sameSender = lastWasText && lastSender != null && m.SenderPublicId != null && ToHex(lastSender) == ToHex(m.SenderPublicId);
                rows.Add(new MsgRow
                {
                    Kind = RowKind.Text,
                    Msg = m,
                    Text = m.Message,
                    Mine = mine,
                    ShowSender = !mine && !sameSender,
                });
                lastSender = m.SenderPublicId;
                lastWasText = true;
            }

            _rows = rows;

            _listMsgs.BeginUpdate();
            _listMsgs.Items.Clear();
            foreach (MsgRow row in rows)
                _listMsgs.Items.Add(row);
            _listMsgs.EndUpdate();

            if (!keepPosition)
            {
                if (_listMsgs.Items.Count > 0)
                    _listMsgs.TopIndex = _listMsgs.Items.Count - 1;
            }
            else if (_listMsgs.Items.Count > 0)
            {
                // 위쪽에 줄이 늘었으면(이전 대화를 불러옴) 보던 줄이 그대로 보이도록 밀어낸다.
                int shift = rows.Count - oldCount;
                int top = shift > 0 ? oldTop + shift : oldTop;
                _listMsgs.TopIndex = Math.Max(0, Math.Min(top, _listMsgs.Items.Count - 1));
            }

            _listMsgs.Invalidate();
        }

        private bool IsNearBottom()
        {
            if (_listMsgs.Items.Count == 0)
                return true;
            int lastVisible = _listMsgs.TopIndex;
            int bottom = 0;
            while (lastVisible < _listMsgs.Items.Count && bottom < _listMsgs.ClientSize.Height)
            {
                bottom += _listMsgs.GetItemHeight(lastVisible);
                lastVisible++;
            }
            return lastVisible >= _listMsgs.Items.Count - 1;
        }

        private void ListMsgs_MeasureItem(object sender, MeasureItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _listMsgs.Items.Count)
                return;

            var row = (MsgRow)_listMsgs.Items[e.Index];
            switch (row.Kind)
            {
                case RowKind.More:
                    e.ItemHeight = 36;
                    break;
                case RowKind.Date:
                    e.ItemHeight = 38;
                    break;
                case RowKind.System:
                    e.ItemHeight = MeasureText(e.Graphics, row.Text, _listMsgs.ClientSize.Width - 80).Height + 16;
                    break;
                default:
                    {
                        BubbleLayout layout = LayoutBubble(e.Graphics, row);
                        int nameHeight = (row.ShowSender && _selected != null && _selected.IsGroup) ? 18 : 0;
                        int height = nameHeight + layout.Height + 8;
                        if (row.ShowSender)
                            height = Math.Max(height, BubbleAvatarSize + 12);
                        e.ItemHeight = height;
                        break;
                    }
            }
        }

        //***************************************************************************
        // @brief 이 메시지를 아직 읽지 않은 사람 수(발신자 제외). 멤버 목록을 받기 전엔 0(표시 안 함).
        //***************************************************************************
        private int UnreadReaders(ConvEntry c, ConvMessageData m)
        {
            if (!c.MembersLoaded || (m.Kind != ConvMessageKind.Text && m.Kind != ConvMessageKind.Image))
                return 0;

            string senderHex = m.SenderPublicId == null ? string.Empty : ToHex(m.SenderPublicId);
            int count = 0;
            foreach (KeyValuePair<string, ConvMemberData> kv in c.Members)
            {
                if (kv.Key == senderHex)
                    continue;
                if (kv.Value.LastReadMsgId < m.MsgId)
                    count++;
            }
            return count;
        }

        private void ListMsgs_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _listMsgs.Items.Count || _selected == null)
                return;

            var row = (MsgRow)_listMsgs.Items[e.Index];
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;

            using (var back = new SolidBrush(ChatTheme.FixedChatBackground))
                g.FillRectangle(back, e.Bounds);

            Rectangle b = e.Bounds;
            using (var small = new Font(Font.FontFamily, 7.5f))
            using (var mutedBrush = new SolidBrush(ChatTheme.Current.TextMuted))
            using (var centered = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
            {
                switch (row.Kind)
                {
                    case RowKind.More:
                        using (var linkFont = new Font(Font.FontFamily, 8.5f, FontStyle.Underline))
                        using (var brush = new SolidBrush(ChatTheme.Current.Accent))
                            g.DrawString(row.Text, linkFont, brush, b, centered);
                        return;

                    case RowKind.Date:
                        {
                            var pill = new Rectangle(b.Left + b.Width / 2 - 90, b.Top + 8, 180, 22);
                            using (var path = RoundedRect(pill, 11))
                            using (var brush = new SolidBrush(Color.FromArgb(238, 240, 244)))
                                g.FillPath(brush, path);
                            g.DrawString(row.Text, small, mutedBrush, pill, centered);
                            return;
                        }

                    case RowKind.System:
                        {
                            Size text = MeasureText(g, row.Text, b.Width - 80);
                            var pill = new Rectangle(b.Left + (b.Width - text.Width) / 2 - 10, b.Top + 4, text.Width + 20, text.Height + 8);
                            using (var path = RoundedRect(pill, 10))
                            using (var brush = new SolidBrush(Color.FromArgb(238, 240, 244)))
                                g.FillPath(brush, path);
                            TextRenderer.DrawText(g, row.Text, MessageFont(), new Rectangle(pill.Left + 10, pill.Top + 4, text.Width, text.Height),
                                ChatTheme.Current.TextSecondary, TextFormatFlags.WordBreak | TextFormatFlags.NoPadding | TextFormatFlags.TextBoxControl);
                            return;
                        }
                }

                // ── 말풍선(텍스트/사진/삭제됨) ──────────────────────────────
                ConvMessageData m = row.Msg;
                BubbleLayout layout = LayoutBubble(g, row);
                bool showName = row.ShowSender && _selected.IsGroup;
                int top = b.Top + 4 + (showName ? 18 : 0);

                int unreadReaders = UnreadReaders(_selected, m);
                string time = FormatMessageTime(m.Timestamp);

                Rectangle bubble;
                if (row.Mine)
                {
                    bubble = new Rectangle(b.Right - 14 - layout.Width, top, layout.Width, layout.Height);
                }
                else
                {
                    int left = b.Left + 10;
                    if (row.ShowSender)
                    {
                        var avatar = new Rectangle(left, b.Top + 4, BubbleAvatarSize, BubbleAvatarSize);
                        DrawAvatar(g, avatar, m.SenderImageUrl, m.SenderNickname, false);
                    }
                    int bubbleLeft = left + BubbleAvatarSize + 8;

                    if (showName)
                        g.DrawString(m.SenderNickname, small, mutedBrush, new PointF(bubbleLeft + 2, b.Top + 3));

                    bubble = new Rectangle(bubbleLeft, top, layout.Width, layout.Height);
                }
                row.BubbleBounds = bubble;
                DrawBubble(g, row, layout, bubble);
                DrawMeta(g, small, mutedBrush, bubble, unreadReaders, time, alignRight: row.Mine);
            }
        }

        //***************************************************************************
        // @brief 말풍선 하나(배경, 답장 인용, 본문/사진)를 그린다.
        //***************************************************************************
        private void DrawBubble(Graphics g, MsgRow row, BubbleLayout layout, Rectangle bubble)
        {
            ConvMessageData m = row.Msg;
            bool deleted = m.Kind == ConvMessageKind.Deleted;

            using (var path = RoundedRect(bubble, 10))
            {
                Color fill = deleted ? Color.FromArgb(240, 241, 244) : (row.Mine ? ChatTheme.Current.MyBubble : Color.White);
                using (var brush = new SolidBrush(fill))
                    g.FillPath(brush, path);
                if (!row.Mine || deleted)
                    using (var pen = new Pen(ChatTheme.Current.Border))
                        g.DrawPath(pen, path);
            }

            Color textColor = deleted ? ChatTheme.Current.TextMuted : (row.Mine ? ChatTheme.Current.MyText : ChatTheme.Current.OtherText);
            int x = bubble.Left + layout.PadX;
            int y = bubble.Top + layout.PadY;
            int innerWidth = bubble.Width - 2 * layout.PadX;

            if (layout.QuoteH > 0)
            {
                DrawQuote(g, row, new Rectangle(x, y, innerWidth, layout.QuoteH - 6));
                y += layout.QuoteH;
            }

            if (layout.IsImage)
            {
                var target = new Rectangle(x, y, layout.Content.Width, layout.Content.Height);
                Image image = GetMessageImage(m.Message);
                GraphicsState state = g.Save();
                using (GraphicsPath clip = RoundedRect(target, 8))
                {
                    g.SetClip(clip, CombineMode.Replace);
                    if (image != null)
                    {
                        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                        g.DrawImage(image, target);
                    }
                    else
                    {
                        using (var back = new SolidBrush(Color.FromArgb(232, 234, 238)))
                            g.FillRectangle(back, target);
                        using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        using (var brush = new SolidBrush(ChatTheme.Current.TextMuted))
                            g.DrawString(IsImageFailed(m.Message) ? "사진을 불러올 수 없습니다" : "사진 불러오는 중...", Font, brush, target, fmt);
                    }
                }
                g.Restore(state);
                return;
            }

            string text = deleted ? DeletedText : row.Text;
            TextRenderer.DrawText(g, text, MessageFont(), new Rectangle(x, y, layout.Content.Width, layout.Content.Height),
                textColor, TextFormatFlags.WordBreak | TextFormatFlags.NoPadding | TextFormatFlags.TextBoxControl);
        }

        //***************************************************************************
        // @brief 답장 인용 블록(왼쪽 막대 + 보낸 사람 + 원문 한 줄)을 그린다.
        //***************************************************************************
        private void DrawQuote(Graphics g, MsgRow row, Rectangle r)
        {
            ConvMessageData m = row.Msg;
            Color bar = row.Mine ? Color.FromArgb(200, 255, 255, 255) : ChatTheme.Current.Accent;
            Color nameColor = row.Mine ? Color.White : ChatTheme.Current.TextSecondary;
            Color previewColor = row.Mine ? Color.FromArgb(225, 255, 255, 255) : ChatTheme.Current.TextSecondary;

            using (var barBrush = new SolidBrush(bar))
                g.FillRectangle(barBrush, r.Left, r.Top, 3, r.Height);

            string preview = m.ReplyKind == ConvMessageKind.Image ? "(사진)"
                : m.ReplyKind == ConvMessageKind.Deleted ? DeletedText
                : FirstLine(m.ReplyPreview);

            using (var nameFont = new Font(Font.FontFamily, 8f, FontStyle.Bold))
            using (var previewFont = new Font(Font.FontFamily, 8.5f))
            using (var nameBrush = new SolidBrush(nameColor))
            using (var previewBrush = new SolidBrush(previewColor))
            using (var fmt = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
            {
                string name = string.IsNullOrEmpty(m.ReplySenderNickname) ? "(알 수 없음)" : m.ReplySenderNickname;
                g.DrawString(name, nameFont, nameBrush, new RectangleF(r.Left + 8, r.Top, r.Width - 8, 15), fmt);
                g.DrawString(preview, previewFont, previewBrush, new RectangleF(r.Left + 8, r.Top + 15, r.Width - 8, 16), fmt);
            }
        }

        // 말풍선 옆에 "안 읽은 사람 수"(노란 숫자)와 시각을 그린다. 내 메시지는 왼쪽, 상대 메시지는 오른쪽.
        private void DrawMeta(Graphics g, Font small, Brush mutedBrush, Rectangle bubble, int unreadReaders, string time, bool alignRight)
        {
            using (var boldSmall = new Font(small, FontStyle.Bold))
            using (var unreadBrush = new SolidBrush(UnreadReaderColor))
            {
                SizeF timeSize = g.MeasureString(time, small);
                string unreadText = unreadReaders > 0 ? unreadReaders.ToString() : null;
                SizeF unreadSize = unreadText != null ? g.MeasureString(unreadText, boldSmall) : SizeF.Empty;

                float x = alignRight ? bubble.Left - 6 : bubble.Right + 6;
                float bottom = bubble.Bottom;
                float timeY = bottom - timeSize.Height;
                float unreadY = timeY - unreadSize.Height + 2;

                if (alignRight)
                {
                    g.DrawString(time, small, mutedBrush, new PointF(x - timeSize.Width, timeY));
                    if (unreadText != null)
                        g.DrawString(unreadText, boldSmall, unreadBrush, new PointF(x - unreadSize.Width, unreadY));
                }
                else
                {
                    g.DrawString(time, small, mutedBrush, new PointF(x, timeY));
                    if (unreadText != null)
                        g.DrawString(unreadText, boldSmall, unreadBrush, new PointF(x, unreadY));
                }
            }
        }

        private void ListMsgs_MouseUp(object sender, MouseEventArgs e)
        {
            int index = _listMsgs.IndexFromPoint(e.Location);
            if (index < 0 || index >= _listMsgs.Items.Count)
                return;
            var row = _listMsgs.Items[index] as MsgRow;
            if (row == null || row.Kind != RowKind.Text || row.Msg == null)
                return;

            if (e.Button == MouseButtons.Left)
            {
                // 사진 말풍선을 누르면 원본 크기 보기.
                if (row.Msg.Kind == ConvMessageKind.Image && row.BubbleBounds.Contains(e.Location))
                    ShowImageViewer(row.Msg.Message);
                return;
            }

            if (e.Button != MouseButtons.Right)
                return;

            ConvMessageKind kind = row.Msg.Kind;
            bool sendable = kind == ConvMessageKind.Text || kind == ConvMessageKind.Image;
            _menuRow = row;
            _miReply.Enabled = sendable;
            _miCopy.Enabled = kind == ConvMessageKind.Text;
            _miDelete.Enabled = sendable && row.Mine;
            if (!_miReply.Enabled && !_miCopy.Enabled && !_miDelete.Enabled)
                return;
            _msgMenu.Show(_listMsgs, e.Location);
        }

        private void ShowImageViewer(string url)
        {
            if (string.IsNullOrEmpty(url))
                return;
            var viewer = new ImageViewerForm(url, _thumbnailHttpClient);
            viewer.Show(FindForm());
        }

        private void ListMsgs_MouseDown(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left)
                return;

            int index = _listMsgs.IndexFromPoint(e.Location);
            if (index < 0)
                return;

            var row = _listMsgs.Items[index] as MsgRow;
            if (row != null && row.Kind == RowKind.More && _selected != null && !_selected.HistoryLoading)
            {
                long oldest = _selected.Messages.Count > 0 ? _selected.Messages[0].MsgId : 0;
                LoadHistory(_selected, oldest);
                RebuildMessageItems(keepPosition: true);
            }
        }

        #endregion

        #region 사용자 동작

        private void LoadHistory(ConvEntry c, long beforeMsgId)
        {
            if (_client == null)
                return;

            c.HistoryLoading = true;
            c.HistoryIsInitial = (beforeMsgId == 0);
            c.IncomingHistory = new List<ConvMessageData>();
            c.HistoryRequestId = _client.RequestConvHistory(c.ConvId, beforeMsgId, HistoryPageSize);
        }

        private static int Utf8Length(string s)
        {
            return Encoding.UTF8.GetByteCount(s);
        }

        private void SendInput()
        {
            if (_client == null || _selected == null)
                return;

            string text = _txtInput.Text.Trim();
            if (text.Length == 0)
                return;

            // 서버 버퍼는 NUL을 포함해 kConvMessageBytes바이트다.
            if (Utf8Length(text) >= ConvConstants.MessageBytes)
            {
                ShowNotice("메시지가 너무 깁니다. 줄여서 다시 보내 주세요.", FriendNoticeLevel.Warning);
                return;
            }

            _client.RequestConvSend(_selected.ConvId, text, ConvMessageKind.Text, _replyTarget != null ? _replyTarget.MsgId : 0);
            _txtInput.Clear();
            CancelReply();
            _txtInput.Focus();
        }

        private void BeginReply(MsgRow row)
        {
            if (row == null || row.Msg == null || _selected == null)
                return;
            if (row.Msg.Kind != ConvMessageKind.Text && row.Msg.Kind != ConvMessageKind.Image)
                return;

            _replyTarget = row.Msg;
            string name = row.Mine ? "나" : row.Msg.SenderNickname;
            string preview = row.Msg.Kind == ConvMessageKind.Image ? "(사진)" : FirstLine(row.Msg.Message);
            _lblReply.Text = name + "에게 답장: " + preview;
            _replyBar.Visible = true;
            _txtInput.Focus();
        }

        private void CancelReply()
        {
            _replyTarget = null;
            if (_replyBar != null)
                _replyBar.Visible = false;
        }

        private void CopyMessage(MsgRow row)
        {
            if (row == null || row.Msg == null || row.Msg.Kind != ConvMessageKind.Text)
                return;
            try
            {
                Clipboard.SetText(row.Msg.Message);
            }
            catch (System.Runtime.InteropServices.ExternalException)
            {
                // 클립보드를 다른 프로그램이 잡고 있음 — 무시.
            }
        }

        private void DeleteMessage(MsgRow row)
        {
            if (_client == null || _selected == null || row == null || row.Msg == null || !row.Mine)
                return;

            if (MessageBox.Show(FindForm(), "이 메시지를 삭제하시겠습니까?\r\n대화상대 모두에게서 삭제됩니다.", "메시지 삭제",
                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;

            _client.RequestConvDelete(_selected.ConvId, row.Msg.MsgId);
        }

        private bool _imageSending;

        //***************************************************************************
        // @brief 사진을 골라 파일 서버에 올린 뒤 이미지 메시지로 보낸다(선택한 답장 대상이 있으면 답장으로).
        //***************************************************************************
        private async void PickAndSendImage()
        {
            if (_client == null || _selected == null || _imageSending)
                return;
            if (ImageUploader == null)
            {
                ShowNotice("사진 업로드를 사용할 수 없습니다.", FriendNoticeLevel.Warning);
                return;
            }

            string path;
            using (var dialog = new OpenFileDialog())
            {
                dialog.Title = "보낼 사진 선택";
                dialog.Filter = "이미지 파일|*.jpg;*.jpeg;*.png;*.gif;*.bmp|모든 파일|*.*";
                if (dialog.ShowDialog(FindForm()) != DialogResult.OK)
                    return;
                path = dialog.FileName;
            }

            try
            {
                if (new FileInfo(path).Length > MaxImageBytes)
                {
                    ShowNotice("사진은 10MB 이하만 보낼 수 있습니다.", FriendNoticeLevel.Warning);
                    return;
                }

                // 이미지로 열리는 파일인지 확인한다(확장자만 바꾼 다른 파일 방지).
                using (var test = Image.FromFile(path))
                {
                }
            }
            catch (Exception)
            {
                ShowNotice("이미지 파일을 열 수 없습니다.", FriendNoticeLevel.Warning);
                return;
            }

            long convId = _selected.ConvId;
            long replyTo = _replyTarget != null ? _replyTarget.MsgId : 0;

            _imageSending = true;
            _btnImage.Enabled = false;
            ShowNotice("사진을 올리는 중...", FriendNoticeLevel.Info);
            try
            {
                string url = await ImageUploader(path);
                if (string.IsNullOrEmpty(url))
                {
                    ShowNotice("사진을 올리지 못했습니다.", FriendNoticeLevel.Error);
                    return;
                }

                if (_client == null)
                    return;
                // 업로드하는 동안 다른 대화방으로 옮겼더라도 처음 고른 방으로 보낸다.
                if (UTF8Length(url) >= ConvConstants.MessageBytes)
                {
                    ShowNotice("사진 주소가 너무 깁니다.", FriendNoticeLevel.Error);
                    return;
                }
                _client.RequestConvSend(convId, url, ConvMessageKind.Image, replyTo);
                if (_replyTarget != null && _replyTarget.MsgId == replyTo)
                    CancelReply();
            }
            finally
            {
                _imageSending = false;
                _btnImage.Enabled = _client != null;
            }
        }

        private static int UTF8Length(string s)
        {
            return Encoding.UTF8.GetByteCount(s);
        }

        private List<FriendInfo> FriendsSnapshot()
        {
            return _friends.Values.OrderByDescending(f => f.Online).ThenBy(f => f.Nickname, StringComparer.CurrentCulture).ToList();
        }

        private void NewConversation()
        {
            if (_client == null)
                return;

            List<FriendInfo> friends = FriendsSnapshot();
            if (friends.Count == 0)
            {
                ShowNotice("대화할 친구가 없습니다. 먼저 친구를 추가해 주세요.", FriendNoticeLevel.Warning);
                return;
            }

            using (var dialog = new ConvPickerDialog("새 대화", "대화할 친구를 선택하세요. (2명 이상이면 그룹 대화)", friends.Select(f => new ConvPickerItem(f.PublicId, f.Nickname, f.Online)).ToList(), allowGroupName: true))
            {
                if (dialog.ShowDialog(FindForm()) != DialogResult.OK || dialog.Selected.Count == 0)
                    return;

                if (dialog.Selected.Count == 1)
                    OpenDirect(dialog.Selected[0]);
                else
                    _client.RequestConvCreateGroup(dialog.GroupName, dialog.Selected);
            }
        }

        private void InviteToSelected()
        {
            if (_client == null || _selected == null || !_selected.IsGroup)
                return;

            // 이미 멤버인 친구는 후보에서 뺀다(멤버 목록을 아직 못 받았으면 서버가 걸러준다).
            var existing = new HashSet<string>(_selected.Members.Keys);
            List<ConvPickerItem> candidates = FriendsSnapshot()
                .Where(f => !existing.Contains(ToHex(f.PublicId)))
                .Select(f => new ConvPickerItem(f.PublicId, f.Nickname, f.Online))
                .ToList();
            if (candidates.Count == 0)
            {
                ShowNotice("초대할 수 있는 친구가 없습니다.", FriendNoticeLevel.Info);
                return;
            }

            using (var dialog = new ConvPickerDialog("대화상대 초대", "초대할 친구를 선택하세요.", candidates, allowGroupName: false))
            {
                if (dialog.ShowDialog(FindForm()) != DialogResult.OK || dialog.Selected.Count == 0)
                    return;

                _client.RequestConvInvite(_selected.ConvId, dialog.Selected);
            }
        }

        //***************************************************************************
        // @brief 데스크톱 알림 설정을 메뉴 체크 상태에 반영한다.
        //***************************************************************************
        private void SyncNotifyMenu()
        {
            _miNotifyOn.Checked = _settings.NotifyEnabled;
            _miNotifyPreview.Checked = _settings.ShowPreview;
            _miNotifySound.Checked = _settings.Sound;
            _miNotifyPreview.Enabled = _settings.NotifyEnabled;
            _miNotifySound.Enabled = _settings.NotifyEnabled;
        }

        private void SaveSettings()
        {
            _settings.Save(_myHex);
        }

        private void ToggleMuteSelected()
        {
            ConvEntry entry = _listConvs.SelectedItem as ConvEntry;
            if (entry == null)
                return;

            _settings.SetMuted(entry.ConvId, !_settings.IsMuted(entry.ConvId));
            SaveSettings();
            _listConvs.Invalidate();
        }

        //***************************************************************************
        // @brief 창이 활성 상태가 아니면(다른 프로그램을 쓰는 중이거나 최소화) 새 메시지를 호스트에 알린다.
        //        창이 활성이면 목록 배지와 탭 제목의 안 읽은 수로 충분하므로 알리지 않는다.
        //        알림이 꺼져 있거나 음소거한 방, 안내/삭제 메시지는 대상이 아니다.
        //***************************************************************************
        private void RaiseMessageNotify(ConvEntry c, ConvMessageData msg)
        {
            if (!_settings.NotifyEnabled || _settings.IsMuted(c.ConvId))
                return;
            if (msg.Kind != ConvMessageKind.Text && msg.Kind != ConvMessageKind.Image)
                return;

            Form form = FindForm();
            bool formActive = form != null && form.WindowState != FormWindowState.Minimized && Form.ActiveForm == form;
            if (formActive)
                return;

            string body;
            if (!_settings.ShowPreview)
            {
                body = "새 메시지가 도착했습니다.";
            }
            else
            {
                string text = msg.Kind == ConvMessageKind.Image ? "(사진)" : FirstLine(msg.Message);
                if (text.Length > 80)
                    text = text.Substring(0, 80) + "…";
                body = c.IsGroup ? (msg.SenderNickname + ": " + text) : text;
            }

            MessageNotifyRequested?.Invoke(c.ConvId, DisplayTitle(c), body, _settings.Sound);
        }

        //***************************************************************************
        // @brief 알림을 눌러 들어왔을 때 해당 대화방을 연다(목록에 없으면 무시).
        //***************************************************************************
        public void OpenConversationFromNotification(long convId)
        {
            if (!_convs.ContainsKey(convId))
                return;
            SelectConv(convId);
            ActivationRequested?.Invoke();
        }

        private void RenameSelected()
        {
            if (_client == null || _selected == null || !_selected.IsGroup)
                return;

            using (var dialog = new ConvRenameDialog(_selected.Name))
            {
                if (dialog.ShowDialog(FindForm()) != DialogResult.OK)
                    return;
                if (dialog.NewName == (_selected.Name ?? string.Empty))
                    return;
                _client.RequestConvRename(_selected.ConvId, dialog.NewName);
            }
        }

        private void LeaveSelected()
        {
            if (_client == null || _selected == null)
                return;

            string message = _selected.IsGroup
                ? "이 그룹 대화방을 나가시겠습니까?\r\n나가면 이전 대화를 다시 볼 수 없습니다."
                : "이 대화방을 목록에서 삭제하시겠습니까?\r\n상대가 메시지를 보내면 다시 나타납니다.";
            if (MessageBox.Show(FindForm(), message, "대화방 나가기", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
                return;

            _client.RequestConvLeave(_selected.ConvId);
        }

        private void ShowMembers()
        {
            if (_client == null || _selected == null)
                return;

            _client.RequestConvMembers(_selected.ConvId);

            var lines = new List<string>();
            foreach (ConvMemberData m in _selected.Members.Values.OrderByDescending(x => x.Online).ThenBy(x => x.Nickname))
                lines.Add((IsMe(m.PublicId) ? "(나) " : string.Empty) + m.Nickname + (m.Online ? "  ● 접속 중" : string.Empty));
            if (lines.Count == 0)
                lines.Add("멤버 정보를 불러오는 중입니다...");

            MessageBox.Show(FindForm(), string.Join("\r\n", lines), DisplayTitle(_selected) + " — 멤버 " + _selected.Members.Count + "명",
                MessageBoxButtons.OK, MessageBoxIcon.None);
        }

        //***************************************************************************
        // @brief 지금 이 대화방을 실제로 보고 있으면(탭이 보이고 창이 활성) 마지막 메시지까지 읽음으로 표시한다.
        //***************************************************************************
        private bool IsViewing()
        {
            Form form = FindForm();
            return Visible && form != null && form.WindowState != FormWindowState.Minimized && Form.ActiveForm == form;
        }

        private void MarkReadIfViewing()
        {
            if (_client == null || _selected == null || !IsViewing())
                return;

            ConvEntry c = _selected;
            if (c.LastMsgId <= c.MyLastReadMsgId && c.UnreadCount == 0)
                return;

            _client.RequestConvRead(c.ConvId, c.LastMsgId);
            c.MyLastReadMsgId = Math.Max(c.MyLastReadMsgId, c.LastMsgId);
            c.UnreadCount = 0;
            ConvMemberData me;
            if (c.Members.TryGetValue(_myHex, out me))
                me.LastReadMsgId = Math.Max(me.LastReadMsgId, c.LastMsgId);

            _listConvs.Invalidate();
            RaiseTotalUnread();
        }

        private static string ReasonText(ConvResult reason)
        {
            switch (reason)
            {
                case ConvResult.NotFriends: return "친구가 아닌 사용자입니다.";
                case ConvResult.Blocked: return "차단 관계라서 대화할 수 없습니다.";
                case ConvResult.NotMember: return "참여 중인 대화방이 아닙니다.";
                case ConvResult.NotGroup: return "그룹 대화방에서만 쓸 수 있습니다.";
                case ConvResult.InvalidTarget: return "대화 상대가 올바르지 않습니다.";
                case ConvResult.MemberLimit: return "대화방 최대 인원을 넘었습니다.";
                case ConvResult.InvalidName: return "대화방 이름이 올바르지 않습니다.";
                case ConvResult.InvalidMessage: return "보낼 수 없는 메시지입니다.";
                case ConvResult.InvalidReply: return "답장할 수 없는 메시지입니다.";
                case ConvResult.NotFound: return "이미 삭제되었거나 없는 메시지입니다.";
                case ConvResult.NotSender: return "내가 보낸 메시지만 삭제할 수 있습니다.";
                default: return "서버 오류가 발생했습니다. 잠시 후 다시 시도해 주세요.";
            }
        }

        #endregion

        #region 서버 이벤트 (수신 스레드 -> UI 스레드)

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
            catch (InvalidOperationException)
            {
                // 핸들이 사라지는 중 — 무시.
            }
        }

        private void OnConvOpenResult(ConvOpenResData res)
        {
            Post(() =>
            {
                if (!res.Success)
                {
                    ShowNotice("대화방을 열지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning);
                    return;
                }

                ConvEntry entry = UpsertConv(res.Summary);
                if (entry.SortTime == 0)
                    entry.SortTime = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                SelectConv(entry.ConvId);
                ActivationRequested?.Invoke();
                RaiseTotalUnread();
            });
        }

        private void OnConvUpdatedNotify(ConvSummaryData summary)
        {
            Post(() =>
            {
                bool isNew = !_convs.ContainsKey(summary.ConvId);
                ConvEntry entry = UpsertConv(summary);
                if (isNew && entry.SortTime == 0)
                    entry.SortTime = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
                RefreshConvList();
                if (_selected != null && _selected.ConvId == entry.ConvId)
                    UpdateHeader();
                if (isNew)
                    ShowNotice("새 대화방에 초대되었습니다: " + DisplayTitle(entry), FriendNoticeLevel.Info);
                RaiseTotalUnread();
            });
        }

        private void OnConvListItem(ConvSummaryData summary)
        {
            Post(() =>
            {
                var entry = new ConvEntry();
                ApplySummary(entry, summary);
                _incomingConvs.Add(entry);
            });
        }

        private void OnConvListEnd(ConvListEndData end)
        {
            Post(() =>
            {
                List<ConvEntry> incoming = _incomingConvs;
                _incomingConvs = new List<ConvEntry>();
                _listRefreshPending = false;

                if (!end.Success)
                {
                    ShowNotice("대화방 목록을 불러오지 못했습니다.", FriendNoticeLevel.Error);
                    return;
                }

                // 서버 목록으로 교체하되, 이미 불러둔 메시지/멤버는 같은 대화방이면 이어받는다.
                var merged = new Dictionary<long, ConvEntry>();
                foreach (ConvEntry fresh in incoming)
                {
                    ConvEntry old;
                    if (_convs.TryGetValue(fresh.ConvId, out old))
                    {
                        // 보고 있는 방은 방금 읽음 처리한 값이 서버 값보다 최신일 수 있다.
                        long myRead = Math.Max(old.MyLastReadMsgId, fresh.MyLastReadMsgId);
                        bool viewingThis = _selected != null && _selected.ConvId == old.ConvId && IsViewing();
                        ApplyFresh(old, fresh);
                        old.MyLastReadMsgId = myRead;
                        if (viewingThis)
                            old.UnreadCount = 0;
                        merged[old.ConvId] = old;
                    }
                    else
                    {
                        if (fresh.SortTime == 0)
                            fresh.SortTime = fresh.LastMsgTime;
                        merged[fresh.ConvId] = fresh;
                    }
                }

                _convs.Clear();
                foreach (KeyValuePair<long, ConvEntry> kv in merged)
                    _convs[kv.Key] = kv.Value;

                if (_selected != null)
                    _selected = _convs.TryGetValue(_selected.ConvId, out ConvEntry keep) ? keep : null;

                RefreshConvList();
                ShowSelected();
                if (_selected == null)
                    RebuildMessageItems(keepPosition: false);
                RaiseTotalUnread();
            });
        }

        private static void ApplyFresh(ConvEntry old, ConvEntry fresh)
        {
            old.Type = fresh.Type;
            old.Name = fresh.Name;
            old.MemberCount = fresh.MemberCount;
            old.UnreadCount = fresh.UnreadCount;
            old.LastMsgId = fresh.LastMsgId;
            old.LastMsgTime = fresh.LastMsgTime;
            old.LastKind = fresh.LastKind;
            old.LastMessage = fresh.LastMessage;
            old.MyLastReadMsgId = fresh.MyLastReadMsgId;
            old.PeerPublicId = fresh.PeerPublicId;
            old.PeerNickname = fresh.PeerNickname;
            old.PeerImageUrl = fresh.PeerImageUrl;
            if (fresh.LastMsgTime > 0)
                old.SortTime = fresh.LastMsgTime;
        }

        private void OnConvInviteResult(ConvResultData res)
        {
            Post(() =>
            {
                if (res.Success)
                    ShowNotice("대화상대를 초대했습니다.", FriendNoticeLevel.Info);
                else
                    ShowNotice("초대하지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning);
            });
        }

        private void OnConvLeaveResult(ConvResultData res)
        {
            Post(() =>
            {
                if (!res.Success)
                {
                    ShowNotice("나가지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning);
                    return;
                }

                _convs.Remove(res.ConvId);
                if (_selected != null && _selected.ConvId == res.ConvId)
                    _selected = null;
                RefreshConvList();
                ShowSelected();
                RebuildMessageItems(keepPosition: false);
                RaiseTotalUnread();
            });
        }

        private void OnConvSendResult(ConvResultData res)
        {
            if (res.Success)
                return;

            Post(() => ShowNotice("메시지를 보내지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning));
        }

        private void OnConvRenameResult(ConvResultData res)
        {
            if (res.Success)
                return;     // 반영은 ConvRenamedNotify로 한다

            Post(() => ShowNotice("대화방 이름을 바꾸지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning));
        }

        private void OnConvRenamedNotify(ConvRenamedData data)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(data.ConvId, out c))
                    return;

                c.Name = data.Name ?? string.Empty;
                _listConvs.Invalidate();
                if (_selected == c)
                    UpdateHeader();
            });
        }

        private void OnConvDeleteResult(ConvResultData res)
        {
            if (res.Success)
                return;     // 실제 반영은 ConvDeletedNotify로 한다

            Post(() => ShowNotice("메시지를 삭제하지 못했습니다: " + ReasonText(res.Reason), FriendNoticeLevel.Warning));
        }

        //***************************************************************************
        // @brief 메시지 삭제 알림 — 해당 메시지를 "삭제된 메시지"로 바꾸고, 그 메시지를 인용한 답장과
        //        목록 미리보기, 안 읽은 수도 같이 정리한다.
        //***************************************************************************
        private void OnConvDeletedNotify(ConvDeletedData data)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(data.ConvId, out c))
                    return;

                ConvMessageData target = c.Messages.FirstOrDefault(m => m.MsgId == data.MsgId);
                if (target != null)
                {
                    bool wasCounted = target.Kind == ConvMessageKind.Text || target.Kind == ConvMessageKind.Image;
                    // 아직 읽지 않은 남의 메시지가 지워졌으면 안 읽은 수에서 뺀다(서버 집계와 같은 규칙).
                    if (wasCounted && !IsMe(target.SenderPublicId) && target.MsgId > c.MyLastReadMsgId && c.UnreadCount > 0)
                        c.UnreadCount--;
                    target.Kind = ConvMessageKind.Deleted;
                    target.Message = string.Empty;
                    target.ReplyToMsgId = 0;
                }

                foreach (ConvMessageData m in c.Messages)
                {
                    if (m.ReplyToMsgId == data.MsgId)
                    {
                        m.ReplyKind = ConvMessageKind.Deleted;
                        m.ReplyPreview = string.Empty;
                    }
                }

                if (c.LastMsgId == data.MsgId)
                {
                    c.LastKind = ConvMessageKind.Deleted;
                    c.LastMessage = string.Empty;
                }

                if (_replyTarget != null && _replyTarget.ConvId == data.ConvId && _replyTarget.MsgId == data.MsgId)
                    CancelReply();

                _listConvs.Invalidate();
                if (_selected == c)
                {
                    bool stick = IsNearBottom();
                    RebuildMessageItems(keepPosition: !stick);
                }
                RaiseTotalUnread();
            });
        }

        //***************************************************************************
        // @brief 새 메시지 푸시(내가 보낸 것의 에코 포함). 불러온 메시지 목록에 추가하고 목록 요약을 갱신한다.
        //***************************************************************************
        private void OnConvMessage(ConvMessageData msg)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(msg.ConvId, out c))
                {
                    // 아직 목록에 없는 대화방(상대가 처음 말을 걸었거나, 나갔다가 되살아남) — 목록을 다시 받는다.
                    if (!_listRefreshPending && _client != null)
                    {
                        _listRefreshPending = true;
                        _client.RequestConvList();
                    }
                    return;
                }

                bool added = AddMessage(c, msg);
                if (!added)
                    return;

                bool mine = IsMe(msg.SenderPublicId);
                c.LastMsgId = Math.Max(c.LastMsgId, msg.MsgId);
                c.LastMsgTime = msg.Timestamp;
                c.SortTime = msg.Timestamp;
                c.LastKind = msg.Kind;
                c.LastMessage = msg.Message;

                bool viewingThis = _selected != null && _selected.ConvId == c.ConvId && IsViewing();
                if (mine)
                {
                    c.MyLastReadMsgId = Math.Max(c.MyLastReadMsgId, msg.MsgId);
                }
                else if (!viewingThis && (msg.Kind == ConvMessageKind.Text || msg.Kind == ConvMessageKind.Image))
                {
                    c.UnreadCount++;
                }

                if (!mine && !viewingThis)
                    RaiseMessageNotify(c, msg);

                // 안내 메시지(초대/나감)는 멤버 구성이 바뀐 것이므로 멤버 목록을 다시 받는다.
                if (msg.Kind == ConvMessageKind.System && _client != null)
                    _client.RequestConvMembers(c.ConvId);

                RefreshConvList();
                if (_selected != null && _selected.ConvId == c.ConvId)
                {
                    bool stick = mine || IsNearBottom();
                    RebuildMessageItems(keepPosition: !stick);
                    if (viewingThis)
                        MarkReadIfViewing();
                }
                RaiseTotalUnread();
            });
        }

        // MsgId 오름차순을 유지하며 추가한다. 이미 있으면 false(중복 — 에코/기록 겹침).
        private static bool AddMessage(ConvEntry c, ConvMessageData msg)
        {
            int index = c.Messages.FindIndex(m => m.MsgId >= msg.MsgId);
            if (index >= 0 && c.Messages[index].MsgId == msg.MsgId)
                return false;
            if (index < 0)
                c.Messages.Add(msg);
            else
                c.Messages.Insert(index, msg);
            return true;
        }

        private void OnConvHistoryItem(ConvMessageData msg)
        {
            Post(() =>
            {
                ConvEntry c;
                if (_convs.TryGetValue(msg.ConvId, out c))
                    c.IncomingHistory.Add(msg);
            });
        }

        private void OnConvHistoryEnd(ConvHistoryEndData end)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(end.ConvId, out c) || end.RequestId != c.HistoryRequestId)
                    return;     // 다른 요청으로 넘어간 뒤 늦게 온 응답

                c.HistoryLoading = false;
                List<ConvMessageData> incoming = c.IncomingHistory;
                c.IncomingHistory = new List<ConvMessageData>();

                if (!end.Success)
                {
                    ShowNotice("지난 대화를 불러오지 못했습니다: " + ReasonText(end.Reason), FriendNoticeLevel.Warning);
                    if (_selected == c)
                        RebuildMessageItems(keepPosition: true);
                    return;
                }

                foreach (ConvMessageData m in incoming)
                    AddMessage(c, m);

                bool initial = c.HistoryIsInitial;
                c.HistoryLoaded = true;
                c.HasMore = end.HasMore;

                if (_selected == c)
                {
                    RebuildMessageItems(keepPosition: !initial);
                    if (initial)
                        MarkReadIfViewing();
                }
            });
        }

        private void OnConvReadNotify(ConvReadNotifyData read)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(read.ConvId, out c) || read.ReaderPublicId == null)
                    return;

                ConvMemberData member;
                if (c.Members.TryGetValue(ToHex(read.ReaderPublicId), out member))
                {
                    member.LastReadMsgId = Math.Max(member.LastReadMsgId, read.UpToMsgId);
                    if (_selected == c)
                        _listMsgs.Invalidate();
                }
            });
        }

        private void OnConvMemberItem(ConvMemberData member)
        {
            Post(() =>
            {
                ConvEntry c;
                if (_convs.TryGetValue(member.ConvId, out c))
                    c.IncomingMembers.Add(member);
            });
        }

        private void OnConvMembersEnd(ConvMembersEndData end)
        {
            Post(() =>
            {
                ConvEntry c;
                if (!_convs.TryGetValue(end.ConvId, out c))
                    return;

                List<ConvMemberData> incoming = c.IncomingMembers;
                c.IncomingMembers = new List<ConvMemberData>();
                if (!end.Success)
                    return;

                c.Members = new Dictionary<string, ConvMemberData>();
                foreach (ConvMemberData m in incoming)
                    c.Members[ToHex(m.PublicId)] = m;
                c.MembersLoaded = true;
                c.MemberCount = incoming.Count;

                _listConvs.Invalidate();
                if (_selected == c)
                {
                    UpdateHeader();
                    _listMsgs.Invalidate();
                }
            });
        }

        #endregion

        #region 친구 캐시 (대화 상대 선택용)

        private void OnFriendListItem(FriendItemData item)
        {
            Post(() => _incomingFriends.Add(new FriendInfo
            {
                PublicId = item.PublicId,
                Nickname = item.Nickname,
                ProfileImageUrl = item.ProfileImageUrl,
                Online = item.Online,
            }));
        }

        private void OnFriendListEnd(FriendListEndData end)
        {
            Post(() =>
            {
                List<FriendInfo> incoming = _incomingFriends;
                _incomingFriends = new List<FriendInfo>();
                if (!end.Success)
                    return;

                _friends.Clear();
                foreach (FriendInfo f in incoming)
                    _friends[ToHex(f.PublicId)] = f;
            });
        }

        private void OnFriendAccepted(FriendAcceptedNotifyData data)
        {
            Post(() => _friends[ToHex(data.PublicId)] = new FriendInfo
            {
                PublicId = data.PublicId,
                Nickname = data.Nickname,
                ProfileImageUrl = data.ProfileImageUrl,
                Online = data.Online,
            });
        }

        private void OnFriendRemoved(FriendRemovedNotifyData data)
        {
            Post(() => _friends.Remove(ToHex(data.PublicId)));
        }

        private void OnFriendProfileChanged(FriendProfileChangedNotifyData data)
        {
            Post(() =>
            {
                string hex = ToHex(data.PublicId);
                FriendInfo f;
                if (_friends.TryGetValue(hex, out f))
                {
                    f.Nickname = data.Nickname;
                    f.ProfileImageUrl = data.ProfileImageUrl;
                }

                // 1:1 대화방 목록의 상대 이름/사진도 같이 갱신한다.
                bool changed = false;
                foreach (ConvEntry c in _convs.Values)
                {
                    if (c.Type == ConvType.Direct && c.PeerPublicId != null && ToHex(c.PeerPublicId) == hex)
                    {
                        c.PeerNickname = data.Nickname;
                        c.PeerImageUrl = data.ProfileImageUrl;
                        changed = true;
                    }
                }
                if (changed)
                {
                    _listConvs.Invalidate();
                    UpdateHeader();
                }
            });
        }

        private void OnFriendStatus(FriendStatusNotifyData data)
        {
            Post(() =>
            {
                string hex = ToHex(data.PublicId);
                FriendInfo f;
                if (_friends.TryGetValue(hex, out f))
                    f.Online = data.Online;

                foreach (ConvEntry c in _convs.Values)
                {
                    ConvMemberData m;
                    if (c.Members.TryGetValue(hex, out m))
                        m.Online = data.Online;
                }
            });
        }

        #endregion

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _noticeTimer?.Dispose();
                _convMenu?.Dispose();
                _msgMenu?.Dispose();
                _notifyMenu?.Dispose();
            }
            base.Dispose(disposing);
        }
    }

    //***************************************************************************
    // @brief 메신저 알림 설정(계정별). %AppData%/ChatApp/messenger_{내 id 앞 16자}.cfg 에 key=value로 저장한다.
    // @details 읽기/쓰기 실패(권한, 디스크 등)는 무시하고 기본값/메모리 상태로 계속 동작한다.
    //***************************************************************************
    public class MessengerSettings
    {
        public bool NotifyEnabled = true;
        public bool ShowPreview = true;
        public bool Sound = true;
        private readonly HashSet<long> _muted = new HashSet<long>();

        public bool IsMuted(long convId)
        {
            return _muted.Contains(convId);
        }

        public void SetMuted(long convId, bool muted)
        {
            if (muted)
                _muted.Add(convId);
            else
                _muted.Remove(convId);
        }

        private static string PathFor(string myHex)
        {
            if (string.IsNullOrEmpty(myHex))
                return null;
            string key = myHex.Length > 16 ? myHex.Substring(0, 16) : myHex;
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "ChatApp", "messenger_" + key + ".cfg");
        }

        public static MessengerSettings Load(string myHex)
        {
            var settings = new MessengerSettings();
            try
            {
                string path = PathFor(myHex);
                if (path == null || !File.Exists(path))
                    return settings;

                foreach (string line in File.ReadAllLines(path, Encoding.UTF8))
                {
                    int eq = line.IndexOf('=');
                    if (eq <= 0)
                        continue;
                    string key = line.Substring(0, eq).Trim();
                    string value = line.Substring(eq + 1).Trim();
                    switch (key)
                    {
                        case "notify": settings.NotifyEnabled = value != "0"; break;
                        case "preview": settings.ShowPreview = value != "0"; break;
                        case "sound": settings.Sound = value != "0"; break;
                        case "muted":
                            foreach (string part in value.Split(new[] { ',' }, StringSplitOptions.RemoveEmptyEntries))
                            {
                                long id;
                                if (long.TryParse(part, out id))
                                    settings._muted.Add(id);
                            }
                            break;
                    }
                }
            }
            catch (Exception)
            {
                // 설정 파일이 깨졌거나 읽을 수 없음 — 기본값으로 계속한다.
            }
            return settings;
        }

        public void Save(string myHex)
        {
            try
            {
                string path = PathFor(myHex);
                if (path == null)
                    return;
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllLines(path, new[]
                {
                    "notify=" + (NotifyEnabled ? "1" : "0"),
                    "preview=" + (ShowPreview ? "1" : "0"),
                    "sound=" + (Sound ? "1" : "0"),
                    "muted=" + string.Join(",", _muted),
                }, Encoding.UTF8);
            }
            catch (Exception)
            {
                // 저장 실패는 무시 — 이번 실행 동안은 메모리 값으로 동작한다.
            }
        }
    }

    //***************************************************************************
    // @brief 대화 상대 선택 대화상자의 한 줄.
    //***************************************************************************
    public class ConvPickerItem
    {
        public readonly byte[] PublicId;
        public readonly string Nickname;
        public readonly bool Online;

        public ConvPickerItem(byte[] publicId, string nickname, bool online)
        {
            PublicId = publicId;
            Nickname = nickname;
            Online = online;
        }

        public override string ToString()
        {
            return Nickname + (Online ? "  ● 접속 중" : string.Empty);
        }
    }

    //***************************************************************************
    // @brief 친구를 체크박스로 고르는 대화상자. 새 대화(1명=1:1, 2명 이상=그룹)와 그룹 초대에 쓴다.
    //***************************************************************************
    public class ConvPickerDialog : Form
    {
        private readonly CheckedListBox _list;
        private readonly TextBox _txtName;
        private readonly Label _lblCount;

        public List<byte[]> Selected { get; private set; } = new List<byte[]>();
        public string GroupName { get; private set; } = string.Empty;

        public ConvPickerDialog(string title, string prompt, List<ConvPickerItem> items, bool allowGroupName)
        {
            Text = title;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false;
            MinimizeBox = false;
            ShowInTaskbar = false;
            ClientSize = new Size(340, allowGroupName ? 440 : 400);
            Font = new Font("맑은 고딕", 9f);

            var lblPrompt = new Label { Left = 14, Top = 12, Width = 312, Height = 34, Text = prompt };

            _list = new CheckedListBox
            {
                Left = 14,
                Top = 52,
                Width = 312,
                Height = 270,
                CheckOnClick = true,
                IntegralHeight = false,
            };
            foreach (ConvPickerItem item in items)
                _list.Items.Add(item);
            _list.ItemCheck += (s, e) => BeginInvoke((MethodInvoker)UpdateCount);

            _lblCount = new Label { Left = 14, Top = 328, Width = 312, Height = 18, ForeColor = Color.Gray };

            int y = 352;
            if (allowGroupName)
            {
                _txtName = new TextBox { Left = 14, Top = y, Width = 312, MaxLength = 16 };
                // 2명 이상 골랐을 때만 의미가 있는 입력이라 안내 문구로 표시한다.
                var lblName = new Label { Left = 14, Top = y - 18, Width = 312, Height = 16, Text = "그룹 이름 (선택, 2명 이상일 때)", ForeColor = Color.Gray };
                Controls.Add(lblName);
                y += 34;
            }

            var btnOk = new Button { Text = "확인", Left = 166, Top = y, Width = 76, Height = 30, DialogResult = DialogResult.OK };
            var btnCancel = new Button { Text = "취소", Left = 250, Top = y, Width = 76, Height = 30, DialogResult = DialogResult.Cancel };
            AcceptButton = btnOk;
            CancelButton = btnCancel;

            btnOk.Click += (s, e) =>
            {
                Selected = _list.CheckedItems.Cast<ConvPickerItem>().Select(i => i.PublicId).ToList();
                if (Selected.Count == 0)
                {
                    DialogResult = DialogResult.None;
                    MessageBox.Show(this, "한 명 이상 선택해 주세요.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                if (Selected.Count > ConvConstants.MaxInvite)
                {
                    DialogResult = DialogResult.None;
                    MessageBox.Show(this, "한 번에 최대 " + ConvConstants.MaxInvite + "명까지 선택할 수 있습니다.", Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }
                GroupName = (_txtName != null && Selected.Count >= 2) ? _txtName.Text.Trim() : string.Empty;
            };

            Controls.AddRange(new Control[] { lblPrompt, _list, _lblCount, btnOk, btnCancel });
            if (_txtName != null)
                Controls.Add(_txtName);
            UpdateCount();
        }

        private void UpdateCount()
        {
            _lblCount.Text = "선택: " + _list.CheckedItems.Count + "명";
        }
    }
    //***************************************************************************
    // @brief 그룹 대화방 이름 입력 대화상자. 비워서 확인하면 이름 해제(멤버 이름으로 표시)다.
    //***************************************************************************
    public class ConvRenameDialog : Form
    {
        private readonly TextBox _txt;

        public string NewName { get; private set; } = string.Empty;

        public ConvRenameDialog(string current)
        {
            Text = "대화방 이름 변경";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterParent;
            MaximizeBox = false;
            MinimizeBox = false;
            ShowInTaskbar = false;
            ClientSize = new Size(320, 120);
            Font = new Font("맑은 고딕", 9f);

            var lbl = new Label { Left = 14, Top = 14, Width = 292, Height = 18, Text = "새 이름 (비우면 이름 해제)" };
            _txt = new TextBox { Left = 14, Top = 38, Width = 292, MaxLength = 16, Text = current ?? string.Empty };
            var btnOk = new Button { Text = "확인", Left = 146, Top = 76, Width = 76, Height = 30, DialogResult = DialogResult.OK };
            var btnCancel = new Button { Text = "취소", Left = 230, Top = 76, Width = 76, Height = 30, DialogResult = DialogResult.Cancel };
            AcceptButton = btnOk;
            CancelButton = btnCancel;
            btnOk.Click += (s, e) => NewName = _txt.Text.Trim();

            Controls.AddRange(new Control[] { lbl, _txt, btnOk, btnCancel });
            Shown += (s, e) => _txt.SelectAll();
        }
    }

    //***************************************************************************
    // @brief 사진 원본 보기 창. 창 크기에 맞춰 비율을 유지해 보여 주고 Esc로 닫는다.
    //***************************************************************************
    public class ImageViewerForm : Form
    {
        private readonly PictureBox _picture;
        private readonly Label _status;

        public ImageViewerForm(string url, HttpClient http)
        {
            Text = "사진 보기";
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(720, 540);
            BackColor = Color.Black;
            KeyPreview = true;

            _picture = new PictureBox { Dock = DockStyle.Fill, SizeMode = PictureBoxSizeMode.Zoom, BackColor = Color.Black };
            _status = new Label
            {
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = Color.White,
                BackColor = Color.Black,
                Text = "불러오는 중...",
            };
            Controls.Add(_picture);
            Controls.Add(_status);
            _status.BringToFront();

            KeyDown += (s, e) =>
            {
                if (e.KeyCode == Keys.Escape)
                    Close();
            };
            _picture.Click += (s, e) => Close();

            Load += async (s, e) =>
            {
                try
                {
                    byte[] bytes = await http.GetByteArrayAsync(url);
                    if (IsDisposed)
                        return;
                    using (var ms = new MemoryStream(bytes))
                    using (var original = Image.FromStream(ms))
                        _picture.Image = new Bitmap(original);
                    _status.Visible = false;
                }
                catch (Exception)
                {
                    if (!IsDisposed)
                        _status.Text = "사진을 불러올 수 없습니다.";
                }
            };
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && _picture != null && _picture.Image != null)
            {
                _picture.Image.Dispose();
                _picture.Image = null;
            }
            base.Dispose(disposing);
        }
    }
}