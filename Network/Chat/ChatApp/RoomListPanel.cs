//***************************************************************************
// RoomListPanel.cs : 채팅방 목록 패널 — ProfileImageGalleryPanel.cs와 동일한
//                    설계 패턴을 따르는 탭 내장 UserControl.
//
// [설계] 모달 팝업(Form) 대신 갤러리 탭과 같은 방식으로 메인 탭 하나를
// 차지한다 — AttachClient()/DetachClient()로 서버 연결 생명주기를 따라가고,
// RefreshTheme()으로 스킨 전환에 대응하며, RefreshList()로 탭 전환 시마다
// 최신 목록을 다시 받아온다. 실제 네트워크 요청(RequestListRooms/
// RequestCreateRoom/RequestRoomEnter)은 이 패널이 직접 보내지만, "입장에
// 성공해서 메인 화면(현재 방 이름/방장 표시, 대화 탭으로 전환)을 갱신"하는
// 책임은 ChatClientForm에 남겨둔다 — RoomEntered 이벤트로 위임한다
// (ProfileImageGalleryPanel의 ActiveImageChanged와 같은 역할).
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Net.Http;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ChatApp
{
    public class RoomListPanel : UserControl
    {
        //***************************************************************************
        // @brief 방 목록 한 줄(카드)을 그리는 타일 — ProfileImageGalleryPanel.
        //        GalleryTile과 동일한 역할이지만, 정사각형 썸네일이 아니라
        //        가로로 긴 정보 행이라 별도로 그린다.
        // @details [추가] 왼쪽에 방 프로필 이미지(원형)를 그린다 — 이미지가
        //          없거나(Item.ImageUrl 비어있음) 아직 못 받아왔으면 방
        //          이름 기반 색상+이니셜로 대체한다(ChatClientForm의
        //          DrawAvatar()/RoomAvatarPanel과 같은 시각 규칙).
        //***************************************************************************
        private class RoomRow : Panel
        {
            private static Color Accent => ChatTheme.Current.Accent;
            private static readonly Color[] AvatarPalette =
            {
                Color.FromArgb(255, 107, 107), Color.FromArgb(78, 205, 196), Color.FromArgb(69, 183, 209),
                Color.FromArgb(150, 206, 180), Color.FromArgb(255, 195, 113), Color.FromArgb(162, 155, 254),
                Color.FromArgb(253, 121, 168), Color.FromArgb(129, 236, 236),
            };

            public RoomListItemData Item;
            public RoomListPanel Owner; // 썸네일 캐시/로딩 및 방장 관리 액션 요청용

            private Button _btnManage;
            private ContextMenuStrip _manageMenu;
            private bool _isOwnerRow;

            public RoomRow()
            {
                DoubleBuffered = true;
                Cursor = Cursors.Hand;
                Size = new Size(520, 56);
                Margin = new Padding(0, 0, 0, 6);
            }

            //***************************************************************************
            // @brief [추가] 이 행이 내 소유(방장인 방)인지에 따라 "⚙" 관리
            //        버튼을 보이거나 숨긴다. OnItemReceived()가 Item을 채운
            //        직후 호출한다. 버튼은 실제 자식 컨트롤이라, 이 위에서
            //        클릭해도 RoomRow 자체의 Click(=방 입장)으로는 안 번진다
            //        (WinForms 자식 컨트롤 클릭은 부모로 안 버블링됨).
            //***************************************************************************
            public void ConfigureManageButton(bool isOwner)
            {
                _isOwnerRow = isOwner;

                if (!isOwner)
                {
                    if (_btnManage != null)
                    {
                        Controls.Remove(_btnManage);
                        _btnManage.Dispose();
                        _btnManage = null;
                    }
                    return;
                }

                if (_btnManage == null)
                {
                    _btnManage = new Button
                    {
                        Text = "⚙",
                        Size = new Size(28, 28),
                        FlatStyle = FlatStyle.Flat,
                        Cursor = Cursors.Hand,
                        BackColor = Color.White,
                    };
                    _btnManage.FlatAppearance.BorderSize = 1;
                    _btnManage.FlatAppearance.BorderColor = ChatTheme.Current.Border;
                    _btnManage.Click += (s, e) => ShowManageMenu();
                    Controls.Add(_btnManage);
                }
                _btnManage.Location = new Point(Width - 38, (Height - 28) / 2);
            }

            //***************************************************************************
            // @brief 관리 버튼 클릭 — 이름변경/프로필이미지변경/삭제 메뉴를 띄운다.
            //        이름변경/삭제는 Owner가 직접 처리하고(간단한 요청+확인
            //        대화상자뿐이라), 프로필 이미지 변경은 업로드 절차가
            //        필요해서 ChatClientForm에 이벤트로 위임한다
            //        (Owner.RoomImageEditRequested 참고).
            //***************************************************************************
            private void ShowManageMenu()
            {
                if (_manageMenu == null)
                {
                    _manageMenu = new ContextMenuStrip();
                    _manageMenu.Items.Add("이름 변경", null, (s, e) => Owner?.RequestRenameRoomFromList(Item.RoomId, Item.Name));
                    _manageMenu.Items.Add("프로필 이미지 변경", null, (s, e) => Owner?.RequestRoomImageEditFromList(Item.RoomId));
                    _manageMenu.Items.Add(new ToolStripSeparator());
                    _manageMenu.Items.Add("방 삭제", null, (s, e) => Owner?.RequestDeleteRoomFromList(Item.RoomId, Item.Name));
                }
                _manageMenu.Show(_btnManage, new Point(0, _btnManage.Height));
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaint(e);
                var g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;

                var rect = new Rectangle(1, 1, Width - 3, Height - 3);
                using (var path = RoundedRectPath(rect, 8))
                {
                    using (var b = new SolidBrush(Color.White))
                        g.FillPath(b, path);
                    using (var pen = new Pen(ChatTheme.Current.Border, 1))
                        g.DrawPath(pen, path);
                }

                const int avatarSize = 40;
                var avatarRect = new Rectangle(12, (Height - avatarSize) / 2, avatarSize, avatarSize);
                Image thumbnail = string.IsNullOrEmpty(Item.ImageUrl) ? null : Owner?.GetThumbnail(Item.ImageUrl, this);

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
                    string name = string.IsNullOrEmpty(Item.Name) ? "?" : Item.Name;
                    Color color = AvatarPalette[(uint)name.GetHashCode() % (uint)AvatarPalette.Length];
                    using (var brush = new SolidBrush(color))
                        g.FillEllipse(brush, avatarRect);

                    string initial = name.Substring(0, 1).ToUpperInvariant();
                    using (var initialFont = new Font(Font.FontFamily, 14f, FontStyle.Bold))
                    using (var initialBrush = new SolidBrush(Color.White))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(initial, initialFont, initialBrush, avatarRect, fmt);
                }

                int textLeft = avatarRect.Right + 12;

                using (var nameFont = new Font(Font.FontFamily, 10f, FontStyle.Bold))
                using (var nameBrush = new SolidBrush(Color.Black))
                    g.DrawString(Item.Name, nameFont, nameBrush, new PointF(textLeft, 8));

                using (var subFont = new Font(Font.FontFamily, 8.5f))
                using (var subBrush = new SolidBrush(ChatTheme.Current.TextMuted))
                    g.DrawString($"방장: {Item.OwnerNickname}", subFont, subBrush, new PointF(textLeft, 30));

                // 인원수 배지 — 오른쪽 끝. [수정] 방장 행이면 관리(⚙) 버튼이
                // 그 자리를 차지하므로, 배지를 그만큼(버튼 폭+여백) 왼쪽으로
                // 밀어서 겹치지 않게 한다.
                int rightMargin = _isOwnerRow ? 46 : 10;
                string countText = $"{Item.UserCount}명";
                using (var countFont = new Font(Font.FontFamily, 9f, FontStyle.Bold))
                {
                    SizeF countSize = g.MeasureString(countText, countFont);
                    var badgeRect = new RectangleF(Width - countSize.Width - 16 - rightMargin, (Height - 22) / 2f, countSize.Width + 16, 22);
                    using (var badgePath = RoundedRectPath(Rectangle.Round(badgeRect), 11))
                    using (var badgeBrush = new SolidBrush(ControlPaint.Light(Accent, 0.85f)))
                        g.FillPath(badgeBrush, badgePath);

                    using (var countBrush = new SolidBrush(Accent))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString(countText, countFont, countBrush, badgeRect, fmt);
                }
            }
        }

        private static GraphicsPath RoundedRectPath(Rectangle bounds, int radius)
        {
            int d = radius * 2;
            var path = new GraphicsPath();
            path.AddArc(bounds.X, bounds.Y, d, d, 180, 90);
            path.AddArc(bounds.Right - d, bounds.Y, d, d, 270, 90);
            path.AddArc(bounds.Right - d, bounds.Bottom - d, d, d, 0, 90);
            path.AddArc(bounds.X, bounds.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }

        private ChatNetworkClient _client;

        // [추가] 방 프로필 이미지 썸네일 캐시/로더 — RoomRow.OnPaint()가
        // GetThumbnail()로 조회한다. ChatClientForm.Media.cs의
        // _avatarImageCache/RequestAvatarImage()와 같은 "URL 하나당 한 번만
        // 받아서 캐시" 패턴이지만, 이 패널은 별도 클래스라 그쪽 캐시를
        // 직접 공유할 수 없어서 자체적으로 하나 더 둔다(같은 URL이면
        // 메모리상 이미지가 두 벌 뜨는 셈이지만, 방 아바타는 개수도 적고
        // 흔한 조작도 아니라 실익 대비 복잡도가 안 맞는다고 판단).
        private static readonly HttpClient _thumbnailHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        private readonly Dictionary<string, Image> _thumbnailCache = new Dictionary<string, Image>();
        // [수정 — 버그] 예전엔 HashSet<string>(진행 중 여부만 표시)이었고,
        // 완료 콜백도 "그 fetch를 시작한 딱 한 RoomRow 인스턴스"만 다시
        // 그리게 돼 있었다 — 로딩 도중에 목록이 새로고침되면(로그인 직후
        // 자동 재입장처럼 흔한 타이밍) 그 행 자체가 폐기(Dispose)되고,
        // 같은 URL을 요청한 새 행은 "이미 진행 중"이라 아예 등록도 안 되니
        // fetch가 끝나도 영원히 기본 아바타로 남았다(ChatClientForm의
        // 아바타 캐시와 완전히 같은 버그 패턴). 이제 URL마다 "지금 이
        // 이미지를 기다리는 행들의 목록"을 들고 있다가, 로딩이 끝나면
        // 아직 살아있는(Disposed 아닌) 행 전부를 다시 그리게 한다.
        private readonly Dictionary<string, List<RoomRow>> _thumbnailPendingRows = new Dictionary<string, List<RoomRow>>();

        // [추가] "방 만들기"로 생성한 직후엔 아직 목록에 그 방이 없어서
        // OnRoomEnterResultReceived()가 이름을 못 찾는다 — 응답이 올 때까지
        // 방금 입력한 이름을 잠깐 기억해뒀다가 폴백으로 쓴다.
        private string _pendingCreatedRoomName;

        private Label _lblSectionTitle;
        private Button _btnRefresh;
        private Button _btnCreateRoom;
        private FlowLayoutPanel _flowRooms;
        private Label _lblStatus;

        //***************************************************************************
        // @brief 이 패널 안에서 방 입장에 성공했을 때 발생 — 호출부
        //        (ChatClientForm)가 현재 방 이름/방장 상태를 갱신하고 대화
        //        탭으로 전환하는 걸 담당한다(ProfileImageGalleryPanel.
        //        ActiveImageChanged와 동일한 위임 패턴).
        //***************************************************************************
        public event Action<RoomListItemData> RoomEntered;

        //***************************************************************************
        // @brief [추가] 지금 이 패널이 들고 있는 목록 캐시에서 roomId의 방
        //        정보를 동기적으로 찾아 돌려준다(없으면 null). 서버 왕복
        //        없이 즉시 답한다.
        // @details ChatClientForm.OnRoomEnterResultReceived()가 방 입장
        //          응답을 받는 즉시(비동기 RoomEntered 이벤트를 기다리지
        //          않고) 이름/방장/이미지를 채우는 데 쓴다 — 안 그러면
        //          RoomEnterResultReceived를 구독한 두 핸들러(ChatClientForm
        //          자신과 이 패널) 중 먼저 실행되는 쪽이 아직 안 채워진
        //          값으로 UpdateRoomStatusUI()를 돌려버리는 경합이 있었다
        //          (방 프로필 이미지가 DB엔 있는데 처음엔 기본 아바타로
        //          보이던 버그의 원인).
        //***************************************************************************
        public RoomListItemData TryGetCachedRoomInfo(int roomId)
        {
            foreach (Control control in _flowRooms.Controls)
            {
                if (control is RoomRow row && row.Item.RoomId == roomId)
                    return row.Item;
            }
            return null;
        }

        //***************************************************************************
        // @brief [추가] 내 닉네임 — 각 방 행에서 "내가 그 방의 방장인지"
        //        (Item.OwnerNickname과 비교) 판단해 관리(⚙) 버튼을 보일지
        //        정하는 데 쓴다. ChatClientForm이 로그인 성공 시점과
        //        닉네임 변경 시점마다 갱신해준다. 값이 바뀌면 목록을 다시
        //        그려서 방장 버튼 표시가 즉시 반영되게 한다.
        //***************************************************************************
        public string MyNickname
        {
            get => _myNickname;
            set
            {
                if (_myNickname == value)
                    return;
                _myNickname = value;
                foreach (Control control in _flowRooms.Controls)
                {
                    if (control is RoomRow row)
                        row.ConfigureManageButton(row.Item.OwnerNickname == _myNickname);
                }
            }
        }
        private string _myNickname;

        //***************************************************************************
        // @brief [추가] 관리 메뉴의 "프로필 이미지 변경" 클릭 시 발생 —
        //        업로드 토큰 발급+파일 서버 업로드가 필요해서 이 패널이
        //        직접 처리하지 못하고 ChatClientForm에 위임한다
        //        (UploadRoomImage(int roomId) 참고).
        //***************************************************************************
        public event Action<int> RoomImageEditRequested;

        public RoomListPanel()
        {
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Dock = DockStyle.Fill;
            // [수정] ProfileImageGalleryPanel과 동일한 이유 — 스킨과 무관하게
            // 항상 고정된 밝은 배경(검정 스킨에서 탭 전체가 새까매지는 걸 방지).
            BackColor = ChatTheme.Blue.PageBack;

            var topPanel = new Panel { Dock = DockStyle.Top, Height = 40, Padding = new Padding(10, 6, 10, 4) };

            _lblSectionTitle = new Label
            {
                Text = "채팅방 목록",
                Dock = DockStyle.Left,
                Width = 160,
                TextAlign = ContentAlignment.MiddleLeft,
                Font = new Font(Font.FontFamily, 9f, FontStyle.Bold),
            };

            _btnCreateRoom = new Button { Text = "방 만들기", Dock = DockStyle.Right, Width = 90, Height = 30 };
            StyleActionButton(_btnCreateRoom, filled: true);
            _btnCreateRoom.Click += BtnCreateRoom_Click;

            _btnRefresh = new Button { Text = "새로고침", Dock = DockStyle.Right, Width = 90, Height = 30 };
            StyleDynamicButton(_btnRefresh);
            _btnRefresh.Click += (s, e) => RefreshList();

            topPanel.Controls.AddRange(new Control[] { _lblSectionTitle, _btnCreateRoom, _btnRefresh });

            _lblStatus = new Label
            {
                Dock = DockStyle.Bottom,
                Height = 22,
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = ChatTheme.Current.TextMuted,
                Font = new Font(Font.FontFamily, 8f),
                Text = "서버에 접속하면 방 목록을 볼 수 있습니다.",
            };

            _flowRooms = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                Padding = new Padding(10),
                BackColor = ChatTheme.Blue.PageBack,
            };

            Controls.Add(_flowRooms);
            Controls.Add(_lblStatus);
            Controls.Add(topPanel);
        }

        //***************************************************************************
        // @brief ProfileImageGalleryPanel.StyleDynamicButton()의 사본(별개
        //        클래스라 공유 불가) — 활성/비활성에 따라 자동으로 배경/글씨
        //        색이 바뀌는 아웃라인 버튼.
        //***************************************************************************
        private static void RefreshDynamicButtonColors(Button btn)
        {
            btn.FlatAppearance.BorderSize = 1;

            if (btn.Enabled)
            {
                btn.BackColor = ChatTheme.Current.Accent;
                btn.ForeColor = Color.White;
                btn.FlatAppearance.BorderColor = ChatTheme.Current.Accent;
            }
            else
            {
                btn.BackColor = Color.FromArgb(236, 238, 241);
                btn.ForeColor = ChatTheme.Current.TextMuted;
                btn.FlatAppearance.BorderColor = ChatTheme.Current.Border;
            }
        }

        private static void StyleDynamicButton(Button btn)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.Cursor = Cursors.Hand;
            btn.EnabledChanged += (s, e) => RefreshDynamicButtonColors(btn);
            RefreshDynamicButtonColors(btn);
        }

        private static void StyleActionButton(Button btn, bool filled)
        {
            btn.FlatStyle = FlatStyle.Flat;
            btn.FlatAppearance.BorderSize = filled ? 0 : 1;
            btn.FlatAppearance.BorderColor = ChatTheme.Current.Border;
            btn.Cursor = Cursors.Hand;
            if (filled)
            {
                btn.BackColor = ChatTheme.Current.Accent;
                btn.ForeColor = Color.White;
            }
            else
            {
                btn.BackColor = Color.White;
                btn.ForeColor = ChatTheme.Current.TextSecondary;
            }
        }

        //***************************************************************************
        // @brief 서버 접속에 성공했을 때 호출 — 방 목록 관련 이벤트 구독을
        //        시작하고 목록을 처음 한 번 불러온다(ProfileImageGalleryPanel.
        //        AttachClient()와 동일한 패턴).
        //***************************************************************************
        public void AttachClient(ChatNetworkClient client)
        {
            _client = client;
            _client.RoomListItemReceived += OnItemReceived;
            _client.RoomListEndReceived += OnListEndReceived;
            _client.CreateRoomResultReceived += OnCreateRoomResultReceived;
            _client.RoomEnterResultReceived += OnRoomEnterResultReceived;

            // [추가] 관리(⚙) 메뉴의 이름변경/삭제/이미지변경 요청 결과 —
            // 성공하면 목록을 다시 불러와서 바뀐 내용을 반영한다. 이
            // 패널을 거치지 않고(예: 지금 그 방에 들어가 있는 상태에서
            // 대화 탭의 버튼으로) 같은 종류의 요청을 보냈을 때도 이
            // 핸들러가 똑같이 불리지만, 여기서는 그냥 목록을 새로고침만
            // 하므로(다른 상태를 덮어쓰지 않음) 문제되지 않는다 —
            // ChatClientForm.Events.cs의 CreateRoom 관련 경쟁 상태 설명과
            // 달리, 이쪽은 부작용이 "새로고침"뿐이라 중복돼도 안전하다.
            _client.RenameRoomResultReceived += OnManageActionResultReceived;
            _client.DeleteRoomResultReceived += OnManageActionResultReceived;
            _client.SetRoomImageResultReceived += OnManageActionResultReceived;

            RefreshList();
        }

        //***************************************************************************
        // @brief [추가] 이름변경/삭제/이미지변경 요청이 성공하면 목록을
        //        다시 불러온다. 세 이벤트의 데이터 타입이 달라서
        //        (RenameRoomResData/DeleteRoomResData/SetRoomImageResData)
        //        각각 오버로드로 받되, 전부 Success/Reason 필드는 동일한
        //        모양이라 로직은 한곳에 모았다.
        //***************************************************************************
        //***************************************************************************
        // @brief [수정 — 버그] 이 세 오버로드 전부 다른 핸들러(OnRoomEnterResultReceived
        //        등)와 달리 BeginInvoke() 없이 바로 실행되고 있었다 —
        //        ChatNetworkClient의 이벤트는 네트워크 수신 스레드에서
        //        발생하므로, 그 스레드에서 곧바로 RefreshList()(내부적으로
        //        _flowRooms.Controls.Clear() 등 UI 컨트롤을 건드림)를 호출하면
        //        크로스스레드 예외가 나서 조용히 실패한다 — "채팅방 목록에서
        //        방 이미지를 설정했는데 목록에 반영 안 됨" 버그의 원인이었다.
        //***************************************************************************
        private void OnManageActionResultReceived(RenameRoomResData data)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (data.Success)
                    RefreshList();
            });
        }

        private void OnManageActionResultReceived(DeleteRoomResData data)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (data.Success)
                    RefreshList();
            });
        }

        private void OnManageActionResultReceived(SetRoomImageResData data)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (data.Success)
                    RefreshList();
            });
        }

        //***************************************************************************
        // @brief [추가] 방 목록 행의 관리(⚙) 메뉴 — "이름 변경". 새 이름을
        //        물어보고 RenameRoomReq를 보낸다. 서버가 요청자가 실제
        //        방장인지 다시 검증하므로, 여기서는 별도로 재확인하지
        //        않는다(관리 버튼 자체가 방장에게만 보이긴 하지만, 그
        //        사이 방장이 바뀌었을 수도 있는 경합은 서버가 걸러준다).
        //***************************************************************************
        private void RequestRenameRoomFromList(int roomId, string currentName)
        {
            if (_client == null)
                return;

            string newName = PromptForText(FindForm(), "방 이름 변경", "새 방 이름을 입력하세요:", currentName ?? "");
            if (string.IsNullOrWhiteSpace(newName))
                return;

            _client.RequestRenameRoom(roomId, newName.Trim());
        }

        //***************************************************************************
        // @brief [추가] 관리 메뉴 — "방 삭제". 확인 대화상자 후 DeleteRoomReq.
        //***************************************************************************
        private void RequestDeleteRoomFromList(int roomId, string roomName)
        {
            if (_client == null)
                return;

            if (MessageBox.Show(FindForm(), $"'{roomName}' 방을 삭제할까요?\n방에 있던 모든 사람이 로비로 이동됩니다.",
                "방 삭제", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            {
                return;
            }

            _client.RequestDeleteRoom(roomId);
        }

        //***************************************************************************
        // @brief [추가] 관리 메뉴 — "프로필 이미지 변경". 업로드 절차가
        //        필요해서 직접 처리하지 못하고 ChatClientForm에 위임한다.
        //***************************************************************************
        private void RequestRoomImageEditFromList(int roomId)
        {
            RoomImageEditRequested?.Invoke(roomId);
        }

        //***************************************************************************
        // @brief 연결이 끊겼을 때 호출 — 이벤트 구독을 해지하고 목록을 비운다.
        //***************************************************************************
        public void DetachClient()
        {
            if (_client != null)
            {
                _client.RoomListItemReceived -= OnItemReceived;
                _client.RoomListEndReceived -= OnListEndReceived;
                _client.CreateRoomResultReceived -= OnCreateRoomResultReceived;
                _client.RoomEnterResultReceived -= OnRoomEnterResultReceived;
                _client.RenameRoomResultReceived -= OnManageActionResultReceived;
                _client.DeleteRoomResultReceived -= OnManageActionResultReceived;
                _client.SetRoomImageResultReceived -= OnManageActionResultReceived;
                _client = null;
            }

            _pendingEnterRoomId = null;
            _pendingCreatedRoomName = null;

            _flowRooms.Controls.Clear();
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "서버에 접속하면 방 목록을 볼 수 있습니다.";
        }

        //***************************************************************************
        // @brief [수정 — 버그] RoomRow.OnPaint()가 호출 — url의 썸네일이
        //        캐시에 있으면 즉시 돌려준다. 없으면 이 row를 "기다리는
        //        목록"에 등록만 해두고(이미 다른 row가 같은 URL을 기다리는
        //        중이면 새로 fetch를 또 시작하지 않음), 일단 null을
        //        돌려준다(그 사이엔 RoomRow가 기본 아바타로 대신 그린다).
        //        로딩이 끝나면 그 URL을 기다리던 (아직 살아있는) row 전부를
        //        다시 그리게 한다.
        //***************************************************************************
        private Image GetThumbnail(string url, RoomRow row)
        {
            if (string.IsNullOrEmpty(url))
                return null;

            bool startFetch = false;

            lock (_thumbnailCache)
            {
                if (_thumbnailCache.TryGetValue(url, out Image cached))
                    return cached;

                if (_thumbnailPendingRows.TryGetValue(url, out List<RoomRow> waiters))
                {
                    if (!waiters.Contains(row))
                        waiters.Add(row);
                }
                else
                {
                    _thumbnailPendingRows[url] = new List<RoomRow> { row };
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
                // 무시한다(그 방은 계속 기본 아바타로 보임).
            }
            finally
            {
                List<RoomRow> waiters;
                lock (_thumbnailCache)
                {
                    _thumbnailPendingRows.TryGetValue(url, out waiters);
                    _thumbnailPendingRows.Remove(url);
                }

                if (waiters != null && !IsDisposed && IsHandleCreated)
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        foreach (RoomRow row in waiters)
                        {
                            if (!row.IsDisposed)
                                row.Invalidate();
                        }
                    });
                }
            }
        }

        //***************************************************************************
        // @brief 스킨 전환 시 ChatClientForm이 호출 — 생성 시점에 굳어버린
        //        색상들을 새 ChatTheme.Current로 다시 씌운다. RoomRow 자체는
        //        Accent를 페인트 시점에 다시 읽으므로 Invalidate만 해주면 된다.
        //***************************************************************************
        public void RefreshTheme()
        {
            BackColor = ChatTheme.Blue.PageBack;
            _flowRooms.BackColor = ChatTheme.Blue.PageBack;
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;

            StyleActionButton(_btnCreateRoom, filled: true);
            RefreshDynamicButtonColors(_btnRefresh);

            Invalidate(true);
        }

        //***************************************************************************
        // @brief 방 목록을 서버에 다시 요청한다. 탭을 열 때마다 호출된다.
        //***************************************************************************
        public void RefreshList()
        {
            if (_client == null)
                return;

            _flowRooms.Controls.Clear();
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "불러오는 중...";
            _client.RequestListRooms();
        }

        private void OnItemReceived(RoomListItemData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                var row = new RoomRow { Item = data, Owner = this };
                row.Click += (s, e) => TryEnterRoom(data);
                row.ConfigureManageButton(data.OwnerNickname == _myNickname);
                _flowRooms.Controls.Add(row);
            });
        }

        private void OnListEndReceived(ListRoomsEndResData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                _lblSectionTitle.Text = $"채팅방 목록  {data.TotalCount}개";
                _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
                _lblStatus.Text = data.TotalCount == 0 ? "아직 만들어진 방이 없습니다." : "방을 클릭하면 입장합니다.";
            });
        }

        // [추가] 이 패널이 직접 RequestRoomEnter를 보낸 방 ID — 응답이
        // 왔을 때 "이게 정말 내가 보낸 요청에 대한 응답인지" 확인하는
        // 용도. 자세한 이유는 OnRoomEnterResultReceived() 참고.
        private int? _pendingEnterRoomId;

        private void TryEnterRoom(RoomListItemData item)
        {
            if (_client == null)
                return;

            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "입장 중...";
            _pendingEnterRoomId = item.RoomId;
            _client.RequestRoomEnter(item.RoomId);
        }

        private void BtnCreateRoom_Click(object sender, EventArgs e)
        {
            if (_client == null)
                return;

            string roomName = PromptForText(FindForm(), "방 만들기", "방 이름을 입력하세요:", "");
            if (string.IsNullOrWhiteSpace(roomName))
                return;

            _pendingCreatedRoomName = roomName.Trim();
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "방 생성 중...";
            _client.RequestCreateRoom(_pendingCreatedRoomName);
        }

        private void OnCreateRoomResultReceived(CreateRoomResData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (!data.Success)
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Danger;
                    _lblStatus.Text = "방 생성 실패: " + DescribeRoomResult(data.Reason);
                    return;
                }

                // [수정 — 버그] CreateRoomResultReceived는 이 패널과
                // ChatClientForm.OnCreateRoomResultReceived() 둘 다 구독한다
                // — 채팅 탭의 "방 만들기" 지름길로 생성했을 때도 이 핸들러가
                // 똑같이 불려서, 예전엔 여기서도 무조건 RequestRoomEnter()를
                // 또 보냈다. 이 패널 자신의 _pendingCreatedRoomName이
                // 비어있다는 건 "이 패널의 방 만들기 버튼으로 생성된 게
                // 아니다"라는 뜻이므로, 그 경우엔 아무것도 안 하고
                // ChatClientForm 쪽 처리에 맡긴다(자세한 경쟁 상태 설명은
                // ChatClientForm.Events.cs의 OnCreateRoomResultReceived() 참고).
                if (_pendingCreatedRoomName == null)
                    return;

                _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
                _lblStatus.Text = "방을 만들었습니다. 입장 중...";
                _pendingEnterRoomId = data.RoomId;
                _client.RequestRoomEnter(data.RoomId);
            });
        }

        //***************************************************************************
        // @brief 방 입장 응답 — 이 패널을 통해 시도한 입장이 성공하면
        //        RoomEntered 이벤트로 방금 조회해둔 목록에서 해당 방 정보를
        //        찾아 넘긴다(RoomEnterResPacket 자체엔 이름/방장이 없어서
        //        이 패널이 보완). 방금 만든 방이라 목록에 아직 없으면
        //        이름만이라도 최소한으로 채운다.
        // @details [수정 — 버그] RoomEnterResultReceived는 채팅 탭의 "방 목록"
        //          버튼(BtnRoomList_Click, 그냥 탭 전환만 함)이나 "방 만들기"
        //          지름길처럼 이 패널을 거치지 않은 입장에도 똑같이 불린다.
        //          예전엔 그 경우에도 여기서 무조건 처리해서, 목록에 그
        //          방이 없으면(당연히 없음 — 이 패널이 관여 안 한 입장이니
        //          목록을 조회한 적도 없음) _pendingCreatedRoomName(이 패널
        //          자신의 것, 당연히 비어있음)로 폴백해 "(새 방)"을
        //          ChatClientForm에 덮어씌우는 버그가 있었다(방장 화면에
        //          방 이름이 "(새 방)"으로 보이던 원인). 이제 _pendingEnterRoomId로
        //          "이 응답이 내가 보낸 요청에 대한 것인지"부터 확인하고,
        //          아니면 조용히 무시한다 — 내가 시작한 입장이 아니면 이
        //          패널이 관여할 일이 없다.
        //***************************************************************************
        private void OnRoomEnterResultReceived(RoomEnterResPacketData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (_pendingEnterRoomId != data.RoomId)
                    return; // 이 패널이 보낸 요청에 대한 응답이 아님 — 관여하지 않음

                _pendingEnterRoomId = null;

                if (!data.Success)
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Danger;
                    _lblStatus.Text = "입장 실패";
                    return;
                }

                RoomListItemData matched = null;
                foreach (Control control in _flowRooms.Controls)
                {
                    if (control is RoomRow row && row.Item.RoomId == data.RoomId)
                    {
                        matched = row.Item;
                        break;
                    }
                }

                var entered = matched ?? new RoomListItemData { RoomId = data.RoomId, Name = _pendingCreatedRoomName ?? "(새 방)", OwnerNickname = "", UserCount = data.RoomUserCount };
                _pendingCreatedRoomName = null;
                RoomEntered?.Invoke(entered);
            });
        }

        private static string DescribeRoomResult(RoomResult reason)
        {
            switch (reason)
            {
                case RoomResult.InvalidRoomId: return "존재하지 않는 방입니다.";
                case RoomResult.RoomNotFound: return "존재하지 않는 방입니다.";
                case RoomResult.NotOwner: return "방장만 할 수 있습니다.";
                case RoomResult.RoomLimitExceeded: return "생성 가능한 방 개수를 초과했습니다.";
                case RoomResult.InvalidName: return "방 이름이 올바르지 않습니다.";
                case RoomResult.DbError: return "서버 오류가 발생했습니다.";
                default: return reason.ToString();
            }
        }

        //***************************************************************************
        // @brief 간단한 한 줄 텍스트 입력 대화상자 — NicknameChangeDialog처럼
        //        별도 Form 클래스를 만드는 대신 즉석에서 구성한 가벼운 Form.
        //***************************************************************************
        private static string PromptForText(IWin32Window owner, string title, string prompt, string defaultValue)
        {
            using (var dlg = new Form
            {
                Text = title,
                ClientSize = new Size(320, 120),
                FormBorderStyle = FormBorderStyle.FixedDialog,
                MaximizeBox = false,
                MinimizeBox = false,
                StartPosition = FormStartPosition.CenterParent,
                ShowInTaskbar = false,
            })
            {
                var lbl = new Label { Text = prompt, Left = 10, Top = 10, Width = 300 };
                var txt = new TextBox { Left = 10, Top = 32, Width = 300, Text = defaultValue };
                var btnOk = new Button { Text = "확인", Left = 134, Top = 68, Width = 80, Height = 28, DialogResult = DialogResult.OK };
                var btnCancel = new Button { Text = "취소", Left = 220, Top = 68, Width = 80, Height = 28, DialogResult = DialogResult.Cancel };

                dlg.Controls.AddRange(new Control[] { lbl, txt, btnOk, btnCancel });
                dlg.AcceptButton = btnOk;
                dlg.CancelButton = btnCancel;

                return dlg.ShowDialog(owner) == DialogResult.OK ? txt.Text : null;
            }
        }
    }
}