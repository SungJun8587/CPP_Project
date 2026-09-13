
//***************************************************************************
// ProfileImageGalleryDialog.cs : 프로필 이미지 갤러리(목록/선택/삭제) 팝업 창.
//
// [설계] ChatClientForm이 갖고 있는 ChatNetworkClient를 그대로 공유받아
// 쓴다. 이 창이 열려 있는 동안만 갤러리 관련 이벤트를 구독하고, 닫히면
// 즉시 해지한다(NicknameChangeDialog와 동일한 패턴).
//
// [설계] 대표 이미지 썸네일/일반 썸네일 fetch 로직(local:/http(s) 구분,
// 청크 다운로드 조립)은 ChatClientForm에 있는 것과 사실상 동일한 코드를
// 이 다이얼로그 안에 독립적으로 두었다 — 두 클래스가 이 로직을 공유할
// 손쉬운 경로(공용 유틸 클래스 등)가 아직 없어서 중복을 감수했다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net.Http;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace ChatApp
{
    public class ProfileImageGalleryDialog : Form
    {
        private class GalleryItem
        {
            public long ImageId;
            public string ImageRef;
            public bool IsActive;
            public Image Thumbnail;
        }

        private readonly ChatNetworkClient _client;
        private readonly HttpClient _httpClient;

        private ListBox _listBox;
        private Button _btnSelect;
        private Button _btnDelete;
        private Button _btnClose;
        private Label _lblStatus;

        private readonly List<GalleryItem> _items = new List<GalleryItem>();

        // BtnSelect_Click/BtnDelete_Click 시점의 대상 항목 정보를 기억해뒀다가,
        // 비동기로 도착하는 결과 처리에서 참고한다(그 사이 목록이 갱신돼도
        // 안전하게 원래 의도했던 대상 기준으로 판단하기 위함).
        private string _pendingSelectedImageRef;
        private bool _pendingDeleteWasActive;

        //***************************************************************************
        // @brief 이 창에서 마지막으로 확정된 "대표 이미지" 참조값. 선택/삭제가
        //        성공해서 대표가 바뀔 때마다 갱신된다. 호출부(ChatClientForm)가
        //        ShowDialog() 반환 후 이 값을 읽어 자기 화면의 프로필 이미지
        //        표시를 갱신한다. 아무 변경도 없었으면 null.
        //***************************************************************************
        public string ActiveImageRef { get; private set; }

        public ProfileImageGalleryDialog(ChatNetworkClient client, HttpClient httpClient)
        {
            _client = client;
            _httpClient = httpClient;
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Text = "프로필 이미지 갤러리";
            ClientSize = new Size(360, 380);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;

            _listBox = new ListBox
            {
                Left = 10,
                Top = 10,
                Width = 340,
                Height = 260,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 48,
            };
            _listBox.DrawItem += ListBox_DrawItem;

            _btnSelect = new Button { Text = "대표로 지정", Left = 10, Top = 280, Width = 105, Height = 28 };
            _btnSelect.Click += BtnSelect_Click;

            _btnDelete = new Button { Text = "삭제", Left = 125, Top = 280, Width = 105, Height = 28 };
            _btnDelete.Click += BtnDelete_Click;

            _btnClose = new Button { Text = "닫기", Left = 245, Top = 280, Width = 105, Height = 28, DialogResult = DialogResult.Cancel };

            _lblStatus = new Label { Left = 10, Top = 316, Width = 340, Height = 40, ForeColor = Color.Gray };

            Controls.AddRange(new Control[] { _listBox, _btnSelect, _btnDelete, _btnClose, _lblStatus });
            CancelButton = _btnClose;

            Load += (s, e) =>
            {
                _client.ProfileImageListItemReceived += OnItemReceived;
                _client.ProfileImageListEndReceived += OnListEndReceived;
                _client.SelectProfileImageResultReceived += OnSelectResultReceived;
                _client.DeleteProfileImageResultReceived += OnDeleteResultReceived;

                RefreshList();
            };
            FormClosed += (s, e) =>
            {
                _client.ProfileImageListItemReceived -= OnItemReceived;
                _client.ProfileImageListEndReceived -= OnListEndReceived;
                _client.SelectProfileImageResultReceived -= OnSelectResultReceived;
                _client.DeleteProfileImageResultReceived -= OnDeleteResultReceived;

                foreach (var item in _items)
                    item.Thumbnail?.Dispose();
            };
        }

        private void RefreshList()
        {
            foreach (var item in _items)
                item.Thumbnail?.Dispose();
            _items.Clear();
            _listBox.Items.Clear();

            _lblStatus.ForeColor = Color.Gray;
            _lblStatus.Text = "불러오는 중...";
            _client.RequestListProfileImages();
        }

        // ── 네트워크 콜백 — 백그라운드 수신 스레드에서 호출된다. ──

        private void OnItemReceived(ProfileImageListItemData data)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                var item = new GalleryItem { ImageId = data.ImageId, ImageRef = data.ImageRef, IsActive = data.IsActive };
                _items.Add(item);
                _listBox.Items.Add(item);
                _ = LoadThumbnailAsync(item);
            });
        }

        private void OnListEndReceived(ListProfileImagesEndResData data)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                _lblStatus.ForeColor = Color.Gray;
                _lblStatus.Text = $"총 {data.TotalCount}개";
            });
        }

        private void OnSelectResultReceived(SelectProfileImageResData data)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                if (data.Success)
                {
                    ActiveImageRef = _pendingSelectedImageRef;
                    _lblStatus.ForeColor = Color.Green;
                    _lblStatus.Text = "대표로 지정했습니다.";
                    RefreshList();
                }
                else
                {
                    _lblStatus.ForeColor = Color.Firebrick;
                    _lblStatus.Text = "지정 실패";
                }
            });
        }

        private void OnDeleteResultReceived(DeleteProfileImageResData data)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                if (data.Success)
                {
                    if (_pendingDeleteWasActive)
                        ActiveImageRef = string.Empty; // 대표 이미지를 지웠으니 "미설정"으로

                    _lblStatus.ForeColor = Color.Green;
                    _lblStatus.Text = "삭제했습니다.";
                    RefreshList();
                }
                else
                {
                    _lblStatus.ForeColor = Color.Firebrick;
                    _lblStatus.Text = "삭제 실패";
                }
            });
        }

        // ── 이미지 fetch(local:/http(s) 구분) — ChatClientForm의 동일 로직과
        //    독립적인 사본. 클래스 상단 설명 참고. ──

        private async Task<byte[]> FetchImageBytesAsync(string imageRef)
        {
            if (imageRef.StartsWith("local:", StringComparison.OrdinalIgnoreCase))
                return await DownloadLocalProfileImageAsync(imageRef);

            return await _httpClient.GetByteArrayAsync(imageRef);
        }

        private Task<byte[]> DownloadLocalProfileImageAsync(string imageRef)
        {
            var tcs = new TaskCompletionSource<byte[]>();
            var buffer = new List<byte>();
            uint expectedDownloadId = 0;
            bool started = false;

            Action<DownloadProfileImageBeginResData> onBegin = null;
            Action<DownloadProfileImageChunkResData> onChunk = null;
            Action<DownloadProfileImageEndResData> onEnd = null;

            void Cleanup()
            {
                _client.DownloadProfileImageBeginReceived -= onBegin;
                _client.DownloadProfileImageChunkReceived -= onChunk;
                _client.DownloadProfileImageEndReceived -= onEnd;
            }

            onBegin = data =>
            {
                if (started)
                    return;

                if (!data.Success)
                {
                    Cleanup();
                    tcs.TrySetResult(null);
                    return;
                }

                expectedDownloadId = data.DownloadId;
                started = true;
            };

            onChunk = data =>
            {
                if (!started || data.DownloadId != expectedDownloadId)
                    return;

                buffer.AddRange(data.ChunkData);
            };

            onEnd = data =>
            {
                if (!started || data.DownloadId != expectedDownloadId)
                    return;

                Cleanup();
                tcs.TrySetResult(buffer.ToArray());
            };

            _client.DownloadProfileImageBeginReceived += onBegin;
            _client.DownloadProfileImageChunkReceived += onChunk;
            _client.DownloadProfileImageEndReceived += onEnd;

            _client.RequestDownloadProfileImage(imageRef);

            return tcs.Task;
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

                if (!IsDisposed)
                    Invoke((MethodInvoker)delegate { _listBox.Invalidate(); });
            }
            catch
            {
                // 실패해도 조용히 무시 — 목록 텍스트는 그대로 보인다.
            }
        }

        // ── UI 이벤트 ──

        private void ListBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= _items.Count)
                return;

            var item = _items[e.Index];
            e.DrawBackground();

            Graphics g = e.Graphics;
            Rectangle bounds = e.Bounds;

            if (item.Thumbnail != null)
                g.DrawImage(item.Thumbnail, new Rectangle(bounds.Left + 4, bounds.Top + 4, 40, 40));

            string label = (item.IsActive ? "[대표] " : "") + item.ImageRef;
            using (var format = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
            {
                var textRect = new RectangleF(bounds.Left + 50, bounds.Top + 14, bounds.Width - 55, 20);
                Brush textBrush = item.IsActive ? Brushes.Blue : Brushes.Black;
                g.DrawString(label, _listBox.Font, textBrush, textRect, format);
            }

            e.DrawFocusRectangle();
        }

        private void BtnSelect_Click(object sender, EventArgs e)
        {
            if (_listBox.SelectedIndex < 0 || _listBox.SelectedIndex >= _items.Count)
                return;

            var item = _items[_listBox.SelectedIndex];
            _pendingSelectedImageRef = item.ImageRef;

            _lblStatus.ForeColor = Color.Gray;
            _lblStatus.Text = "대표로 지정하는 중...";
            _client.RequestSelectProfileImage(item.ImageId);
        }

        private void BtnDelete_Click(object sender, EventArgs e)
        {
            if (_listBox.SelectedIndex < 0 || _listBox.SelectedIndex >= _items.Count)
                return;

            var item = _items[_listBox.SelectedIndex];

            if (MessageBox.Show(this, "이 이미지를 삭제할까요?", "확인", MessageBoxButtons.YesNo) != DialogResult.Yes)
                return;

            _pendingDeleteWasActive = item.IsActive;

            _lblStatus.ForeColor = Color.Gray;
            _lblStatus.Text = "삭제하는 중...";
            _client.RequestDeleteProfileImage(item.ImageId);
        }
    }
}