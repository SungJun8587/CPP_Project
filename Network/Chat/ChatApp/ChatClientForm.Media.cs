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
    // ChatClientForm.Media.cs : 파일 첨부 업로드(대용량 스트리밍 포함)·삭제, 프로필 이미지 설정/변경/업로드, URL 아바타·링크 미리보기 조회·캐싱 — 파일/이미지/외부 콘텐츠를 다루는 코드 전부.
    public partial class ChatClientForm
    {

        //***************************************************************************
        // @brief 파일 첨부 카드의 × 버튼 클릭 처리 — 파일 서버에 실제 DELETE
        //        요청을 보내고, 채팅 서버에도 messageId로 삭제를 요청해서
        //        같은 방/로비의 다른 사람들 화면에서도 지워지게 한다.
        // @details [수정 — messageId 기반으로 전환] 예전엔 파일 URL을 그대로
        //          식별자로 써서 서버가 재검증 없이 릴레이했는데, 이제는
        //          서버가 발급한 messageId + 소유권 검증(TryDeleteMessage())을
        //          거친다 — 남의 메시지 ID를 지정해도 NotOwner로 거부된다.
        //          로컬 화면에서 실제로 지우는 시점도 낙관적으로 바로
        //          지우지 않고, 서버의 DeleteChatMessageNotify가 돌아올
        //          때(OnDeleteChatMessageNotified)로 미뤘다 — 그래야 혹시
        //          소유권 검증에 실패해도 화면과 서버 상태가 어긋나지 않는다.
        // @details [알려진 한계] 파일 서버의 DELETE /images/{path}는 갤러리
        //          이미지 삭제와 마찬가지로 별도 인증이 없다(기존부터
        //          알려진 한계) — 파일 자체는 URL만 알면 누구나 지울 수
        //          있다. 다만 "채팅창에서 카드가 사라지는" 것은 이제
        //          messageId 소유권 검증을 거친다.
        //***************************************************************************
        private async void DeleteFileAttachment(int itemIndex)
        {
            if (itemIndex < 0 || itemIndex >= _listBoxChat.Items.Count)
                return;

            if (!(_listBoxChat.Items[itemIndex] is ChatBubbleItem item))
                return;

            Match fileMatch = FileAttachmentRegex.Match(item.Message);
            if (!fileMatch.Success)
                return;

            if (item.MessageId == 0)
            {
                // 서버가 아직 이 메시지의 진짜 ID를 안 보내온 상태(보낸 직후의
                // 아주 짧은 순간) — 잠시 후 다시 시도하도록 안내만 하고 끝낸다.
                MessageBox.Show(this, "메시지 전송이 아직 서버에 확인되지 않았습니다. 잠시 후 다시 시도해주세요.", "알림");
                return;
            }

            if (MessageBox.Show(this, $"'{fileMatch.Groups["name"].Value}' 파일을 삭제할까요?\n(서버에서도 삭제되어 되돌릴 수 없습니다)",
                "파일 삭제", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            {
                return;
            }

            string url = fileMatch.Groups["url"].Value;

            try
            {
                await _httpClient.DeleteAsync(url);
            }
            catch (Exception ex)
            {
                // 네트워크 오류 등 — 서버 파일은 못 지웠을 수 있지만, 채팅
                // 목록에서는 아래 messageId 삭제 요청 결과에 맡긴다.
                AppendSystemLog("[시스템] 파일 서버에서 삭제 요청이 실패했습니다: " + ex.Message, ColorSystemError);
            }

            // 채팅 서버에 messageId 기반 삭제를 요청한다 — 실제 화면 제거는
            // 서버가 소유권을 확인한 뒤 보내주는 DeleteChatMessageNotify를
            // 받았을 때(OnDeleteChatMessageNotified) 이뤄진다.
            _client?.RequestDeleteChatMessage(item.MessageId);
        }

        //***************************************************************************
        // @brief [추가] 채팅에 파일을 첨부해서 보낸다 — 프로필 이미지 업로드와
        //        동일한 토큰 발급 + 파일 서버 업로드 흐름을 그대로 재사용하고,
        //        업로드가 끝나면 그 URL을 "[FILE:파일명:바이트수]URL" 형식의
        //        특수 텍스트로 감싸 일반 채팅 메시지로 보낸다(새 패킷 타입
        //        없이 기존 프로토콜 재사용). 화면에는 ChatListBox_DrawItem이
        //        이 형식을 감지해서 일반 텍스트가 아니라 파일 카드로 그려준다.
        // @details [알려진 한계] 파일 서버의 MaxUploadBytes(프로필 이미지
        //          기준으로 설정된 값, 보통 2MB)가 그대로 적용된다 — 더 큰
        //          파일을 자주 보내야 한다면 파일 서버 설정에서 이 값을
        //          늘려야 한다. 또한 어떤 파일 형식이든 서버가 그대로
        //          저장만 하고(ImageResizeUtil은 이미지가 아니면 조용히
        //          건너뜀 — 별도 처리 불필요), 실행 파일 등 위험한 확장자에
        //          대한 별도 차단은 없다(데모 범위로 판단).
        //***************************************************************************
        //***************************************************************************
        // @brief [수정 — 대용량 파일 스트리밍] 예전엔 File.ReadAllBytes()로
        //        파일 전체를 메모리에 올린 뒤 업로드했다 — 1GB급 파일이면
        //        그 순간 클라이언트 프로세스 메모리에 파일 전체가 올라가야
        //        했다. 이제는 파일을 열어보지도 않고(존재/읽기 권한만
        //        확인) 경로와 크기만 들고 있다가, 실제 업로드
        //        (UploadToFileServerAsync)에서 FileStream을 열어 그 자리에서
        //        바로 네트워크로 흘려보낸다.
        //***************************************************************************
        private void AttachAndSendFile()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 파일을 보낼 수 있습니다.", "알림");
                return;
            }

            using (var dlg = new OpenFileDialog { Filter = "모든 파일|*.*" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                _ = SendFileAttachmentAsync(dlg.FileName);
            }
        }

        //***************************************************************************
        // @brief [추가] 파일 경로 하나를 업로드해서 채팅 메시지로 전송한다 —
        //        AttachAndSendFile()(파일 선택 대화상자로 고른 경우)과
        //        ChatListBox_DragDrop()(드래그 앤 드롭으로 받은 경우) 양쪽이
        //        공유하는 실제 업로드+전송 로직. 예전엔 AttachAndSendFile()
        //        안에 이 내용이 그대로 있었는데, 드래그 앤 드롭도 똑같은
        //        일을 해야 해서 재사용 가능하게 뽑아냈다.
        //***************************************************************************
        private async Task SendFileAttachmentAsync(string filePath)
        {
            if (_client == null)
                return;

            long fileLength;
            try
            {
                fileLength = new FileInfo(filePath).Length;
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "파일 정보를 읽지 못했습니다: " + ex.Message, "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }

            // 파일명에 콜론(:)이나 닫는 대괄호(])가 있으면
            // "[FILE:이름:크기]" 형식 파싱(FileAttachmentRegex)이 깨지므로
            // 미리 밑줄로 치환해둔다.
            string safeName = Path.GetFileName(filePath).Replace(":", "_").Replace("]", "_");

            AppendSystemLog("[시스템] 업로드 토큰 발급 중...", ColorSystemInfo);

            try
            {
                _uploadTokenTcs = new TaskCompletionSource<RequestUploadTokenResData>();
                _client.RequestUploadToken();

                var tokenResult = await WaitWithTimeout(_uploadTokenTcs.Task, TimeSpan.FromSeconds(10));
                if (tokenResult == null || !tokenResult.Success || string.IsNullOrEmpty(tokenResult.FileServerUrl))
                {
                    AppendSystemLog("[시스템] 업로드 토큰 발급 실패 — 파일 서버가 설정돼 있지 않을 수 있습니다.", ColorSystemError);
                    return;
                }

                AppendSystemLog($"[시스템] 파일 서버에 업로드 중... (0%, {FormatFileSize(fileLength)})", ColorSystemInfo);

                string uploadedUrl;
                try
                {
                    uploadedUrl = await UploadToFileServerAsync(tokenResult.FileServerUrl, tokenResult.UploadToken, filePath);
                }
                catch (Exception ex)
                {
                    AppendSystemLog("[시스템] 파일 서버 업로드 실패: " + ex.Message, ColorSystemError);
                    return;
                }

                if (string.IsNullOrEmpty(uploadedUrl))
                {
                    AppendSystemLog("[시스템] 파일 서버가 URL을 돌려주지 않았습니다.", ColorSystemError);
                    return;
                }

                string fileMessage = $"[FILE:{safeName}:{fileLength}]{uploadedUrl}";

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Enqueue(fileMessage);
                }

                AppendChat(_currentNickname ?? "나", _myProfileImageUrl, fileMessage, isMyMessage: true);
                _client?.SendChat(fileMessage);
            }
            finally
            {
                _uploadTokenTcs = null;
            }
        }

        //***************************************************************************
        // @brief 파일을 골라 서버에 청크로 업로드하고, 성공하면 대표 이미지로
        //        지정한다(서버가 업로드 완료 처리 안에서 곧바로 대표로 등록).
        // @details Begin -> Chunk*N(응답 없음, 그냥 순서대로 다 보냄) -> End
        //          -> End 응답 대기 순으로 진행한다. 각 단계의 서버 응답은
        //          이벤트로 오는데, TaskCompletionSource로 감싸서 이 메서드
        //          안에서 await로 순서대로 기다리는 형태로 만들었다.
        //***************************************************************************
        private async void UploadMyProfileImage()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 업로드할 수 있습니다.", "알림");
                return;
            }

            using (var dlg = new OpenFileDialog { Filter = "이미지 파일|*.png;*.jpg;*.jpeg;*.bmp;*.gif" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                if (!File.Exists(dlg.FileName))
                {
                    MessageBox.Show(this, "파일을 찾을 수 없습니다.", "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }

                AppendSystemLog("[시스템] 업로드 토큰 발급 중...", ColorSystemInfo);

                try
                {
                    _uploadTokenTcs = new TaskCompletionSource<RequestUploadTokenResData>();
                    _client.RequestUploadToken();

                    var tokenResult = await WaitWithTimeout(_uploadTokenTcs.Task, TimeSpan.FromSeconds(10));
                    if (tokenResult == null || !tokenResult.Success || string.IsNullOrEmpty(tokenResult.FileServerUrl))
                    {
                        AppendSystemLog("[시스템] 업로드 토큰 발급 실패 — 파일 서버가 설정돼 있지 않을 수 있습니다.", ColorSystemError);
                        return;
                    }

                    AppendSystemLog("[시스템] 파일 서버에 업로드 중...", ColorSystemInfo);

                    string uploadedUrl;
                    try
                    {
                        uploadedUrl = await UploadToFileServerAsync(tokenResult.FileServerUrl, tokenResult.UploadToken, dlg.FileName);
                    }
                    catch (Exception ex)
                    {
                        AppendSystemLog("[시스템] 파일 서버 업로드 실패: " + ex.Message, ColorSystemError);
                        return;
                    }

                    if (string.IsNullOrEmpty(uploadedUrl))
                    {
                        AppendSystemLog("[시스템] 파일 서버가 URL을 돌려주지 않았습니다.", ColorSystemError);
                        return;
                    }

                    // 업로드된 URL을 채팅 서버에 새 대표 이미지로 등록한다 —
                    // SetMyProfileImageUrl()과 동일한 등록 경로(SetProfileImageUrlReq)를
                    // 그대로 재사용한다. 성공/실패 결과 처리는
                    // OnSetProfileImageUrlResultReceived()가 공통으로 담당한다.
                    // [수정] 여기도 낙관적 업데이트 — 이유는 SetMyProfileImageUrl() 참고.
                    _pendingProfileImageUrlRequest = uploadedUrl;
                    _profileImageUrlBeforeRequest = _myProfileImageUrl;
                    ApplyProfileImageUrl(uploadedUrl);

                    _client.RequestSetProfileImageUrl(uploadedUrl);
                }
                finally
                {
                    _uploadTokenTcs = null;
                }
            }
        }

        //***************************************************************************
        // @brief 파일 서버에 이미지를 업로드하고, 결과로 접근 가능한 URL을 돌려받는다.
        // @details [파일 서버 API 계약] multipart/form-data POST
        //          {fileServerUrl}/upload — 폼 필드 "file"에 이미지 바이트,
        //          "token"에 채팅 서버가 발급한 업로드 토큰. 성공하면 본문에
        //          평문(text/plain)으로 접근 가능한 이미지 URL을 돌려준다.
        //          [설계] 파일 서버는 아직 별도로 구현되지 않았다 — 이 계약은
        //          채팅 서버 쪽 RequestUploadTokenRes 설계(토큰+파일서버주소
        //          발급, ChatServerMain::RequestUploadToken() 참고)와 짝을
        //          이루도록 클라이언트가 미리 정해둔 것이다. 실제 파일 서버
        //          구현이 이 계약과 다르게 나온다면 이 메서드만 고치면 되고,
        //          나머지 흐름(토큰 발급, 대표 이미지 등록)은 영향받지 않는다.
        //***************************************************************************
        //***************************************************************************
        // @brief 파일 서버에 파일을 업로드하고, 결과로 접근 가능한 URL을 돌려받는다.
        // @details [파일 서버 API 계약] multipart/form-data POST
        //          {fileServerUrl}/upload — 폼 필드 "file"에 파일 바이트,
        //          "token"에 채팅 서버가 발급한 업로드 토큰. 성공하면 본문에
        //          평문(text/plain)으로 접근 가능한 URL을 돌려준다.
        // @details [수정 — 대용량 파일 스트리밍] ByteArrayContent(파일 전체를
        //          메모리에 들고 있어야 함) 대신 StreamContent + FileStream을
        //          쓴다 — HttpClient가 이 스트림에서 내부 버퍼 크기만큼씩
        //          읽어 그때그때 네트워크로 흘려보내므로, 클라이언트 메모리에는
        //          파일 전체가 절대 안 올라간다. FileStream이 seek 가능하므로
        //          StreamContent가 자동으로 Content-Length를 계산해 붙여준다
        //          (서버가 chunked가 아니라 Content-Length 기반으로 파싱하므로
        //          이 부분이 중요하다).
        //          진행률은 ProgressFileStream이 읽은 바이트 수를 콜백으로
        //          알려주는 방식으로 표시한다 — 10%p 단위로만 로그를 찍어서
        //          큰 파일이어도 시스템 로그가 도배되지 않게 했다.
        //***************************************************************************
        private async Task<string> UploadToFileServerAsync(string fileServerUrl, string uploadToken, string localFilePath)
        {
            int lastReportedDecile = -1;

            using (var fileStream = new ProgressFileStream(localFilePath, (bytesRead, totalBytes) =>
            {
                if (totalBytes <= 0) return;
                int decile = (int)(bytesRead * 10 / totalBytes); // 0~10
                if (decile != lastReportedDecile)
                {
                    lastReportedDecile = decile;
                    int percent = decile * 10;
                    // 백그라운드(HttpClient 내부 읽기) 스레드에서 호출되므로 UI 스레드로 넘긴다.
                    BeginInvoke((MethodInvoker)delegate
                    {
                        AppendSystemLog($"[시스템] 파일 서버에 업로드 중... ({percent}%)", ColorSystemInfo);
                    });
                }
            }))
            using (var content = new MultipartFormDataContent())
            {
                var fileContent = new StreamContent(fileStream);
                fileContent.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/octet-stream");
                content.Add(fileContent, "file", Path.GetFileName(localFilePath));
                content.Add(new StringContent(uploadToken), "token");

                string uploadUrl = fileServerUrl.TrimEnd('/') + "/upload";
                HttpResponseMessage response = await _uploadHttpClient.PostAsync(uploadUrl, content);
                response.EnsureSuccessStatusCode();

                return (await response.Content.ReadAsStringAsync()).Trim();
            }
        }

        //***************************************************************************
        // @brief [추가] 읽은 바이트 수를 콜백으로 알려주는 FileStream — 대용량
        //        업로드 진행률 표시용. 읽기 전용으로만 쓰는 걸 전제로 동기/비동기
        //        Read 양쪽을 다 오버라이드했다(HttpClient가 어느 쪽을 타든
        //        진행률이 누락되지 않도록).
        //***************************************************************************
        private sealed class ProgressFileStream : FileStream
        {
            private readonly Action<long, long> _onProgress;
            private long _totalRead;

            public ProgressFileStream(string path, Action<long, long> onProgress)
                : base(path, FileMode.Open, FileAccess.Read, FileShare.Read, bufferSize: 81920, useAsync: true)
            {
                _onProgress = onProgress;
            }

            public override int Read(byte[] buffer, int offset, int count)
            {
                int n = base.Read(buffer, offset, count);
                _totalRead += n;
                _onProgress?.Invoke(_totalRead, Length);
                return n;
            }

            public override async Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken)
            {
                int n = await base.ReadAsync(buffer, offset, count, cancellationToken).ConfigureAwait(false);
                _totalRead += n;
                _onProgress?.Invoke(_totalRead, Length);
                return n;
            }
        }

        //***************************************************************************
        // @brief task가 timeout 안에 끝나면 그 결과를, 아니면 null을 반환한다.
        //        서버가 응답을 영영 안 주는 경우(연결 끊김 등)에 이 메서드
        //        호출부가 무한정 멈춰있지 않게 하기 위함.
        //***************************************************************************
        private static async Task<T> WaitWithTimeout<T>(Task<T> task, TimeSpan timeout) where T : class
        {
            var completed = await Task.WhenAny(task, Task.Delay(timeout));
            return completed == task ? task.Result : null;
        }

        //***************************************************************************
        // @brief 프로필 이미지 파일을 골라 _picProfileImage에 표시하고, 프로필
        //        이름별로 경로를 로컬에 저장해둔다(다음 실행 때 다시 불러옴).
        // @details [알려진 한계] 이 이미지는 순수 로컬 개인화용이다 — 서버
        //          프로토콜에 이미지 전송 기능이 없어서 다른 사용자에게는
        //          전혀 보이지 않는다. 실제로 공유하려면 서버에 이미지
        //          업로드/전달 경로를 새로 만들어야 한다.
        //***************************************************************************
        private void SetMyProfileImage()
        {
            using (var dlg = new OpenFileDialog { Filter = "이미지 파일|*.png;*.jpg;*.jpeg;*.bmp;*.gif" })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK)
                    return;

                try
                {
                    using (var original = Image.FromFile(dlg.FileName))
                    {
                        _picProfileImage.Image?.Dispose();
                        _picProfileImage.Image = new Bitmap(original); // 파일 핸들 의존성 제거
                    }
                    _picProfileImage.BackColor = Color.White;

                    if (!string.IsNullOrEmpty(_profileName))
                        SaveMyProfileImagePath(_profileName, dlg.FileName);
                }
                catch (Exception ex)
                {
                    MessageBox.Show(this, "이미지를 불러오지 못했습니다: " + ex.Message, "오류", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
            }
        }

        private static string ProfileImagePathFile(string profileName) =>
            Path.Combine(AppContext.BaseDirectory, "chat_profile_image_" + profileName + ".dat");

        private static void SaveMyProfileImagePath(string profileName, string imagePath)
        {
            try
            {
                File.WriteAllText(ProfileImagePathFile(profileName), imagePath);
            }
            catch
            {
                // 저장 실패해도 이번 세션 표시에는 지장 없음 — 다음 실행 때만 다시 물어보면 됨.
            }
        }

        //***************************************************************************
        // @brief 이전에 저장해둔 프로필 이미지가 있으면 불러와 표시한다.
        //        파일이 없거나 로드에 실패해도 조용히 무시(기본 회색 칸 유지).
        //***************************************************************************
        private void LoadMyProfileImage(string profileName)
        {
            try
            {
                string path = ProfileImagePathFile(profileName);
                if (!File.Exists(path))
                    return;

                string imagePath = File.ReadAllText(path).Trim();
                if (string.IsNullOrEmpty(imagePath) || !File.Exists(imagePath))
                    return;

                using (var original = Image.FromFile(imagePath))
                {
                    _picProfileImage.Image?.Dispose();
                    _picProfileImage.Image = new Bitmap(original);
                }
                _picProfileImage.BackColor = Color.White;
            }
            catch
            {
                // 손상된 파일 등 — 조용히 무시하고 기본 회색 칸 유지.
            }
        }

        //***************************************************************************
        // @brief 프로필 이미지를 URL로 설정 요청한다 — 성공하면 서버가 이
        //        URL을 계정에 저장하고, 이후 내가 보내는 모든 채팅 메시지에
        //        실려 다른 사람에게도 보인다(로컬 파일 방식과 달리 실제로 공유됨).
        //        서버 연결 전이면 조용히 무시한다.
        //***************************************************************************
        //***************************************************************************
        // @brief 서버가 알려준(또는 방금 내가 설정한) 내 프로필 이미지 URL을
        //        실제로 내려받아 _picProfileImage에 반영한다. 실패해도 조용히
        //        무시(기본 회색 칸 또는 이전 이미지 유지).
        //***************************************************************************
        private async void LoadMyProfileImageFromUrl(string url)
        {
            try
            {
                byte[] imageBytes = await FetchImageBytesAsync(url);
                if (imageBytes == null || imageBytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(imageBytes))
                using (var original = Image.FromStream(ms))
                {
                    _picProfileImage.Image?.Dispose();
                    _picProfileImage.Image = new Bitmap(original);
                }
                _picProfileImage.BackColor = Color.White;
            }
            catch
            {
                // 네트워크 오류 등 — 조용히 무시.
            }
        }

        private void SetMyProfileImageUrl()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 설정할 수 있습니다.", "알림");
                return;
            }

            string url = PromptForText(this, "프로필 이미지 URL", "이미지 URL (https://...)", _myProfileImageUrl);
            if (url == null) // 취소
                return;

            _pendingProfileImageUrlRequest = url;

            // [수정] 서버 응답(비동기 왕복)을 기다리지 않고 즉시 로컬에 먼저
            // 반영한다 — 그렇지 않으면 변경 직후 보낸 첫 메시지가 아직 안
            // 바뀐 옛 URL을 스냅샷으로 들고 가버려서(BtnSend_Click이 그
            // 시점의 _myProfileImageUrl을 그대로 씀), "메시지를 하나 더
            // 보내야 반영되는" 것처럼 보이는 문제가 있었다. 실패하면
            // OnSetProfileImageUrlResultReceived()가 이 값(_profileImageUrlBeforeRequest)으로
            // 되돌린다.
            _profileImageUrlBeforeRequest = _myProfileImageUrl;
            ApplyProfileImageUrl(url);

            _client.RequestSetProfileImageUrl(url);
        }

        //***************************************************************************
        // @brief 프로필 이미지 URL 공유를 해제한다(빈 문자열로 설정 요청).
        //***************************************************************************
        private void ClearMyProfileImageUrl()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 해제할 수 있습니다.", "알림");
                return;
            }

            _pendingProfileImageUrlRequest = string.Empty;

            // [수정] SetMyProfileImageUrl()과 동일한 이유로 낙관적 업데이트.
            _profileImageUrlBeforeRequest = _myProfileImageUrl;
            ApplyProfileImageUrl(string.Empty);

            _client.RequestSetProfileImageUrl(string.Empty);
        }

        //***************************************************************************
        // @brief "갤러리 관리" 메뉴 클릭 시 — 별도 창을 띄우는 대신 갤러리
        //        탭으로 전환한다(카카오톡 스타일 통합).
        //***************************************************************************
        private void SwitchToGalleryTab()
        {
            if (_client == null)
            {
                MessageBox.Show(this, "서버에 접속한 뒤에 갤러리를 볼 수 있습니다.", "알림");
                return;
            }

            _tabControl.SelectedIndex = 1; // 0=대화, 1=갤러리
        }

        //***************************************************************************
        // @brief 갤러리 탭에서 대표 이미지가 바뀔 때마다(선택/삭제 성공) 호출된다.
        //        예전(모달 다이얼로그)엔 창이 닫힌 뒤에야 한 번 반영했는데,
        //        이제는 탭을 안 벗어나도 즉시 반영된다.
        //***************************************************************************
        private void OnGalleryActiveImageChanged(string newImageRef)
        {
            ApplyProfileImageUrl(newImageRef);
        }

        //***************************************************************************
        // @brief 대표 프로필 이미지 URL이 바뀔 때(설정/업로드/해제/갤러리에서
        //        선택·삭제) 화면에 그 결과를 반영하는 단일 진입점. 채팅 탭의
        //        작은 프로필 사진과 갤러리 탭의 큰 대표 이미지, 두 군데를
        //        항상 같이 갱신해서 두 화면이 서로 어긋나지 않게 한다.
        //***************************************************************************
        private void ApplyProfileImageUrl(string url)
        {
            _myProfileImageUrl = url ?? string.Empty;

            if (string.IsNullOrEmpty(_myProfileImageUrl))
            {
                _picProfileImage.Image?.Dispose();
                _picProfileImage.Image = null;
                _picProfileImage.BackColor = ControlPaint.Light(AccentColor, 0.9f);
            }
            else
            {
                LoadMyProfileImageFromUrl(_myProfileImageUrl);
            }

            _galleryPanel.SetActiveImagePreview(_myProfileImageUrl);
        }

        //***************************************************************************
        // @brief url의 프로필 이미지를 요청한다(아바타용). 이미 캐시에 있거나
        //        요청이 진행 중이면 새로 요청하지 않는다 — RequestLinkPreview()와
        //        동일한 "URL 하나당 한 번만" 원칙.
        //***************************************************************************
        private void RequestAvatarImage(string url, int itemIndex)
        {
            if (string.IsNullOrEmpty(url))
                return;

            lock (_avatarImageCache)
            {
                if (_avatarImageCache.ContainsKey(url) || _avatarFetchInProgress.Contains(url))
                    return;
                _avatarFetchInProgress.Add(url);
            }

            _ = FetchAvatarImageAsync(url, itemIndex);
        }

        //***************************************************************************
        // @brief imageRef로 이미지 바이트를 가져온다. 링크 미리보기 썸네일,
        //        아바타, 내 프로필 이미지 표시 등 이미지가 필요한 모든 곳이
        //        이 헬퍼 하나로 통일해서 쓴다.
        // @details [설계 변경] 예전엔 "local:" 접두사가 있으면 채팅 서버와의
        //          TCP 청크 다운로드 프로토콜을 썼는데, 이미지 저장/서빙을
        //          별도 파일 서버로 분리하면서 그 프로토콜 자체가 없어졌다 —
        //          이제 모든 이미지 참조는 실제로 HTTP(S)로 접근 가능한 URL
        //          (외부 호스팅이든, 우리 파일 서버가 돌려준 주소든)이라
        //          HttpClient 하나로 통일된다.
        //***************************************************************************
        private async Task<byte[]> FetchImageBytesAsync(string imageRef)
        {
            return await _httpClient.GetByteArrayAsync(imageRef);
        }


        private async Task FetchAvatarImageAsync(string url, int itemIndex)
        {
            try
            {
                byte[] imageBytes = await FetchImageBytesAsync(url);
                if (imageBytes == null || imageBytes.Length == 0)
                    return;

                using (var ms = new MemoryStream(imageBytes))
                using (var original = Image.FromStream(ms))
                {
                    lock (_avatarImageCache)
                    {
                        _avatarImageCache[url] = new Bitmap(original);
                    }
                }
            }
            catch
            {
                // 네트워크 오류, 잘못된 URL, 이미지 디코딩 실패 등 — 조용히 무시.
            }
            finally
            {
                lock (_avatarImageCache)
                {
                    _avatarFetchInProgress.Remove(url);
                }
                RefreshChatItem(itemIndex);
            }
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
        // @details [수정 — 실제 재현된 버그] 예전엔 Items[index]에 같은
        //          참조를 그대로 재대입하는 트릭을 썼는데, WinForms가 참조가
        //          그대로면 "이 항목은 안 바뀌었다"고 판단해서 MeasureItem이
        //          다시 안 불리는 경우가 있었다 — 그러면 링크 미리보기 카드
        //          공간(높이) 자체가 안 잡혀서 화면에 안 보이다가, 나중에
        //          "다른" 메시지가 새로 추가되면서 리스트 전체가 다시
        //          측정/그려질 때 우연히 같이 반영되는 것처럼 보였다(실제로
        //          "메시지를 하나 더 보내야 반영된다"는 증상으로 재현됨).
        //          Items[index]를 진짜로 제거했다가 다시 넣으면 WinForms가
        //          확실히 새 항목으로 인식해서 MeasureItem/DrawItem을 항상
        //          다시 태운다.
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
                    _listBoxChat.Items.RemoveAt(itemIndex);
                    _listBoxChat.Items.Insert(itemIndex, existing);
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
    }
}