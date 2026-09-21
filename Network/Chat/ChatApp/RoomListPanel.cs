
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
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace ChatApp
{
    public class RoomListPanel : UserControl
    {
        //***************************************************************************
        // @brief 방 목록 한 줄(카드)을 그리는 타일 — ProfileImageGalleryPanel.
        //        GalleryTile과 동일한 역할이지만, 정사각형 썸네일이 아니라
        //        가로로 긴 정보 행이라 별도로 그린다.
        //***************************************************************************
        private class RoomRow : Panel
        {
            private static Color Accent => ChatTheme.Current.Accent;

            public RoomListItemData Item;

            public RoomRow()
            {
                DoubleBuffered = true;
                Cursor = Cursors.Hand;
                Size = new Size(520, 56);
                Margin = new Padding(0, 0, 0, 6);
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

                using (var nameFont = new Font(Font.FontFamily, 10f, FontStyle.Bold))
                using (var nameBrush = new SolidBrush(Color.Black))
                    g.DrawString(Item.Name, nameFont, nameBrush, new PointF(14, 8));

                using (var subFont = new Font(Font.FontFamily, 8.5f))
                using (var subBrush = new SolidBrush(ChatTheme.Current.TextMuted))
                    g.DrawString($"방장: {Item.OwnerNickname}", subFont, subBrush, new PointF(14, 30));

                // 인원수 배지 — 오른쪽 끝.
                string countText = $"{Item.UserCount}명";
                using (var countFont = new Font(Font.FontFamily, 9f, FontStyle.Bold))
                {
                    SizeF countSize = g.MeasureString(countText, countFont);
                    var badgeRect = new RectangleF(Width - countSize.Width - 30, (Height - 22) / 2f, countSize.Width + 16, 22);
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

            RefreshList();
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
                _client = null;
            }

            _flowRooms.Controls.Clear();
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "서버에 접속하면 방 목록을 볼 수 있습니다.";
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
                var row = new RoomRow { Item = data };
                row.Click += (s, e) => TryEnterRoom(data);
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

        private void TryEnterRoom(RoomListItemData item)
        {
            if (_client == null)
                return;

            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "입장 중...";
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
                _client.RequestRoomEnter(data.RoomId);
            });
        }

        //***************************************************************************
        // @brief 방 입장 응답 — 이 패널을 통해 시도한 입장이 성공하면
        //        RoomEntered 이벤트로 방금 조회해둔 목록에서 해당 방 정보를
        //        찾아 넘긴다(RoomEnterResPacket 자체엔 이름/방장이 없어서
        //        이 패널이 보완). 방금 만든 방이라 목록에 아직 없으면
        //        이름만이라도 최소한으로 채운다.
        //***************************************************************************
        private void OnRoomEnterResultReceived(RoomEnterResPacketData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
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