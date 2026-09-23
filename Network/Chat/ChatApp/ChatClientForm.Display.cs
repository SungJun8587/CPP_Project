
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
    // ChatClientForm.Display.cs : 채팅 목록 OwnerDraw 렌더링(말풍선/아바타/링크·파일 카드)과 마우스 처리, 그리고 채팅/시스템 로그를 화면에 추가하는 공통 헬퍼.
    public partial class ChatClientForm
    {

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

            // [추가] 파일 첨부 메시지("[FILE:이름:크기]url")는 원문 텍스트를
            // 그대로 보여주지 않고 고정 크기 카드로만 그린다 — 텍스트 길이에
            // 따라 줄바꿈되는 일반 메시지와 달리 높이가 항상 같다.
            if (FileAttachmentRegex.IsMatch(item.Message) || item.IsUploading)
            {
                int fileBaseHeight = item.IsMyMessage ? 15 : 25; // 일반 메시지의 baseHeight에서 텍스트 한 줄분(약 10px)을 뺀 값
                e.ItemHeight = kFileCardHeight + fileBaseHeight;
                return;
            }

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
        // @brief 발신자 아바타를 그린다. profileImageUrl로 받아온 실제 이미지이
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

            // [수정] e.DrawBackground()는 내부적으로 e.State에 Selected가
            // 있으면 SystemColors.Highlight(파란색)로 칠한다 — SelectionMode.None을
            // 설정해뒀지만(InitializeComponents 참고) 그래도 파란 배경이
            // 재현된다는 신고가 있어서, e.State를 아예 보지 않고 리스트박스의
            // BackColor로 직접 채우도록 바꿨다. 이러면 어떤 상태 플래그가
            // 서 있든 결과가 항상 동일해서 더 확실하다.
            using (var backBrush = new SolidBrush(_listBoxChat.BackColor))
                e.Graphics.FillRectangle(backBrush, e.Bounds);

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

                // [추가] 파일 첨부 메시지는 원문("[FILE:이름:크기]url")을 그대로
                // 그리지 않고 고정 크기 카드로 대체한다 — 말풍선 크기도
                // 텍스트 길이가 아니라 카드 크기에 맞춘다.
                Match fileMatch = FileAttachmentRegex.Match(item.Message);
                bool isFileAttachment = fileMatch.Success || item.IsUploading;

                int bubbleWidth = isFileAttachment ? Math.Min(maxBubbleWidth, 220) : (int)textSize.Width + 16;
                int bubbleHeight = isFileAttachment ? kFileCardHeight : (int)textSize.Height + 10;

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

                    if (isFileAttachment)
                    {
                        // 파일 첨부는 색깔 말풍선이 아니라 파일 카드 그 자체로 표현한다.
                        DrawFileAttachmentCard(g, item, fileMatch, bubbleRect, e.Index, isMyMessage: true);
                    }
                    else
                    {
                        using (var path = GetRoundedRectPath(bubbleRect, kBubbleCornerRadius))
                        {
                            g.FillPath(myBubbleColor, path);
                        }

                        DrawMessageWithLinks(g, item.Message, msgFont, textColor, myBubbleColor, new RectangleF(bubbleX + 8, bubbleY + 5, maxBubbleWidth, textSize.Height), e.Index);

                        // 내 메시지는 우측 정렬이라, 미리보기 카드도 말풍선과 같은
                        // 오른쪽 기준선에 맞춘다(아바타 폭만큼 같이 밀어줌).
                        DrawLinkPreviewCardIfAny(g, item.Message, bounds.Right - Math.Min(maxBubbleWidth, 220) - kAvatarSize - kAvatarGap - 10,
                            bubbleY + bubbleHeight + 4, Math.Min(maxBubbleWidth, 220), e.Index);
                    }

                    SizeF timeSize = g.MeasureString(timeStr, _timeFont);
                    g.DrawString(timeStr, _timeFont, timeColor, bubbleX - timeSize.Width - 5, bubbleY + bubbleHeight - timeSize.Height);
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

                    if (isFileAttachment)
                    {
                        DrawFileAttachmentCard(g, item, fileMatch, bubbleRect, e.Index, isMyMessage: false);
                    }
                    else
                    {
                        using (var path = GetRoundedRectPath(bubbleRect, kBubbleCornerRadius))
                        {
                            g.FillPath(otherBubbleColor, path);
                            g.DrawPath(Pens.LightGray, path);
                        }

                        DrawMessageWithLinks(g, item.Message, msgFont, textColor, otherBubbleColor, new RectangleF(bubbleX + 8, bubbleY + 5, maxBubbleWidth, textSize.Height), e.Index);

                        // 상대 메시지는 좌측 정렬이라, 미리보기 카드도 말풍선과 같은
                        // 왼쪽 기준선에 맞춘다.
                        DrawLinkPreviewCardIfAny(g, item.Message, bubbleX, bubbleY + bubbleHeight + 4,
                            Math.Min(maxBubbleWidth, 220), e.Index);
                    }

                    SizeF timeSize = g.MeasureString(timeStr, _timeFont);
                    g.DrawString(timeStr, _timeFont, timeColor, bubbleX + bubbleWidth + 5, bubbleY + bubbleHeight - timeSize.Height);
                }
            }
            else
            {
                // 시스템 안내성 문자열이 섞여 들어온 경우(현재는 안 쓰지만 방어적으로)
                string text = _listBoxChat.Items[e.Index].ToString();
                TextRenderer.DrawText(g, text, _listBoxChat.Font, bounds, Color.Gray, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
            }

            // [수정] SelectionMode.None이라 "선택된/포커스된 항목" 개념
            // 자체가 없다 — 예전에 있던 e.DrawFocusRectangle() 호출은
            // 클릭한 항목 주변에 점선 테두리를 남길 뿐 아무 의미가 없어서
            // 제거했다.
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
        // @brief [추가] 파일 첨부 메시지("[FILE:이름:크기]url")를 아이콘+파일명+
        //        용량으로 구성된 카드로 그린다. 클릭 히트테스트를 위해 카드
        //        영역도 _chatItemUrlRegions에 등록한다(링크 카드와 동일한
        //        메커니즘 재사용 — ChatListBox_MouseClick이 그대로 처리해줌).
        //        내가 보낸 파일이면(isMyMessage) 우측 상단에 × 삭제 버튼도
        //        그리고, 그 클릭 영역은 _chatItemDeleteButtonRegions에 별도
        //        등록한다(파일 열기와 구분해서 판정해야 하므로).
        // @details [추가] item.IsUploading/IsDownloading 중이면 파일명/용량
        //          아래에 진행률 바를 그린다. 업로드 중에는 아직 서버 URL이
        //          없으므로(item.PendingFileName/PendingFileSize를 대신
        //          쓴다) fileMatch가 실패한 상태로 넘어올 수 있다 — 그 경우
        //          fileMatch 쪽 정보는 아예 안 쓴다. 전송 중에는 × 삭제
        //          버튼과 클릭(파일 열기/다운로드)을 비활성화한다 — 진행
        //          중인 전송을 취소하는 기능은 없어서, 어중간하게 누를 수
        //          있게 두는 것보다는 아예 막는 게 낫다고 판단했다.
        //***************************************************************************
        private void DrawFileAttachmentCard(Graphics g, ChatBubbleItem item, Match fileMatch, Rectangle rect, int itemIndex, bool isMyMessage)
        {
            bool isTransferring = item.IsUploading || item.IsDownloading;
            int transferPercent = item.IsUploading ? item.UploadPercent : item.DownloadPercent;

            string fileName;
            long sizeBytes;
            string url;

            if (item.IsUploading)
            {
                fileName = item.PendingFileName ?? string.Empty;
                sizeBytes = item.PendingFileSize;
                url = string.Empty;
            }
            else
            {
                fileName = fileMatch.Groups["name"].Value;
                url = fileMatch.Groups["url"].Value;
                sizeBytes = long.TryParse(fileMatch.Groups["size"].Value, out long parsed) ? parsed : 0;
            }

            using (var path = GetRoundedRectPath(rect, 8))
            {
                g.FillPath(Brushes.WhiteSmoke, path);
                g.DrawPath(Pens.LightGray, path);
            }

            // 아이콘 자리 — 확장자 텍스트만 보여주는 단순한 사각 배지로
            // 대체한다(실제 파일 종류별 아이콘 세트는 없음).
            const int kIconSize = 36;
            var iconRect = new Rectangle(rect.Left + 7, rect.Top + (rect.Height - kIconSize) / 2, kIconSize, kIconSize);
            string ext = Path.GetExtension(fileName).TrimStart('.').ToUpperInvariant();
            if (ext.Length > 4) ext = ext.Substring(0, 4);

            using (var iconPath = GetRoundedRectPath(iconRect, 4))
            using (var iconBrush = new SolidBrush(AccentColor))
                g.FillPath(iconBrush, iconPath);

            using (var extFont = new Font(_listBoxChat.Font.FontFamily, 7.5f, FontStyle.Bold))
            using (var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center })
                g.DrawString(ext, extFont, Brushes.White, iconRect, fmt);

            // 내가 보낸 파일이면 오른쪽에 × 버튼 자리를 비워서 파일명/용량
            // 텍스트가 거기 겹치지 않게 한다(전송 중에는 × 버튼 자체를 안
            // 그리지만, 자리 계산 방식은 그대로 둬서 전송 완료 후 레이아웃이
            // 안 흔들리게 한다).
            const int kDeleteButtonSize = 16;
            int textRightMargin = isMyMessage ? (kDeleteButtonSize + 8) : 6;

            int textX = iconRect.Right + 8;
            int textWidth = Math.Max(0, rect.Right - textRightMargin - textX);

            // [추가] 전송 중이면 파일명/용량을 위쪽으로 살짝 올리고, 그
            // 아래에 진행률 바 + 퍼센트 텍스트를 그린다.
            int nameTop = isTransferring ? rect.Top + 4 : rect.Top + 8;

            using (var nameFont = new Font(_listBoxChat.Font, FontStyle.Bold))
            using (var clipFormat = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.LineLimit })
            {
                var nameRect = new RectangleF(textX, nameTop, textWidth, 16);
                g.DrawString(fileName, nameFont, Brushes.Black, nameRect, clipFormat);

                if (!isTransferring)
                {
                    var sizeRect = new RectangleF(textX, rect.Top + 28, textWidth, 16);
                    g.DrawString(FormatFileSize(sizeBytes), _timeFont, Brushes.Gray, sizeRect, clipFormat);
                }
            }

            if (isTransferring)
            {
                const int kBarHeight = 6;
                var barBackRect = new RectangleF(textX, rect.Top + 26, textWidth, kBarHeight);
                var barFillRect = new RectangleF(textX, rect.Top + 26, textWidth * Math.Max(0, Math.Min(100, transferPercent)) / 100f, kBarHeight);

                using (var backBrush = new SolidBrush(Color.FromArgb(230, 230, 230)))
                    g.FillRectangle(backBrush, barBackRect);
                using (var fillBrush = new SolidBrush(AccentColor))
                    g.FillRectangle(fillBrush, barFillRect);

                string label = (item.IsUploading ? "업로드 중 " : "다운로드 중 ") + transferPercent + "%";
                using (var labelFmt = new StringFormat { Trimming = StringTrimming.EllipsisCharacter })
                    g.DrawString(label, _timeFont, Brushes.Gray, new RectangleF(textX, rect.Top + 34, textWidth, 14), labelFmt);
            }

            if (isMyMessage && !isTransferring)
            {
                var deleteRect = new RectangleF(rect.Right - kDeleteButtonSize - 4, rect.Top + 4, kDeleteButtonSize, kDeleteButtonSize);

                using (var circleBrush = new SolidBrush(Color.FromArgb(60, 0, 0, 0)))
                    g.FillEllipse(circleBrush, deleteRect);

                using (var xPen = new Pen(Color.White, 1.5f))
                {
                    float pad = 4f;
                    g.DrawLine(xPen, deleteRect.Left + pad, deleteRect.Top + pad, deleteRect.Right - pad, deleteRect.Bottom - pad);
                    g.DrawLine(xPen, deleteRect.Right - pad, deleteRect.Top + pad, deleteRect.Left + pad, deleteRect.Bottom - pad);
                }

                _chatItemDeleteButtonRegions[itemIndex] = deleteRect;
            }
            else
            {
                // 전송 중으로 바뀌면서 이전에 등록됐던 × 버튼 영역이 남아있으면
                // 안 된다(전송 중엔 안 그리므로 클릭 판정도 없어야 함).
                _chatItemDeleteButtonRegions.Remove(itemIndex);
            }

            if (!isTransferring)
            {
                if (!_chatItemUrlRegions.TryGetValue(itemIndex, out var regions))
                {
                    regions = new List<(string Url, RectangleF Bounds)>();
                    _chatItemUrlRegions[itemIndex] = regions;
                }
                regions.Add((url, rect));
            }
            else
            {
                // 업로드 중(URL 미확정)이거나 다운로드 중(중복 클릭 방지)이면
                // 클릭 영역 자체를 등록하지 않는다.
                _chatItemUrlRegions.Remove(itemIndex);
            }
        }

        //***************************************************************************
        // @brief 바이트 수를 "1.2MB"/"340KB"/"512B" 형태의 읽기 쉬운 문자열로 바꾼다.
        //***************************************************************************
        private static string FormatFileSize(long bytes)
        {
            if (bytes >= 1024 * 1024)
                return (bytes / (1024.0 * 1024.0)).ToString("0.#") + "MB";
            if (bytes >= 1024)
                return (bytes / 1024.0).ToString("0.#") + "KB";
            return bytes + "B";
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
        private void DrawMessageWithLinks(Graphics g, string message, Font font, Brush textColor, Brush bubbleBackgroundColor, RectangleF layoutRect, int itemIndex)
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

                        // [수정 — 글자 깨짐] 검정 텍스트 위에 파란 텍스트를
                        // 그냥 덧그리면, 두 번의 DrawString 호출 사이의 미세한
                        // 안티앨리어싱/서브픽셀 렌더링 차이 때문에 두 텍스트가
                        // 살짝 어긋난 채 겹쳐 보여서 글자가 깨진 것처럼
                        // 보였다(실제로 재현됨). 먼저 그 자리를 말풍선
                        // 배경색으로 채워 검정 텍스트를 깨끗이 지운 뒤에
                        // 파란 밑줄 텍스트를 그린다.
                        g.FillRectangle(bubbleBackgroundColor, bounds);
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
            if (index < 0)
                return;

            // [추가] × 삭제 버튼 판정을 "카드 클릭(파일 열기)"보다 먼저 한다 —
            // 삭제 버튼이 카드 영역 안쪽 구석에 있어서, 먼저 확인 안 하면
            // 항상 "파일 열기"로만 처리돼버린다.
            if (_chatItemDeleteButtonRegions.TryGetValue(index, out var deleteRect) && deleteRect.Contains(e.Location))
            {
                DeleteFileAttachment(index);
                return;
            }

            if (!_chatItemUrlRegions.TryGetValue(index, out var regions))
                return;

            foreach (var region in regions)
            {
                if (!region.Bounds.Contains(e.Location))
                    continue;

                // [수정] 파일 첨부 카드는 일반 링크 미리보기와 달리 브라우저로
                // 안 넘기고 앱 안에서 직접 받는다(저장 위치를 고르고 진행률을
                // 그 카드에 그려주기 위함) — item.Message가 FileAttachmentRegex에
                // 맞으면 파일 카드로 판단한다. DrawFileAttachmentCard()가
                // 전송 중(IsUploading/IsDownloading)에는 애초에 이 영역을
                // 등록하지 않으므로, 여기 도달했다는 건 지금 전송 중이
                // 아니라는 뜻이라 중복 다운로드 걱정은 없다.
                if (_listBoxChat.Items[index] is ChatBubbleItem item && FileAttachmentRegex.IsMatch(item.Message))
                {
                    Match fileMatch = FileAttachmentRegex.Match(item.Message);
                    DownloadFileAttachment(item, fileMatch.Groups["url"].Value, fileMatch.Groups["name"].Value);
                }
                else
                {
                    OpenUrl(region.Url);
                }
                return;
            }
        }

        //***************************************************************************
        // @brief [추가] 채팅 목록 위로 파일을 끌고 왔을 때 — 실제 파일
        //        드롭(DataFormats.FileDrop)일 때만 Copy 커서로 "여기 놓을 수
        //        있다"를 보여준다. 그 외(텍스트를 끌고 온 경우 등)는 None으로
        //        거부 표시.
        //***************************************************************************
        private void ChatListBox_DragEnter(object sender, DragEventArgs e)
        {
            e.Effect = (_client != null && e.Data.GetDataPresent(DataFormats.FileDrop))
                ? DragDropEffects.Copy
                : DragDropEffects.None;
        }

        //***************************************************************************
        // @brief [추가] 채팅 목록에 파일을 놓으면 파일 선택 대화상자를 거친
        //        것과 동일하게 업로드+전송한다. 여러 파일을 한꺼번에 끌어다
        //        놓으면 순서대로 하나씩 처리한다 — 업로드 토큰 발급
        //        (_uploadTokenTcs)이 한 번에 하나만 진행되는 구조라, 동시에
        //        여러 개를 병렬로 올리면 서로의 토큰 응답을 가로챌 수
        //        있어서 의도적으로 직렬화했다.
        //***************************************************************************
        private async void ChatListBox_DragDrop(object sender, DragEventArgs e)
        {
            if (_client == null || !e.Data.GetDataPresent(DataFormats.FileDrop))
                return;

            string[] filePaths = (string[])e.Data.GetData(DataFormats.FileDrop);
            if (filePaths == null)
                return;

            foreach (string filePath in filePaths)
            {
                if (File.Exists(filePath)) // 디렉터리가 끌려온 경우는 조용히 건너뜀
                    await SendFileAttachmentAsync(filePath);
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
        // @brief [수정] timestamp 파라미터 추가 — 실시간 메시지는 항상 null을
        //        넘겨서 지금 시각을 쓰지만(기존 동작 그대로), 과거 대화
        //        기록(OnChatHistoryItemReceived())은 실제 발신 시각을 그대로
        //        넘겨서 날짜 구분선이 그 시점 기준으로 정확히 나뉘게 한다.
        // @details [수정 — 버그] messageId != 0(서버가 부여한 실제 ID —
        //          실시간/기록 메시지 전부 해당, 로컬 에코만 0)이면 이미
        //          같은 ID의 항목이 리스트박스에 있는지 먼저 확인하고,
        //          있으면 조용히 건너뛴다. 두 가지 경쟁 상태를 막기 위함:
        //          1) 방 입장 처리(MoveToRoom, 멤버 등록)와 방 정보/기록
        //             조회(비동기 DB) 사이의 짧은 틈에 다른 사람이 보낸
        //             메시지가 실시간 브로드캐스트로 먼저 오고, 뒤이어
        //             오는 기록 조회에도 같은 메시지가 포함될 수 있다.
        //          2) 방을 빠르게 두 번 클릭하는 등으로 같은 방에 대한
        //             기록 스트리밍이 두 번 시작되면, 모든 메시지가
        //             통째로 중복 추가될 수 있다.
        //***************************************************************************
        private void AppendChat(string senderName, string senderProfileImageUrl, string message, bool isMyMessage, long messageId = 0, DateTime? timestamp = null)
        {
            if (messageId != 0)
            {
                foreach (object existing in _listBoxChat.Items)
                {
                    if (existing is ChatBubbleItem existingItem && existingItem.MessageId == messageId)
                        return; // 이미 있는 메시지 — 중복 추가하지 않음
                }
            }

            DateTime effectiveTimestamp = timestamp ?? DateTime.Now;

            // [추가] 마지막 메시지와 날짜(일 단위)가 다르면(또는 첫 메시지면)
            // 구분선을 먼저 끼워 넣는다 — 카카오톡 등에서 흔히 보이는 패턴.
            DateTime? lastDate = null;
            for (int i = _listBoxChat.Items.Count - 1; i >= 0; i--)
            {
                if (_listBoxChat.Items[i] is ChatBubbleItem prevItem)
                {
                    lastDate = prevItem.Timestamp.Date;
                    break;
                }
            }
            if (lastDate == null || lastDate.Value != effectiveTimestamp.Date)
                _listBoxChat.Items.Add(new DateSeparatorItem { Date = effectiveTimestamp });

            _listBoxChat.Items.Add(new ChatBubbleItem(senderName, senderProfileImageUrl, message, isMyMessage, messageId) { Timestamp = effectiveTimestamp });
            int newIndex = _listBoxChat.Items.Count - 1;
            _listBoxChat.TopIndex = newIndex;

            // 방금 추가한 메시지에 URL이 있으면 미리보기 카드용 데이터를
            // 비동기로 요청한다 — 완료되면 이 특정 항목만 다시 그려지도록
            // RefreshChatItem()이 트리거된다. [수정] 파일 첨부 메시지는
            // "[FILE:...]url" 자체가 URL을 포함하고 있어서 이 검사에 걸리지만,
            // 그건 웹페이지가 아니라 파일 서버가 준 파일 URL이라 링크
            // 미리보기(HTML 메타 태그 파싱)를 시도하는 건 의미가 없고
            // 불필요한 요청만 낭비한다 — 건너뛴다(파일 카드는 DrawItem이
            // FileAttachmentRegex로 별도 처리).
            if (FileAttachmentRegex.IsMatch(message))
                return;

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
        // @brief 현재 위치(로비/방 이름+방장) 표시와 관련 버튼들의 활성화/
        //        표시 상태를 한 번에 갱신한다.
        // @details [수정] 방이 번호가 아니라 이름을 갖게 되면서 표시 문구가
        //          바뀌었고, 방장 전용 버튼(이름변경/삭제)의 표시 여부도
        //          여기서 같이 판단한다 — _currentRoomOwnerNickname이 내
        //          닉네임과 같으면(로비가 아닐 때만) 내가 방장이라는 뜻이다.
        //***************************************************************************
        private void UpdateRoomStatusUI()
        {
            if (_currentRoomId < 0)
            {
                SetCurrentRoomLabel("위치: (로그인 전)");
                _btnRoomList.Enabled = false;
                _btnCreateRoom.Enabled = false;
                _btnRoomLeave.Enabled = false;
                _btnRenameRoom.Visible = false;
                _btnDeleteRoom.Visible = false;
                _pnlRoomInfo.Visible = false;
                return;
            }

            bool inLobby = (_currentRoomId == ProtocolConstants.LobbyRoomId);

            if (inLobby)
            {
                SetCurrentRoomLabel("위치: 로비");
            }
            else
            {
                string roomLabel = string.IsNullOrEmpty(_currentRoomName) ? $"{_currentRoomId}번 방" : _currentRoomName;
                SetCurrentRoomLabel($"위치: {roomLabel}");
            }

            _btnRoomList.Enabled = true;
            _btnCreateRoom.Enabled = true;
            _btnRoomLeave.Enabled = !inLobby;

            bool isOwner = !inLobby && _currentRoomOwnerNickname != null && _currentRoomOwnerNickname == _currentNickname;
            _btnRenameRoom.Visible = isOwner;
            _btnDeleteRoom.Visible = isOwner;

            // [추가] 방 프로필 이미지+이름+방장 행 — 로비에서는 숨기고,
            // 방에 있을 때만 채워서 보여준다. 방장이면 아바타에 손 커서를
            // 줘서 "클릭해서 바꿀 수 있다"를 암시한다(PnlRoomAvatar_Click
            // 참고 — 방장이 아니면 클릭해도 아무 일도 안 함).
            _pnlRoomInfo.Visible = !inLobby;
            if (!inLobby)
            {
                _lblRoomInfoName.Text = string.IsNullOrEmpty(_currentRoomName) ? $"{_currentRoomId}번 방" : _currentRoomName;
                _lblRoomInfoOwner.Text = string.IsNullOrEmpty(_currentRoomOwnerNickname) ? "" : $"방장: {_currentRoomOwnerNickname}";
                _pnlRoomAvatar.RoomName = _lblRoomInfoName.Text;
                _pnlRoomAvatar.Cursor = isOwner ? Cursors.Hand : Cursors.Default;

                ApplyRoomAvatarImage(_currentRoomImageUrl);
            }
        }

        //***************************************************************************
        // @brief [추가] 방 아바타에 표시할 이미지를 갱신한다. 캐시에 이미
        //        있으면 즉시 반영하고, 없으면 RequestAvatarImage()로
        //        비동기 로딩을 걸고 완료되는 대로 다시 칠한다.
        // @details 로딩이 끝나는 시점엔 사용자가 이미 다른 방으로 옮겨가
        //          _currentRoomImageUrl이 바뀌어 있을 수 있다 — 그 경우
        //          지금은 상관없는 이미지이므로 반영하지 않는다(완료 콜백
        //          안에서 다시 한번 비교).
        //***************************************************************************
        private void ApplyRoomAvatarImage(string imageUrl)
        {
            if (string.IsNullOrEmpty(imageUrl))
            {
                AppendSystemLog("[디버그] ApplyRoomAvatarImage: URL이 비어있음 - 기본 아바타로", ColorSystemDebug);
                _pnlRoomAvatar.LoadedImage = null;
                _pnlRoomAvatar.Invalidate();
                return;
            }

            Image cached;
            lock (_avatarImageCache)
            {
                _avatarImageCache.TryGetValue(imageUrl, out cached);
            }

            if (cached != null)
            {
                AppendSystemLog($"[디버그] ApplyRoomAvatarImage: 캐시 히트 - 즉시 반영 (url={imageUrl})", ColorSystemDebug);
                _pnlRoomAvatar.LoadedImage = cached;
                _pnlRoomAvatar.Invalidate();
                return;
            }

            AppendSystemLog($"[디버그] ApplyRoomAvatarImage: 캐시 미스 - 로딩 시작 (url={imageUrl})", ColorSystemDebug);
            _pnlRoomAvatar.LoadedImage = null;
            _pnlRoomAvatar.Invalidate();

            RequestAvatarImage(imageUrl, () =>
            {
                if (IsDisposed || !IsHandleCreated)
                    return;

                Invoke((MethodInvoker)delegate
                {
                    if (_currentRoomImageUrl != imageUrl)
                    {
                        AppendSystemLog($"[디버그] ApplyRoomAvatarImage: 로딩 완료됐지만 그 사이 방이 바뀌어 무시 (요청url={imageUrl}, 지금url={_currentRoomImageUrl})", ColorSystemDebug);
                        return; // 그 사이 다른 방으로 이동함 — 이 결과는 이제 안 맞음
                    }

                    lock (_avatarImageCache)
                    {
                        _avatarImageCache.TryGetValue(imageUrl, out var img);
                        _pnlRoomAvatar.LoadedImage = img;
                        AppendSystemLog(img != null
                            ? $"[디버그] ApplyRoomAvatarImage: 로딩 완료 - 반영함 (url={imageUrl})"
                            : $"[디버그] ApplyRoomAvatarImage: 로딩 완료됐는데 캐시에 이미지가 없음(fetch 실패 추정) (url={imageUrl})",
                            ColorSystemDebug);
                    }
                    _pnlRoomAvatar.Invalidate();
                });
            });
        }

        //***************************************************************************
        // @brief [추가] _lblCurrentRoom의 텍스트와 툴팁을 함께 갱신한다.
        //        라벨 자체는 AutoEllipsis라 폭을 넘는 부분이 "..."으로
        //        잘려 보이므로(폭을 더 넓힐 여유가 없어서 — InitializeComponents()
        //        참고), 전체 문구는 마우스를 올렸을 때 툴팁으로 확인할 수
        //        있게 한다.
        //***************************************************************************
        private void SetCurrentRoomLabel(string fullText)
        {
            _lblCurrentRoom.Text = fullText;
            _roomStatusToolTip.SetToolTip(_lblCurrentRoom, fullText);
        }
    }
}