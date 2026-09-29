
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ChatApp
{
    //***************************************************************************
    // @brief 채팅 화면의 대화 탭 안에서, 채팅 목록 자리를 덮는 오버레이로 표시되는
    //        방 멤버 목록(프로필 이미지 + 닉네임 + 접속 상태) 패널.
    // @details [수정 — 팝업 창에서 오버레이 패널로] 예전엔 별도 Form(ShowDialog())
    //          이었다 — 그런데 채팅 화면 자체가 고정 좌표 레이아웃이라 옆에
    //          나란히 둘 공간이 없고, 매번 새 창을 띄우는 방식이 "채팅방 탭
    //          안에서 멤버보기 버튼으로 바로 보이는" 자연스러운 흐름과도
    //          맞지 않았다. 이제 ChatClientForm이 이 패널을 채팅 목록
    //          (_listBoxChat)과 정확히 같은 자리에 미리 만들어 얹어두고
    //          (Visible=false로 시작), 멤버보기 버튼을 누르면 이 패널이
    //          앞으로 나와 채팅 목록을 가리는 방식이다 — RoomListPanel이
    //          탭 페이지로 상주하는 것과 같은 구조를 "오버레이"로 응용한
    //          것뿐이다.
    //
    //          AttachClient()/DetachClient()는 RoomListPanel/
    //          ProfileImageGalleryPanel과 동일한 패턴 — 서버 접속(로그인
    //          성공) 시점에 이벤트 구독을 시작하고, 연결이 끊기면 해제한다.
    //          Open(roomId, myPublicId)는 멤버보기 버튼을 누를 때마다
    //          호출해서 대상 방을 지정하고 목록을 새로 불러온다.
    //
    //          RoomListPanel의 RoomRow/GetThumbnail() 패턴(원형 클리핑,
    //          이니셜 기본 아바타, URL당 한 번만 받아서 캐시)을 그대로
    //          따른다 — 이 패널은 별도 클래스라 그쪽 캐시를 공유하지
    //          못해 자체적으로 하나 더 둔다(RoomListPanel의 같은 설계
    //          판단과 동일한 이유).
    //***************************************************************************
    public class RoomMemberListPanel : Panel
    {
        private static readonly Color[] AvatarPalette =
        {
            Color.FromArgb(255, 107, 107), Color.FromArgb(78, 205, 196), Color.FromArgb(69, 183, 209),
            Color.FromArgb(150, 206, 180), Color.FromArgb(255, 195, 113), Color.FromArgb(162, 155, 254),
            Color.FromArgb(253, 121, 168), Color.FromArgb(129, 236, 236),
        };

        private const int PageSize = 20;

        private ChatNetworkClient _client;
        private int _roomId;
        private byte[] _myPublicId;
        private int _requestId;
        private RoomMemberListScope _scope = RoomMemberListScope.Present;
        private int _page;
        private int _totalPages = 1;

        private FlowLayoutPanel _flowMembers;
        private Label _lblTitle;
        private Button _btnClose;
        private Label _lblStatus;
        private Button _btnScopePresent;
        private Button _btnScopeHistory;
        private Button _btnPrev;
        private Button _btnNext;
        private Label _lblPage;

        //***************************************************************************
        // @brief 이 패널이 지금 화면에 나와 있는지(멤버보기가 열려 있는지) 여부.
        //        채팅 화면이 이 값을 보고 방을 옮길 때 자동으로 닫을지 판단한다.
        //***************************************************************************
        public bool IsOpen => Visible;

        //***************************************************************************
        // @brief 이 패널이 닫혔을 때(✕ 버튼 클릭) 발생. 이 패널을 다른 화면(예:
        //        RoomListPanel) 안에 얹어서 쓰는 호스트가, 닫힌 뒤 자기 자신의
        //        원래 화면(목록 등)을 다시 보여줘야 할 때 이 이벤트로 알 수 있다.
        //***************************************************************************
        public event Action Closed;

        private static readonly HttpClient _thumbnailHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        private readonly Dictionary<string, Image> _thumbnailCache = new Dictionary<string, Image>();
        private readonly Dictionary<string, List<MemberRow>> _thumbnailPendingRows = new Dictionary<string, List<MemberRow>>();

        public RoomMemberListPanel()
        {
            InitializeComponents();
        }

        //***************************************************************************
        // @brief 서버 접속(로그인 성공) 시점에 호출 — 이벤트 구독을 시작한다.
        //        RoomListPanel.AttachClient()와 동일한 패턴.
        //***************************************************************************
        public void AttachClient(ChatNetworkClient client)
        {
            _client = client;
            _client.RoomMemberListItemReceived += OnMemberItemReceived;
            _client.RoomMemberListEndReceived += OnMemberListEndReceivedCore;
        }

        //***************************************************************************
        // @brief 연결이 끊겼을 때 호출 — 이벤트 구독을 해제하고 패널을 숨긴다.
        //***************************************************************************
        public void DetachClient()
        {
            if (_client != null)
            {
                _client.RoomMemberListItemReceived -= OnMemberItemReceived;
                _client.RoomMemberListEndReceived -= OnMemberListEndReceivedCore;
                _client = null;
            }

            Visible = false;
            _flowMembers.Controls.Clear();
            _page = 0;
            _totalPages = 1;
            _requestId = 0;
        }

        //***************************************************************************
        // @brief 멤버보기 버튼을 눌렀을 때 호출 — 대상 방을 지정하고 패널을
        //        앞으로 꺼낸 뒤 첫 페이지를 요청한다.
        // @param roomId 지금 자신이 있는 방(로비 포함)
        // @param myPublicId 로그인 응답으로 받은 내 PublicId — "나" 표시용
        //***************************************************************************
        public void Open(int roomId, byte[] myPublicId)
        {
            if (_client == null)
                return;

            _roomId = roomId;
            _myPublicId = myPublicId;
            _scope = RoomMemberListScope.Present;
            UpdateScopeButtons();

            Visible = true;
            BringToFront();

            RequestPage(0);
        }

        //***************************************************************************
        // @brief 패널을 닫는다(채팅 목록으로 돌아감). 이벤트 구독은 유지한다 —
        //        DetachClient()는 연결이 끊길 때만 호출된다.
        //***************************************************************************
        public void Close()
        {
            Visible = false;
            _flowMembers.Controls.Clear();
            Closed?.Invoke();
        }

        //***************************************************************************
        // @brief 지정한 페이지를 서버에 요청한다.
        //***************************************************************************
        private void RequestPage(int page)
        {
            if (_client == null)
                return;

            if (page < 0)
                page = 0;

            _flowMembers.Controls.Clear();
            _lblStatus.Text = "불러오는 중...";
            _btnPrev.Enabled = false;
            _btnNext.Enabled = false;

            _requestId = _client.RequestRoomMemberList(_roomId, _scope, page, PageSize);
        }

        private void InitializeComponents()
        {
            BackColor = Color.White;
            Visible = false;

            var titleBar = new Panel { Dock = DockStyle.Top, Height = 36 };
            _lblTitle = new Label
            {
                Dock = DockStyle.Fill,
                Text = "참여 인원",
                Font = new Font(Font.FontFamily, 11f, FontStyle.Bold),
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(12, 0, 0, 0),
            };
            _btnClose = new Button { Text = "✕", Dock = DockStyle.Right, Width = 36, FlatStyle = FlatStyle.Flat };
            _btnClose.FlatAppearance.BorderSize = 0;
            _btnClose.Click += (s, e) => Close();
            titleBar.Controls.Add(_lblTitle);
            titleBar.Controls.Add(_btnClose);

            var scopePanel = new Panel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(12, 4, 12, 4) };
            _btnScopePresent = new Button { Text = "현재 참여자", Left = 0, Top = 0, Width = 100, Height = 30 };
            _btnScopeHistory = new Button { Text = "전체 참여 이력", Left = 106, Top = 0, Width = 100, Height = 30 };
            _btnScopePresent.Click += (s, e) => SwitchScope(RoomMemberListScope.Present);
            _btnScopeHistory.Click += (s, e) => SwitchScope(RoomMemberListScope.History);
            scopePanel.Controls.AddRange(new Control[] { _btnScopePresent, _btnScopeHistory });
            UpdateScopeButtons();

            _lblStatus = new Label
            {
                Dock = DockStyle.Top,
                Height = 24,
                Text = "불러오는 중...",
                ForeColor = ChatTheme.Current.TextMuted,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(12, 0, 0, 0),
            };

            _flowMembers = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                Padding = new Padding(8),
            };

            var pagerPanel = new Panel { Dock = DockStyle.Bottom, Height = 40 };
            _btnPrev = new Button { Text = "◀ 이전", Width = 80, Height = 28 };
            _btnNext = new Button { Text = "다음 ▶", Width = 80, Height = 28 };
            _lblPage = new Label
            {
                Text = "1 / 1",
                Width = 80,
                Height = 28,
                TextAlign = ContentAlignment.MiddleCenter,
                Font = new Font(Font.FontFamily, 9f, FontStyle.Bold),
            };
            _btnPrev.Enabled = false;
            _btnNext.Enabled = false;
            _btnPrev.Click += (s, e) => RequestPage(_page - 1);
            _btnNext.Click += (s, e) => RequestPage(_page + 1);
            pagerPanel.Controls.AddRange(new Control[] { _btnPrev, _lblPage, _btnNext });
            pagerPanel.Resize += (s, e) => LayoutPager(pagerPanel);

            Controls.Add(_flowMembers);
            Controls.Add(pagerPanel);
            Controls.Add(_lblStatus);
            Controls.Add(scopePanel);
            Controls.Add(titleBar);

            LayoutPager(pagerPanel);
        }

        //***************************************************************************
        // @brief 탭 버튼의 선택 표시(칠해진/테두리만)를 갱신한다.
        //***************************************************************************
        private void UpdateScopeButtons()
        {
            StyleScopeButton(_btnScopePresent, _scope == RoomMemberListScope.Present);
            StyleScopeButton(_btnScopeHistory, _scope == RoomMemberListScope.History);
        }

        private static void StyleScopeButton(Button btn, bool selected)
        {
            btn.FlatStyle = FlatStyle.Flat;
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
        // @brief 현재 참여자 / 전체 참여 이력 탭을 전환한다. 첫 페이지부터
        //        다시 조회한다.
        //***************************************************************************
        private void SwitchScope(RoomMemberListScope scope)
        {
            if (_scope == scope)
                return;

            _scope = scope;
            UpdateScopeButtons();
            RequestPage(0);
        }

        //***************************************************************************
        // @brief 페이지 이동 줄의 [이전][페이지 표시][다음]을 가운데 정렬한다.
        //***************************************************************************
        private void LayoutPager(Panel pagerPanel)
        {
            int totalWidth = _btnPrev.Width + _lblPage.Width + _btnNext.Width + 16;
            int left = Math.Max(0, (pagerPanel.ClientSize.Width - totalWidth) / 2);
            int top = Math.Max(0, (pagerPanel.ClientSize.Height - _btnPrev.Height) / 2);
            _btnPrev.Location = new Point(left, top);
            _lblPage.Location = new Point(left + _btnPrev.Width + 8, top);
            _btnNext.Location = new Point(left + _btnPrev.Width + 8 + _lblPage.Width + 8, top);
        }

        private void OnMemberItemReceived(RoomMemberItemData data)
        {
            // 이 패널은 ChatClientForm이 로드될 때 미리 만들어 얹어두므로
            // Open() 시점엔 이미 창 핸들이 있다 — 그래도 방어적으로 확인한다
            // (팝업 창이던 시절 실제로 이 경합으로 서버 연결이 끊긴 적이 있었다:
            // 창 핸들이 없는 상태에서 BeginInvoke()가 예외를 던지면 그게
            // 네트워크 수신 스레드에서 처리되지 않은 채로 올라가 수신 루프
            // 자체가 죽었다).
            if (IsDisposed || !IsHandleCreated)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (IsDisposed || data.RequestId != _requestId)
                    return;

                bool isMe = _myPublicId != null && data.PublicId != null
                    && _myPublicId.Length == data.PublicId.Length
                    && _myPublicId.SequenceEqual(data.PublicId);

                var row = new MemberRow(this, data, isMe);
                _flowMembers.Controls.Add(row);
            });
        }

        private void OnMemberListEndReceivedCore(RoomMemberListEndResData data)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (IsDisposed || data.RequestId != _requestId)
                    return;

                int pageSize = data.PageSize > 0 ? data.PageSize : PageSize;
                _page = data.Page;
                _totalPages = Math.Max(1, (data.TotalCount + pageSize - 1) / pageSize);

                _lblPage.Text = $"{_page + 1} / {_totalPages}";
                _btnPrev.Enabled = _page > 0;
                _btnNext.Enabled = _page + 1 < _totalPages;

                if (data.TotalCount == 0)
                {
                    _lblStatus.Text = data.Scope == RoomMemberListScope.Present
                        ? "지금 아무도 없습니다."
                        : "입장한 적이 있는 사람이 없습니다.";
                }
                else
                {
                    _lblStatus.Text = data.Scope == RoomMemberListScope.Present
                        ? $"지금 {data.TotalCount}명 접속 중"
                        : $"총 {data.TotalCount}명 (입장 이력 기준)";
                }
            });
        }

        //***************************************************************************
        // @brief RoomListPanel.GetThumbnail()과 동일한 패턴 — 캐시에 있으면 즉시
        //        반환, 없으면 비동기로 받아오고 그 동안은 null(기본 아바타)을
        //        반환한다. 로딩이 끝나면 기다리던 행들을 다시 그리게 한다.
        //***************************************************************************
        private Image GetThumbnail(string url, MemberRow row)
        {
            if (string.IsNullOrEmpty(url))
                return null;

            bool startFetch = false;

            lock (_thumbnailCache)
            {
                if (_thumbnailCache.TryGetValue(url, out Image cached))
                    return cached;

                if (_thumbnailPendingRows.TryGetValue(url, out List<MemberRow> waiters))
                {
                    if (!waiters.Contains(row))
                        waiters.Add(row);
                }
                else
                {
                    _thumbnailPendingRows[url] = new List<MemberRow> { row };
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
                // 네트워크 오류, 잘못된 URL, 이미지 디코딩 실패 등 — 조용히
                // 무시한다(그 사람은 계속 기본 아바타로 보임).
            }
            finally
            {
                List<MemberRow> waiters;
                lock (_thumbnailCache)
                {
                    _thumbnailPendingRows.TryGetValue(url, out waiters);
                    _thumbnailPendingRows.Remove(url);
                }

                if (waiters != null && !IsDisposed && IsHandleCreated)
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        foreach (MemberRow row in waiters)
                        {
                            if (!row.IsDisposed)
                                row.Invalidate();
                        }
                    });
                }
            }
        }

        //***************************************************************************
        // @brief 멤버 한 명(아바타 + 닉네임)을 그리는 행. RoomListPanel.RoomRow와
        //        같은 방식(원형 클리핑, 이니셜 기본 아바타)이다.
        //***************************************************************************
        private class MemberRow : Panel
        {
            private readonly RoomMemberListPanel _owner;
            private readonly RoomMemberItemData _item;
            private readonly bool _isMe;

            public MemberRow(RoomMemberListPanel owner, RoomMemberItemData item, bool isMe)
            {
                _owner = owner;
                _item = item;
                _isMe = isMe;

                DoubleBuffered = true;
                Size = new Size(300, 56); // 이름 줄 + 접속 상태 줄, 두 줄을 표시하려고 기존보다 조금 늘림
                Margin = new Padding(0, 0, 0, 4);
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaint(e);
                var g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;

                if (_isMe)
                {
                    using (var b = new SolidBrush(ControlPaint.Light(ChatTheme.Current.Accent, 0.9f)))
                        g.FillRectangle(b, new Rectangle(0, 0, Width, Height));
                }

                const int avatarSize = 36;
                var avatarRect = new Rectangle(8, (Height - avatarSize) / 2, avatarSize, avatarSize);
                Image thumbnail = string.IsNullOrEmpty(_item.ProfileImageUrl) ? null : _owner.GetThumbnail(_item.ProfileImageUrl, this);

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
                    string name = string.IsNullOrEmpty(_item.Nickname) ? "?" : _item.Nickname;
                    Color color = AvatarPalette[(uint)name.GetHashCode() % (uint)AvatarPalette.Length];
                    using (var brush = new SolidBrush(color))
                        g.FillEllipse(brush, avatarRect);

                    string initial = name.Substring(0, 1).ToUpperInvariant();
                    using (var initialFont = new Font(Font.FontFamily, 12f, FontStyle.Bold))
                    using (var initialBrush = new SolidBrush(Color.White))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(initial, initialFont, initialBrush, avatarRect, fmt);
                }

                string displayName = _isMe ? $"{_item.Nickname} (나)" : _item.Nickname;
                int textLeft = avatarRect.Right + 10;
                using (var nameFont = new Font(Font.FontFamily, 10f, _isMe ? FontStyle.Bold : FontStyle.Regular))
                using (var nameBrush = new SolidBrush(_item.Online ? Color.Black : ChatTheme.Current.TextMuted))
                    g.DrawString(displayName, nameFont, nameBrush, new PointF(textLeft, 6));

                // 접속 상태 — 초록 점 + "온라인"/회색 점 + "오프라인". 이 방에
                // 지금 있는지가 아니라 서버 어딘가에 로그인해 있는지를 뜻한다
                // (RoomMemberItemData.Online 설명 참고).
                Color statusColor = _item.Online ? Color.FromArgb(47, 184, 112) : ChatTheme.Current.TextMuted;
                string statusText = _item.Online ? "온라인" : "오프라인";
                const int dotSize = 8;
                var dotRect = new Rectangle(textLeft, 27, dotSize, dotSize);
                using (var dotBrush = new SolidBrush(statusColor))
                    g.FillEllipse(dotBrush, dotRect);

                using (var statusFont = new Font(Font.FontFamily, 8f))
                using (var statusBrush = new SolidBrush(statusColor))
                    g.DrawString(statusText, statusFont, statusBrush, new PointF(dotRect.Right + 6, 24));
            }
        }
    }
}