
//***************************************************************************
// ProfileImageGalleryPanel.cs : 프로필 이미지 갤러리 패널 (카카오톡 스타일).
//
// [설계] 상단에 현재 대표 이미지를 크게 보여주고(클릭하면 카메라 배지로
// 업로드 메뉴), 하단에 전체 사진을 3열 정사각형 썸네일 그리드로 보여준다.
// 실제 업로드/URL설정/로컬파일/해제 네트워크 로직은 이 패널이 갖고 있지
// 않다(그건 ChatClientForm 소유) — 카메라 배지를 누르면 이벤트만 올려보내고,
// ChatClientForm이 기존 메서드(UploadMyProfileImage() 등, 채팅 탭 프로필
// 사진 메뉴와 동일)를 그대로 재사용해서 처리한다. 처리 결과는
// SetActiveImagePreview()로 다시 이 패널에 통보된다.
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
    public class ProfileImageGalleryPanel : UserControl
    {
        private class GalleryItem
        {
            public long ImageId;
            public string ImageRef;
            public bool IsActive;
            public Image Thumbnail;
        }

        //***************************************************************************
        // @brief 그리드의 사진 한 장(또는 맨 끝의 "+" 추가 칸)을 그리는 타일.
        //        Item이 null이면 "+" 추가 칸으로 그린다.
        //***************************************************************************
        private class GalleryTile : Panel
        {
            private static Color Accent => ChatTheme.Current.Accent;

            public GalleryItem Item;
            public bool IsSelected;
            public bool IsAddTile => Item == null;

            public GalleryTile()
            {
                DoubleBuffered = true;
                Cursor = Cursors.Hand;
                Size = new Size(160, 160);
                Margin = new Padding(4);
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaint(e);
                var g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;

                var rect = new Rectangle(1, 1, Width - 3, Height - 3);
                using (var path = RoundedRectPath(rect, 12))
                {
                    if (IsAddTile)
                    {
                        using (var b = new SolidBrush(Color.White))
                            g.FillPath(b, path);
                        using (var pen = new Pen(Color.FromArgb(216, 220, 226), 1) { DashStyle = DashStyle.Dash })
                            g.DrawPath(pen, path);

                        int cx = Width / 2, cy = Height / 2, s = Math.Min(Width, Height) / 6;
                        using (var pen = new Pen(ChatTheme.Current.TextMuted, 2))
                        {
                            g.DrawLine(pen, cx - s, cy, cx + s, cy);
                            g.DrawLine(pen, cx, cy - s, cx, cy + s);
                        }
                        return;
                    }

                    if (Item.Thumbnail != null)
                    {
                        var oldClip = g.Clip;
                        g.SetClip(path, CombineMode.Intersect);
                        g.DrawImage(Item.Thumbnail, rect);
                        g.Clip = oldClip;
                    }
                    else
                    {
                        using (var b = new SolidBrush(Color.FromArgb(240, 242, 245)))
                            g.FillPath(b, path);
                    }

                    if (IsSelected)
                    {
                        using (var overlay = new SolidBrush(Color.FromArgb(70, Accent)))
                            g.FillPath(overlay, path);
                    }

                    if (Item.IsActive || IsSelected)
                    {
                        using (var pen = new Pen(Accent, 2))
                            g.DrawPath(pen, path);
                    }
                }

                if (Item.IsActive)
                {
                    var badgeRect = new RectangleF(5, 5, 34, 16);
                    using (var path = RoundedRectPath(Rectangle.Round(badgeRect), 8))
                    using (var b = new SolidBrush(Accent))
                        g.FillPath(b, path);
                    using (var f = new Font(Font.FontFamily, 7.5f, FontStyle.Bold))
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                        g.DrawString("대표", f, Brushes.White, badgeRect, fmt);
                }

                if (IsSelected)
                {
                    int cx = Width - 17, cy = 17, r = 9;
                    using (var b = new SolidBrush(Accent))
                        g.FillEllipse(b, cx - r, cy - r, r * 2, r * 2);
                    using (var pen = new Pen(Color.White, 2))
                    {
                        g.DrawLine(pen, cx - 4, cy, cx - 1, cy + 3);
                        g.DrawLine(pen, cx - 1, cy + 3, cx + 4, cy - 3);
                    }
                }
            }
        }

        //***************************************************************************
        // @brief 상단 대표 이미지 옆에 겹쳐 그리는 원형 카메라 배지 버튼.
        //***************************************************************************
        private class CircleBadgeButton : Button
        {
            public CircleBadgeButton()
            {
                FlatStyle = FlatStyle.Flat;
                FlatAppearance.BorderSize = 0;
                BackColor = ChatTheme.Current.Accent;
                Cursor = Cursors.Hand;
                DoubleBuffered = true;
            }

            //***************************************************************************
            // @brief 스킨 전환 시 ChatClientForm이 호출 — BackColor는 생성 시점에
            //        굳어버리는 실제 프로퍼티라 페인트만으로는 안 바뀐다.
            //***************************************************************************
            public void RefreshThemeColor()
            {
                BackColor = ChatTheme.Current.Accent;
                Invalidate();
            }

            protected override void OnResize(EventArgs e)
            {
                base.OnResize(e);
                var path = new GraphicsPath();
                path.AddEllipse(0, 0, Width, Height);
                Region = new Region(path);
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                var g = e.Graphics;
                g.SmoothingMode = SmoothingMode.AntiAlias;
                using (var b = new SolidBrush(BackColor))
                    g.FillEllipse(b, 0, 0, Width - 1, Height - 1);

                using (var pen = new Pen(Color.White, 2))
                {
                    float bw = Width * 0.5f, bh = Height * 0.34f;
                    var bodyRect = new RectangleF((Width - bw) / 2f, (Height - bh) / 2f + Height * 0.06f, bw, bh);
                    g.DrawRectangle(pen, bodyRect.X, bodyRect.Y, bodyRect.Width, bodyRect.Height);

                    float lensR = Width * 0.12f;
                    g.DrawEllipse(pen, Width / 2f - lensR, Height / 2f - lensR + Height * 0.02f, lensR * 2, lensR * 2);
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
        private readonly HttpClient _httpClient;

        private PictureBox _picActive;
        private CircleBadgeButton _btnCameraBadge;
        private Label _lblActiveCaption;
        private Label _lblSectionTitle;
        private FlowLayoutPanel _flowThumbnails;
        private Button _btnSelect;
        private Button _btnDelete;
        private Label _lblStatus;

        private readonly List<GalleryItem> _items = new List<GalleryItem>();
        // [수정 — 멀티 삭제] 단일 선택(_selectedTile)에서 다중 선택으로 확장.
        // "대표로 지정"은 여전히 하나만 골랐을 때만 의미가 있어서(대표는
        // 항상 한 장뿐이므로), 그 버튼은 개수가 정확히 1개일 때만 활성화한다.
        private readonly HashSet<GalleryTile> _selectedTiles = new HashSet<GalleryTile>();

        // [추가] 삭제 요청은 서버 응답(DeleteProfileImageResData)에 어떤
        // 이미지에 대한 것인지 식별자가 없다 — Success/Reason뿐이다. 여러
        // 장을 한꺼번에 보내면 어떤 응답이 어떤 요청 것인지 구분할 수
        // 없으므로, 하나씩 순서대로 보내고 응답을 받은 뒤에야 다음 걸
        // 보내는 큐로 처리한다(느리지만 정확하다 — 몇 장 안 되는 갤러리
        // 삭제에서 체감 지연은 미미함).
        private readonly Queue<long> _pendingDeleteQueue = new Queue<long>();
        private int _deleteSuccessCount;
        private int _deleteFailCount;
        private bool _pendingDeleteWasActive;

        //***************************************************************************
        // @brief 선택/삭제가 성공해서 대표 이미지가 바뀔 때마다 발생. 인자는
        //        새 대표 이미지의 image_ref(해제된 경우 빈 문자열).
        //***************************************************************************
        public event Action<string> ActiveImageChanged;

        // [추가] 카메라 배지 메뉴 — 실제 처리는 ChatClientForm에 위임.
        public event Action UploadRequested;
        public event Action SetUrlRequested;
        public event Action LocalFileRequested;
        public event Action ClearRequested;

        public ProfileImageGalleryPanel(HttpClient httpClient)
        {
            _httpClient = httpClient;
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Dock = DockStyle.Fill;
            // [수정] 스킨과 무관하게 항상 고정된 밝은 배경 — ChatClientForm의
            // PageBackColor(=ChatTheme.Blue.PageBack)와 동일한 규칙. 예전엔
            // ChatTheme.Current.PageBack을 썼는데, 검정 스킨을 선택하면 이
            // 값이 어두운 색이 되면서 갤러리 탭 전체가 새까매지는 문제가
            // 있었다(채팅 탭은 이미 고정값을 써서 문제없었음).
            BackColor = ChatTheme.Blue.PageBack;

            // ── 상단: 대표 이미지 크게 + 카메라 배지 ──────────────────────
            var topPanel = new Panel { Dock = DockStyle.Top, Height = 190 };

            _picActive = new PictureBox
            {
                Size = new Size(140, 140),
                Left = (577 - 140) / 2,
                Top = 16,
                SizeMode = PictureBoxSizeMode.StretchImage,
                BackColor = ControlPaint.Light(ChatTheme.Current.Accent, 0.9f),
            };
            ApplyRoundedRegion(_picActive, 18);
            _picActive.Resize += (s, e) => ApplyRoundedRegion(_picActive, 18);

            var cameraMenu = new ContextMenuStrip();
            cameraMenu.Items.Add("URL로 설정 (모두에게 공유)", null, (s, e) => SetUrlRequested?.Invoke());
            cameraMenu.Items.Add("이미지 업로드 (서버에 저장, 공유)", null, (s, e) => UploadRequested?.Invoke());
            cameraMenu.Items.Add("로컬 파일로 설정 (나만 보임)", null, (s, e) => LocalFileRequested?.Invoke());
            cameraMenu.Items.Add("프로필 이미지 해제 (공유 해제)", null, (s, e) => ClearRequested?.Invoke());

            _btnCameraBadge = new CircleBadgeButton
            {
                Size = new Size(34, 34),
                Left = _picActive.Left + _picActive.Width - 30,
                Top = _picActive.Top + _picActive.Height - 30,
            };
            _btnCameraBadge.Click += (s, e) => cameraMenu.Show(_btnCameraBadge, new Point(0, _btnCameraBadge.Height));

            _lblActiveCaption = new Label
            {
                Text = "현재 대표 이미지",
                Left = 0,
                Top = _picActive.Bottom + 6,
                Width = 577,
                Height = 18,
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = ChatTheme.Current.TextSecondary,
                Font = new Font(Font.FontFamily, 8.5f),
            };

            topPanel.Controls.AddRange(new Control[] { _picActive, _btnCameraBadge, _lblActiveCaption });
            topPanel.Resize += (s, e) => RecenterTopPanel(topPanel);

            // ── 섹션 제목 ──────────────────────────────────────────────
            _lblSectionTitle = new Label
            {
                Dock = DockStyle.Top,
                Height = 26,
                Padding = new Padding(12, 6, 0, 0),
                Text = "전체 사진",
                Font = new Font(Font.FontFamily, 9f, FontStyle.Bold),
            };

            // ── 하단 액션 버튼 + 상태 ──────────────────────────────────
            var buttonPanel = new Panel { Dock = DockStyle.Bottom, Height = 40, Padding = new Padding(10, 4, 10, 4) };

            _btnSelect = new Button { Text = "대표로 지정", Dock = DockStyle.Left, Width = 270, Height = 32, Enabled = false };
            StyleActionButton(_btnSelect, filled: true);
            _btnSelect.Click += BtnSelect_Click;

            _btnDelete = new Button { Text = "삭제", Dock = DockStyle.Right, Width = 270, Height = 32, Enabled = false };
            StyleDynamicButton(_btnDelete);
            _btnDelete.Click += BtnDelete_Click;

            buttonPanel.Controls.AddRange(new Control[] { _btnSelect, _btnDelete });

            _lblStatus = new Label
            {
                Dock = DockStyle.Bottom,
                Height = 22,
                TextAlign = ContentAlignment.MiddleCenter,
                ForeColor = ChatTheme.Current.TextMuted,
                Font = new Font(Font.FontFamily, 8f),
                Text = "서버에 접속하면 갤러리를 볼 수 있습니다.",
            };

            // ── 중앙: 3열 썸네일 그리드 ─────────────────────────────────
            _flowThumbnails = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                AutoScroll = true,
                Padding = new Padding(8),
                BackColor = ChatTheme.Blue.PageBack,
            };

            Controls.Add(_flowThumbnails);
            Controls.Add(_lblSectionTitle);
            Controls.Add(_lblStatus);
            Controls.Add(buttonPanel);
            Controls.Add(topPanel);
        }

        //***************************************************************************
        // @brief 활성/비활성 상태에 따라 자동으로 모양이 바뀌는 버튼 스타일
        //        (ChatClientForm의 동일 로직 사본 — 별개 클래스라 공유 불가).
        //        활성화면 액센트 배경+흰 글씨, 비활성화면 회색 배경+회색
        //        글씨로 확실히 구분되게 하고, 테두리는 항상 그린다.
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
            if (filled)
            {
                btn.BackColor = ChatTheme.Current.Accent;
                btn.ForeColor = Color.White;
            }
            else
            {
                btn.BackColor = Color.White;
                btn.ForeColor = ChatTheme.Current.Danger;
            }
        }

        private static void ApplyRoundedRegion(Control control, int radius)
        {
            using (var path = RoundedRectPath(new Rectangle(0, 0, control.Width, control.Height), radius))
                control.Region = new Region(path);
        }

        private void RecenterTopPanel(Panel topPanel)
        {
            _picActive.Left = (topPanel.Width - _picActive.Width) / 2;
            _btnCameraBadge.Left = _picActive.Left + _picActive.Width - 30;
            _lblActiveCaption.Width = topPanel.Width;
        }

        //***************************************************************************
        // @brief 서버에 접속에 성공했을 때 호출 — 갤러리 관련 이벤트 구독을
        //        시작하고 목록을 처음 한 번 불러온다.
        //***************************************************************************
        public void AttachClient(ChatNetworkClient client)
        {
            _client = client;
            _client.ProfileImageListItemReceived += OnItemReceived;
            _client.ProfileImageListEndReceived += OnListEndReceived;
            _client.SelectProfileImageResultReceived += OnSelectResultReceived;
            _client.DeleteProfileImageResultReceived += OnDeleteResultReceived;

            RefreshList();
        }

        //***************************************************************************
        // @brief 연결이 끊겼을 때 호출 — 이벤트 구독을 해지하고 목록을 비운다.
        //***************************************************************************
        public void DetachClient()
        {
            if (_client != null)
            {
                _client.ProfileImageListItemReceived -= OnItemReceived;
                _client.ProfileImageListEndReceived -= OnListEndReceived;
                _client.SelectProfileImageResultReceived -= OnSelectResultReceived;
                _client.DeleteProfileImageResultReceived -= OnDeleteResultReceived;
                _client = null;
            }

            ClearThumbnails();
            ClearSelection();

            _picActive.Image?.Dispose();
            _picActive.Image = null;
            _picActive.BackColor = ControlPaint.Light(ChatTheme.Current.Accent, 0.9f);

            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "서버에 접속하면 갤러리를 볼 수 있습니다.";
        }

        //***************************************************************************
        // @brief 스킨 전환 시 ChatClientForm이 호출 — Panel.BackColor,
        //        Button.BackColor/ForeColor처럼 생성 시점에 굳어버린 값들을
        //        새 ChatTheme.Current로 다시 씌우고 다시 그린다. 그리드 타일
        //        (GalleryTile)과 카메라 배지는 각각 Accent를 페인트 시점에
        //        다시 읽거나(GalleryTile) RefreshThemeColor()로 갱신한다.
        //***************************************************************************
        public void RefreshTheme()
        {
            // [수정] 배경은 스킨과 무관하게 항상 고정 — 위 InitializeComponents()와
            // 동일한 이유. 여기서 다시 씌우는 이유는 예전 버전과의 일관성
            // 유지 차원일 뿐, 실제로는 값이 안 바뀌므로 없어도 무방하다.
            BackColor = ChatTheme.Blue.PageBack;
            _flowThumbnails.BackColor = ChatTheme.Blue.PageBack;

            if (_picActive.Image == null)
                _picActive.BackColor = ControlPaint.Light(ChatTheme.Current.Accent, 0.9f);

            _lblActiveCaption.ForeColor = ChatTheme.Current.TextSecondary;
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;

            _btnCameraBadge.RefreshThemeColor();
            StyleActionButton(_btnSelect, filled: true);
            RefreshDynamicButtonColors(_btnDelete);

            Invalidate(true);
        }

        //***************************************************************************
        // @brief 갤러리 목록을 서버에 다시 요청한다. 탭을 열 때마다 호출된다.
        //***************************************************************************
        public void RefreshList()
        {
            if (_client == null)
                return;

            ClearThumbnails();
            ClearSelection();

            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "불러오는 중...";
            _client.RequestListProfileImages();
        }

        //***************************************************************************
        // @brief 대표 이미지가 이 패널 밖(채팅 탭 프로필 메뉴 등)에서 바뀌었을
        //        때, 상단 큰 이미지를 그 값으로 다시 맞춘다.
        //***************************************************************************
        public void SetActiveImagePreview(string url)
        {
            if (string.IsNullOrEmpty(url))
            {
                _picActive.Image?.Dispose();
                _picActive.Image = null;
                _picActive.BackColor = ControlPaint.Light(ChatTheme.Current.Accent, 0.9f);
                return;
            }

            _ = LoadActivePreviewAsync(url);
        }

        private async Task LoadActivePreviewAsync(string url)
        {
            try
            {
                byte[] bytes = await FetchImageBytesAsync(url);
                if (bytes == null || bytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(bytes))
                using (var original = Image.FromStream(ms))
                {
                    _picActive.Image?.Dispose();
                    _picActive.Image = new Bitmap(original);
                }
                _picActive.BackColor = Color.White;
            }
            catch
            {
                // 네트워크 오류 등 — 조용히 무시.
            }
        }

        private void ClearThumbnails()
        {
            foreach (var item in _items)
                item.Thumbnail?.Dispose();
            _items.Clear();
            _flowThumbnails.Controls.Clear();
        }

        // ── 네트워크 콜백 — 백그라운드 수신 스레드에서 호출된다. ──

        private void OnItemReceived(ProfileImageListItemData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                var item = new GalleryItem { ImageId = data.ImageId, ImageRef = data.ImageRef, IsActive = data.IsActive };
                _items.Add(item);
                RebuildThumbnailGrid();
                _ = LoadThumbnailAsync(item);
            });
        }

        private void OnListEndReceived(ListProfileImagesEndResData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                _lblSectionTitle.Text = $"전체 사진  {data.TotalCount}장";
                _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
                _lblStatus.Text = "사진을 선택하면 대표 지정 또는 삭제할 수 있습니다.";
            });
        }

        private void OnSelectResultReceived(SelectProfileImageResData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (data.Success)
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Success;
                    _lblStatus.Text = "대표로 지정했습니다.";

                    // 대표 지정은 정확히 1개 선택 상태에서만 가능하므로
                    // (BtnSelect_Click 참고), _selectedTiles에 남아있는
                    // 유일한 타일에서 참조를 가져온다.
                    string newRef = string.Empty;
                    foreach (GalleryTile t in _selectedTiles)
                    {
                        newRef = t.Item?.ImageRef ?? string.Empty;
                        break;
                    }

                    ActiveImageChanged?.Invoke(newRef);
                    ClearSelection();
                    RefreshList();
                }
                else
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Danger;
                    _lblStatus.Text = "지정 실패";
                }
            });
        }

        //***************************************************************************
        // @brief [수정 — 멀티 삭제] 응답 하나는 "지금 큐에서 막 보낸 요청"에
        //        대한 것으로 간주한다(순차 처리라 한 번에 하나만 요청 중이므로
        //        안전하게 매칭됨). 성공/실패를 누적하고 ProcessNextPendingDelete()로
        //        다음 항목을 이어서 처리한다 — 큐가 비면 그 함수가 알아서
        //        결과 요약 + RefreshList()를 한 번만 수행한다.
        //***************************************************************************
        private void OnDeleteResultReceived(DeleteProfileImageResData data)
        {
            if (IsDisposed)
                return;

            BeginInvoke((MethodInvoker)delegate
            {
                if (data.Success)
                    _deleteSuccessCount++;
                else
                    _deleteFailCount++;

                ProcessNextPendingDelete();
            });
        }

        // ── 썸네일 그리드 구성 ──────────────────────────────────────────

        private void RebuildThumbnailGrid()
        {
            _flowThumbnails.SuspendLayout();
            _flowThumbnails.Controls.Clear();

            foreach (var item in _items)
            {
                var tile = new GalleryTile { Item = item };
                tile.Click += (s, e) => SelectTile(tile);
                _flowThumbnails.Controls.Add(tile);
            }

            // 맨 끝 "+" 추가 칸 — 클릭하면 업로드 메뉴 대신 곧바로 업로드를 시도한다
            // (이미지가 없는 상태에서 굳이 메뉴를 한 번 더 띄우지 않고 가장 흔한
            // 동작인 "새로 업로드"로 바로 연결).
            var addTile = new GalleryTile { Item = null };
            addTile.Click += (s, e) => UploadRequested?.Invoke();
            _flowThumbnails.Controls.Add(addTile);

            _flowThumbnails.ResumeLayout();
        }

        //***************************************************************************
        // @brief [수정 — 멀티 삭제] 클릭한 타일 하나만 선택 상태를 토글한다
        //        — 예전엔 "다른 선택을 풀고 이것만 선택"하는 단일 선택이었지만,
        //        이제 여러 장을 동시에 선택할 수 있어야 하므로 다른 선택에는
        //        손대지 않는다.
        //***************************************************************************
        private void SelectTile(GalleryTile tile)
        {
            if (tile.IsAddTile)
                return;

            if (_selectedTiles.Contains(tile))
            {
                _selectedTiles.Remove(tile);
                tile.IsSelected = false;
            }
            else
            {
                _selectedTiles.Add(tile);
                tile.IsSelected = true;
            }
            tile.Invalidate();

            UpdateActionButtonsForSelection();
        }

        //***************************************************************************
        // @brief [추가] 선택된 개수에 따라 "대표로 지정"/"삭제" 버튼의
        //        활성화 여부와 문구를 갱신한다.
        //***************************************************************************
        private void UpdateActionButtonsForSelection()
        {
            int count = _selectedTiles.Count;

            _btnSelect.Enabled = count == 1; // 대표는 한 장만 가능 — 여러 장 선택 시엔 모호하므로 비활성화

            _btnDelete.Enabled = count > 0;
            _btnDelete.Text = count > 1 ? $"삭제 ({count})" : "삭제";
        }

        //***************************************************************************
        // @brief [추가] 선택 상태를 전부 해제한다 — 목록 새로고침/연결
        //        해제 시 호출(RefreshList()/DetachClient() 등).
        //***************************************************************************
        private void ClearSelection()
        {
            _selectedTiles.Clear();
            UpdateActionButtonsForSelection();
        }

        private async Task<byte[]> FetchImageBytesAsync(string imageRef)
        {
            return await _httpClient.GetByteArrayAsync(imageRef);
        }

        private async Task LoadThumbnailAsync(GalleryItem item)
        {
            try
            {
                byte[] bytes = await FetchImageBytesAsync(item.ImageRef);
                if (bytes == null || bytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(bytes))
                using (var original = Image.FromStream(ms))
                {
                    item.Thumbnail = new Bitmap(original);
                }

                if (IsDisposed)
                    return;

                BeginInvoke((MethodInvoker)delegate
                {
                    foreach (GalleryTile tile in _flowThumbnails.Controls)
                    {
                        if (tile.Item == item)
                        {
                            tile.Invalidate();
                            break;
                        }
                    }
                });
            }
            catch
            {
                // 실패해도 조용히 무시 — 회색 칸으로 남는다.
            }
        }

        // ── 액션 버튼 ──────────────────────────────────────────────────

        private void BtnSelect_Click(object sender, EventArgs e)
        {
            // [수정] 다중 선택 중엔 이 버튼 자체가 비활성화돼 있지만
            // (UpdateActionButtonsForSelection() 참고), 방어적으로 여기서도
            // 정확히 1개일 때만 진행한다.
            if (_client == null || _selectedTiles.Count != 1)
                return;

            GalleryTile tile = null;
            foreach (GalleryTile t in _selectedTiles) { tile = t; break; }
            if (tile == null || tile.IsAddTile)
                return;

            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = "대표로 지정하는 중...";
            _client.RequestSelectProfileImage(tile.Item.ImageId);
        }

        //***************************************************************************
        // @brief [수정 — 멀티 삭제] 선택된 이미지 전부를 삭제 큐에 넣고
        //        순차 처리를 시작한다. 서버 응답이 어떤 이미지에 대한
        //        것인지 식별할 방법이 없어서(DeleteProfileImageResData 참고)
        //        한 번에 하나씩만 요청한다 — ProcessNextPendingDelete()가
        //        응답을 받을 때마다 큐의 다음 항목을 꺼내 이어서 보낸다.
        //***************************************************************************
        private void BtnDelete_Click(object sender, EventArgs e)
        {
            if (_client == null || _selectedTiles.Count == 0)
                return;

            int count = _selectedTiles.Count;
            string confirmText = count > 1 ? $"선택한 이미지 {count}장을 삭제할까요?" : "이 이미지를 삭제할까요?";
            if (MessageBox.Show(FindForm(), confirmText, "확인", MessageBoxButtons.YesNo) != DialogResult.Yes)
                return;

            _pendingDeleteQueue.Clear();
            _deleteSuccessCount = 0;
            _deleteFailCount = 0;
            _pendingDeleteWasActive = false;

            foreach (GalleryTile tile in _selectedTiles)
            {
                if (tile.IsAddTile)
                    continue;

                _pendingDeleteQueue.Enqueue(tile.Item.ImageId);
                if (tile.Item.IsActive)
                    _pendingDeleteWasActive = true;
            }

            _btnDelete.Enabled = false;
            _btnSelect.Enabled = false;
            ProcessNextPendingDelete();
        }

        //***************************************************************************
        // @brief [추가] 삭제 큐에서 하나를 꺼내 요청을 보낸다. 큐가 비었으면
        //        지금까지의 성공/실패 결과를 요약해서 보여주고 목록을
        //        새로고침한다(대표 이미지가 삭제 대상에 포함돼 있었으면
        //        ActiveImageChanged도 같이 알린다).
        //***************************************************************************
        private void ProcessNextPendingDelete()
        {
            if (_pendingDeleteQueue.Count == 0)
            {
                if (_deleteFailCount == 0)
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Success;
                    _lblStatus.Text = _deleteSuccessCount > 1 ? $"{_deleteSuccessCount}장 삭제했습니다." : "삭제했습니다.";
                }
                else
                {
                    _lblStatus.ForeColor = ChatTheme.Current.Danger;
                    _lblStatus.Text = $"삭제 완료 {_deleteSuccessCount}장, 실패 {_deleteFailCount}장";
                }

                if (_pendingDeleteWasActive)
                    ActiveImageChanged?.Invoke(string.Empty);

                ClearSelection();
                RefreshList();
                return;
            }

            long imageId = _pendingDeleteQueue.Dequeue();

            int remaining = _pendingDeleteQueue.Count + 1;
            _lblStatus.ForeColor = ChatTheme.Current.TextMuted;
            _lblStatus.Text = remaining > 1 ? $"삭제하는 중... ({remaining}장 남음)" : "삭제하는 중...";

            _client.RequestDeleteProfileImage(imageId);
        }
    }
}