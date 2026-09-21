
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
    // ChatClientForm.Events.cs : ChatNetworkClient가 발생시키는 서버 이벤트 핸들러와, 버튼 클릭 등 UI 입력 이벤트 핸들러 전부.
    public partial class ChatClientForm
    {

        // ── 네트워크 콜백들 — 전부 백그라운드 수신 스레드에서 호출된다.
        // UI 컨트롤은 반드시 Invoke()로 UI 스레드에 넘긴 뒤에만 건드린다. ──

        private void OnLoginResultReceived(LoginResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    AccountStorage.Save(_profileName, res.PublicId, res.Token);

                    // [수정] 접속 성공 = 초록으로 되돌림("성공은 초록"이라는
                    // 일반적인 관례대로).
                    SetStatus("연결됨", Color.FromArgb(47, 184, 112));

                    _currentNickname = res.Nickname;
                    _myProfileImageUrl = res.ProfileImageUrl ?? string.Empty;
                    AppendSystemLog("[시스템] 로그인 성공 - 닉네임: " + res.Nickname, ColorSystemOk);

                    // 서버가 기억하고 있는(=다른 사람에게 보이는) 프로필
                    // 이미지가 있으면 내 화면에도 그 실제 이미지를 반영한다
                    // — 로컬에서 마지막으로 설정한 이미지와 다를 수 있으므로
                    // (다른 기기에서 URL로 설정했을 수 있음) 로그인 시점의
                    // 서버 값을 우선한다.
                    if (!string.IsNullOrEmpty(_myProfileImageUrl))
                        LoadMyProfileImageFromUrl(_myProfileImageUrl);

                    // 갤러리는 로그인된 세션이 있어야 조회 가능(서버가
                    // IsLoggedIn() 확인)하므로, TCP 연결 시점이 아니라
                    // 로그인 성공 시점에 붙인다.
                    _galleryPanel.AttachClient(_client);
                    _roomListPanel.AttachClient(_client);

                    // 서버가 로그인 직후 자동으로 로비에 배정한다 — 클라이언트도
                    // 그 전제로 현재 위치를 로비로 잡아둔다(서버의 RoomUserCountNotify가
                    // 곧이어 도착해 실제 인원수도 채워줄 것).
                    _currentRoomId = ProtocolConstants.LobbyRoomId;
                    UpdateRoomStatusUI();

                    _btnSend.Enabled = true;
                    _btnAttachFile.Enabled = true;
                    _txtMessage.Enabled = true;
                    _btnOpenNicknameDialog.Visible = true;
                    _picProfileImage.Visible = true;

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
        //***************************************************************************
        // @brief 채팅 메시지 수신. _pendingSentEchoes의 맨 앞과 일치하면 방금
        //        내가 보낸 메시지의 서버 에코이므로 조용히 소비하고 화면엔
        //        찍지 않는다(BtnSend_Click에서 이미 "나: ..."로 찍었음).
        //        일치하지 않으면 다른 사람이 보낸 메시지이므로, 서버가
        //        채워 보낸 발신자 닉네임을 그대로 표시한다.
        // @details [추가] 내 에코일 때도 그냥 버리지 않는다 — 이 에코에는
        //          서버가 방금 발급한 진짜 messageId가 실려있으므로, 이미
        //          화면에 찍어둔(그때는 messageId=0으로 미확정이었던) 내
        //          말풍선을 찾아 그 값을 채워준다. 그래야 나중에 그 메시지를
        //          지우고 싶을 때 어떤 ID를 보내야 할지 알 수 있다.
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
            {
                Invoke((MethodInvoker)delegate { ApplyServerMessageId(data.Message, data.MessageId); });
                return;
            }

            Invoke((MethodInvoker)delegate { AppendChat(data.SenderNickname, data.SenderProfileImageUrl, data.Message, isMyMessage: false, data.MessageId); });
        }

        //***************************************************************************
        // @brief [추가] 아직 messageId가 미확정(0)인 내 말풍선 중, 방금 온
        //        에코와 내용이 같은 가장 최근 항목을 찾아 서버가 부여한
        //        진짜 ID를 채워넣는다.
        //***************************************************************************
        private void ApplyServerMessageId(string message, long messageId)
        {
            for (int i = _listBoxChat.Items.Count - 1; i >= 0; i--)
            {
                if (_listBoxChat.Items[i] is ChatBubbleItem item
                    && item.IsMyMessage && item.MessageId == 0 && item.Message == message)
                {
                    item.MessageId = messageId;
                    break;
                }
            }
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
                    AppendSystemLog("[시스템] 방 입장 실패 - " + DescribeRoomResult(res.Reason), ColorSystemError);
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
                    _currentRoomName = null;
                    _currentRoomOwnerNickname = null;
                    _txtRoomUserCount.Text = ""; // 로비 자체 인원수는 이 라벨이 아니라 RoomUserCountNotify로 별도 관리하지 않음(단순화)
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

        //***************************************************************************
        // @brief 프로필 이미지 URL 설정/해제 요청에 대한 서버 응답 처리.
        //        응답 패킷 자체엔 URL이 없어서, 요청 시점에 이 폼이 기억해둔
        //        _pendingProfileImageUrlRequest를 그대로 참고한다.
        // @details [수정] 요청을 보낸 시점(SetMyProfileImageUrl() 등)에 이미
        //          낙관적으로 화면에 반영해뒀으므로, 성공 시엔 다시 반영할
        //          필요가 없다(이미 반영돼있음) — 실패했을 때만 요청 직전
        //          값(_profileImageUrlBeforeRequest)으로 되돌린다.
        //***************************************************************************
        private void OnSetProfileImageUrlResultReceived(SetProfileImageUrlResPacketData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.Success)
                {
                    if (string.IsNullOrEmpty(_myProfileImageUrl))
                        AppendSystemLog("[시스템] 프로필 이미지가 해제되었습니다.", ColorSystemOk);
                    else
                        AppendSystemLog("[시스템] 프로필 이미지가 설정되었습니다 - " + _myProfileImageUrl, ColorSystemOk);

                    // [추가] 업로드/URL설정/해제 전부 이 응답 하나로 귀결되므로
                    // (SetProfileImageUrlReq 공통 경로), 여기서 갤러리 탭의
                    // 썸네일 목록을 바로 다시 불러온다 — 방금 바뀐 대표 표시나
                    // 새로 추가된 사진이 갤러리 탭을 다시 열지 않아도 곧바로
                    // 보이게 한다. 삭제는 ProfileImageGalleryPanel 안에서
                    // 자체적으로 이미 이렇게 처리하고 있다(OnDeleteResultReceived).
                    _galleryPanel.RefreshList();
                }
                else
                {
                    ApplyProfileImageUrl(_profileImageUrlBeforeRequest ?? string.Empty);
                    AppendSystemLog("[시스템] 프로필 이미지 설정 실패 - 서버 오류", ColorSystemError);
                }
            });
        }

        //***************************************************************************
        // @brief [추가] 내가 요청한 메시지 삭제의 결과. 성공이면 별도로 할
        //        일이 없다 — 곧이어 도착할 DeleteChatMessageNotify(요청자
        //        본인도 받음)가 실제 화면 제거를 담당한다. 실패(주로
        //        NotOwner — 이미 다른 클라이언트에서 지워졌거나 추적 창을
        //        벗어난 경우)면 사용자에게 알린다.
        //***************************************************************************
        private void OnDeleteChatMessageResultReceived(DeleteChatMessageResData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (!data.Success)
                    AppendSystemLog("[시스템] 메시지를 삭제하지 못했습니다 (" + data.Reason + ").", ColorSystemError);
            });
        }

        //***************************************************************************
        // @brief [추가] 같은 방/로비의 누군가(나 포함)가 메시지 삭제에
        //        성공했다는 브로드캐스트 처리. messageId가 일치하는 항목을
        //        찾아 내 화면에서도 제거한다. 뒤에서부터 찾는 이유: 보통
        //        최근에 지운 메시지일 가능성이 높아 평균적으로 더 빨리 찾는다.
        //***************************************************************************
        private void OnDeleteChatMessageNotified(DeleteChatMessageNotifyData data)
        {
            Invoke((MethodInvoker)delegate
            {
                for (int i = _listBoxChat.Items.Count - 1; i >= 0; i--)
                {
                    if (_listBoxChat.Items[i] is ChatBubbleItem item && item.MessageId == data.MessageId)
                    {
                        _listBoxChat.Items.RemoveAt(i);
                        _chatItemDeleteButtonRegions.Remove(i);
                        _chatItemUrlRegions.Remove(i);
                        break;
                    }
                }
            });
        }

        private void OnDisconnected()
        {
            Invoke((MethodInvoker)delegate
            {
                SetStatus("연결 끊김", Color.Gray);
                _serverUserCountPollTimer.Stop();
                _txtServerUserCount.Text = "";
                _txtLobbyUserCount.Text = "";
                AppendSystemLog("[시스템] 서버와 연결이 끊어졌습니다.", ColorSystemError);
                _btnConnect.Enabled = true;
                _btnConnect.Text = "접속";
                _btnSend.Enabled = false;
                _btnAttachFile.Enabled = false;
                _txtMessage.Enabled = false;
                _btnOpenNicknameDialog.Visible = false;
                _picProfileImage.Visible = false;

                // [추가] 접속 시도 때 잠갔던 필드를 다시 풀어준다.
                _txtServerIp.Enabled = true;
                _txtServerPort.Enabled = true;
                _txtProfileName.Enabled = true;
                ForceFullRedraw(_txtServerIp);
                ForceFullRedraw(_txtServerPort);
                ForceFullRedraw(_txtProfileName);

                _currentRoomId = -1;
                _txtRoomUserCount.Text = "";
                UpdateRoomStatusUI();

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Clear();
                }

                _galleryPanel.DetachClient();
                _roomListPanel.DetachClient();
            });
        }

        private void OnErrorOccurred(Exception ex)
        {
            Invoke((MethodInvoker)delegate { AppendSystemLog("[오류] " + ex.Message, ColorSystemError); });
        }

        //***************************************************************************
        // @brief 간단한 한 줄 텍스트 입력 대화상자. WinForms에 내장 InputBox가
        //        없어서 최소 구성으로 직접 만들었다.
        // @return 확인을 누르면 입력한 문자열(트림됨), 취소하면 null.
        //***************************************************************************
        private static string PromptForText(IWin32Window owner, string title, string label, string initialValue)
        {
            using (var dlg = new Form())
            {
                dlg.Text = title;
                dlg.FormBorderStyle = FormBorderStyle.FixedDialog;
                dlg.StartPosition = FormStartPosition.CenterParent;
                dlg.ClientSize = new Size(360, 110);
                dlg.MaximizeBox = false;
                dlg.MinimizeBox = false;
                dlg.ShowInTaskbar = false;

                var lbl = new Label { Text = label, Left = 10, Top = 15, Width = 340 };
                var txt = new TextBox { Left = 10, Top = 35, Width = 340, Text = initialValue ?? string.Empty };
                var btnOk = new Button { Text = "확인", Left = 190, Top = 70, Width = 75, DialogResult = DialogResult.OK };
                var btnCancel = new Button { Text = "취소", Left = 275, Top = 70, Width = 75, DialogResult = DialogResult.Cancel };

                dlg.Controls.AddRange(new Control[] { lbl, txt, btnOk, btnCancel });
                dlg.AcceptButton = btnOk;
                dlg.CancelButton = btnCancel;

                return dlg.ShowDialog(owner) == DialogResult.OK ? txt.Text.Trim() : null;
            }
        }

        //***************************************************************************
        // @brief 접속/끊기를 하나의 버튼으로 통합 — 현재 버튼 텍스트로 상태를
        //        판단한다("접속"이면 연결 시도, "끊기"면 연결 종료).
        //***************************************************************************
        private void BtnConnect_Click(object sender, EventArgs e)
        {
            if (_btnConnect.Text == "끊기")
            {
                _client?.Close();
                return;
            }

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
            LoadMyProfileImage(profileName);
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

            // [추가] 방 관리 — CreateRoomResultReceived/RoomListItemReceived/
            // RoomListEndReceived는 여기서 구독하지 않는다(RoomListPanel(채팅방 탭)이
            // 열려있는 동안만 자체적으로 구독/해지) — 메인 폼은 방 생성
            // "지름길" 버튼(BtnCreateRoom_Click)의 결과만 알면 되므로
            // CreateRoomResultReceived만 구독한다.
            _client.CreateRoomResultReceived += OnCreateRoomResultReceived;
            _client.DeleteRoomResultReceived += OnDeleteRoomResultReceived;
            _client.DeleteRoomNotified += OnDeleteRoomNotified;
            _client.RenameRoomResultReceived += OnRenameRoomResultReceived;
            _client.RenameRoomNotified += OnRenameRoomNotified;
            _client.RoomOwnerChangedNotified += OnRoomOwnerChangedNotified;
            _client.ServerUserCountReceived += OnServerUserCountReceived;
            _client.SetProfileImageUrlResultReceived += OnSetProfileImageUrlResultReceived;
            _client.DeleteChatMessageResultReceived += OnDeleteChatMessageResultReceived;
            _client.DeleteChatMessageNotified += OnDeleteChatMessageNotified;
            // [추가] 업로드 토큰 발급 응답은 UploadMyProfileImage()의 await 흐름과
            // TaskCompletionSource로 연결한다 — 진행 중인 요청이 없을 때
            // (필드가 null일 때) 도착하면 조용히 무시한다.
            _client.UploadTokenReceived += data => _uploadTokenTcs?.TrySetResult(data);
            _client.Disconnected += OnDisconnected;
            _client.ErrorOccurred += OnErrorOccurred;

            _client.Connect(_txtServerIp.Text.Trim(), port, hasToken, profileName, publicId, token);

            SetStatus(hasToken ? "재접속 중..." : "가입 중...", Color.Orange);
            AppendSystemLog(hasToken ? "[시스템] 저장된 계정으로 재접속을 시도합니다." : "[시스템] 신규 가입을 시도합니다.", ColorSystemInfo);

            // [수정] 접속/끊기를 하나의 버튼으로 통합 — 연결 시도 시작과 동시에
            // "끊기"로 바뀌어서 진행 중인 접속 시도도 바로 취소할 수 있게 한다.
            _btnConnect.Text = "끊기";

            // [추가] 접속 시도 중/접속된 동안엔 이 값들을 바꿀 수 없게 잠근다 —
            // 이미 시작된(또는 진행 중인) 연결의 대상을 몰래 바꾸는 걸 막기 위함.
            // Disconnected 콜백에서 다시 풀어준다.

            // [수정 — 포커스가 있던 컨트롤만 테두리가 안 바뀌는 문제] 포커스가
            // 있는 컨트롤을 Enabled=false로 만들면 Windows가 포커스를 다른
            // 곳으로 옮기는 처리를 같이 해야 하는데, 그 과정과 아래 강제
            // 재도색이 꼬이지 않도록 먼저 포커스를 셋과 무관한 곳(접속
            // 버튼)으로 옮겨둔다.
            _btnConnect.Focus();

            _txtServerIp.Enabled = false;
            _txtServerPort.Enabled = false;
            _txtProfileName.Enabled = false;

            // [수정 — 진짜 원인] TextBox의 BorderStyle.Fixed3D 테두리는
            // WS_EX_CLIENTEDGE 확장 스타일로 그려지는 논클라이언트 영역이라
            // Invalidate()/Refresh()로는 절대 갱신이 안 된다 — BorderStyle을
            // 순간적으로 껐다 켜는 방법도 시도했지만 그것도 확실히 해결되지
            // 않았다. Win32 RedrawWindow()를 RDW_FRAME과 함께 직접 호출해서
            // 논클라이언트 영역(테두리)까지 OS 수준에서 강제로 다시 그리게
            // 한다 — 이게 이 문제를 해결하는 가장 직접적인 방법이다.
            ForceFullRedraw(_txtServerIp);
            ForceFullRedraw(_txtServerPort);
            ForceFullRedraw(_txtProfileName);
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

            AppendChat(_currentNickname ?? "나", _myProfileImageUrl, msg, isMyMessage: true);

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

        //***************************************************************************
        // @brief [수정] "방 목록" 클릭 — 이제 모달 다이얼로그 대신 "채팅방"
        //        그 안에서 목록 조회/생성/입장을 전부 처리하고, 실제로
        //        입장에 성공하면 EnteredRoom에 방 이름/방장 정보가 담겨
        //        돌아온다 — 그 값으로 로컬 상태(_currentRoomName/
        //        _currentRoomOwnerNickname)를 갱신한다(RoomEnterResPacket
        //        자체엔 이 정보가 없어서 이 다이얼로그가 보완해준다).
        //***************************************************************************
        //***************************************************************************
        // @brief [수정] "방 목록" 클릭 — 이제 모달 다이얼로그 대신 "채팅방"
        //        탭(RoomListPanel, 갤러리와 동일한 내장 패널 방식)으로
        //        전환한다. 실제 목록 조회/생성/입장은 전부 그 패널이 처리하고,
        //        입장에 성공하면 OnRoomListPanelRoomEntered()가 대화 탭으로
        //        자동 전환해준다.
        //***************************************************************************
        private void BtnRoomList_Click(object sender, EventArgs e)
        {
            _tabControl.SelectedIndex = 1; // 0=대화, 1=채팅방, 2=갤러리
        }

        //***************************************************************************
        // @brief [추가] RoomListPanel에서 방 입장에 성공했을 때(직접 클릭
        //        또는 그 안의 "방 만들기") 호출된다 — 방 이름/방장 상태를
        //        갱신하고 대화 탭으로 자동 전환한다. _currentRoomId 자체는
        //        대화 탭이 항상 구독 중인 OnRoomEnterResultReceived()가
        //        이미 갱신했을 것이므로 여기서 다시 건드리지 않는다.
        //***************************************************************************
        private void OnRoomListPanelRoomEntered(RoomListItemData room)
        {
            _currentRoomName = room.Name;
            _currentRoomOwnerNickname = room.OwnerNickname;
            UpdateRoomStatusUI();
            _tabControl.SelectedIndex = 0; // 대화 탭으로 전환
        }

        //***************************************************************************
        // @brief [추가] "방 만들기" 클릭 — RoomListPanel(채팅방 탭)을 거치지 않고
        //        메인 화면에서 바로 만들 수 있는 지름길. 이름만 물어보고
        //        CreateRoomReq를 보낸 뒤 결과는 OnCreateRoomResultReceived()가
        //        처리한다(성공 시 그 방으로 바로 입장까지 시도).
        //***************************************************************************
        private void BtnCreateRoom_Click(object sender, EventArgs e)
        {
            if (_client == null)
                return;

            string roomName = PromptForText(this, "방 만들기", "방 이름을 입력하세요:", "");
            if (string.IsNullOrWhiteSpace(roomName))
                return;

            // [추가] CreateRoomResData엔 roomId만 오고 이름은 안 온다 —
            // OnCreateRoomResultReceived()가 _currentRoomName을 채울 수
            // 있도록 방금 입력한 이름을 잠시 기억해둔다(RoomListPanel을
            // 거친 경우엔 그 패널이 목록에서 직접 찾아 보완하므로 이 필드가
            // 필요 없지만, 이 지름길 경로는 목록 자체를 조회한 적이 없어서
            // 이렇게 하지 않으면 방 이름을 알 방법이 없다).
            _pendingCreatedRoomName = roomName.Trim();
            _client.RequestCreateRoom(_pendingCreatedRoomName);
        }

        //***************************************************************************
        // @brief [추가] "이름변경" 클릭 — 방장일 때만 보이는 버튼
        //        (UpdateRoomStatusUI() 참고). 새 이름을 물어보고
        //        RenameRoomReq를 보낸다 — 성공 여부/실제 반영은
        //        OnRenameRoomResultReceived()/OnRenameRoomNotified()가 처리한다.
        //***************************************************************************
        private void BtnRenameRoom_Click(object sender, EventArgs e)
        {
            if (_client == null || _currentRoomId <= ProtocolConstants.LobbyRoomId)
                return;

            string newName = PromptForText(this, "방 이름 변경", "새 방 이름을 입력하세요:", _currentRoomName ?? "");
            if (string.IsNullOrWhiteSpace(newName))
                return;

            _client.RequestRenameRoom(_currentRoomId, newName.Trim());
        }

        //***************************************************************************
        // @brief [추가] "방 삭제" 클릭 — 방장일 때만 보이는 버튼. 확인
        //        후 DeleteRoomReq를 보낸다 — 성공하면 서버가 이 클라이언트를
        //        포함해 그 방에 있던 모두를 로비로 옮기고 DeleteRoomNotify를
        //        보내주므로(OnDeleteRoomNotified()), 여기서 직접 화면을
        //        로비로 되돌리지는 않는다.
        //***************************************************************************
        private void BtnDeleteRoom_Click(object sender, EventArgs e)
        {
            if (_client == null || _currentRoomId <= ProtocolConstants.LobbyRoomId)
                return;

            if (MessageBox.Show(this, $"'{_currentRoomName}' 방을 삭제할까요?\n방에 있던 모든 사람이 로비로 이동됩니다.",
                "방 삭제", MessageBoxButtons.YesNo, MessageBoxIcon.Warning) != DialogResult.Yes)
            {
                return;
            }

            _client.RequestDeleteRoom(_currentRoomId);
        }

        private void BtnRoomLeave_Click(object sender, EventArgs e)
        {
            _client?.RequestRoomLeave();
        }

        private void SetStatus(string text, Color color)
        {
            _lblStatus.Text = text;
            _lblStatus.ForeColor = color;
            _pnlStatusDot?.Invalidate(); // 점은 _lblStatus.ForeColor를 그대로 참조해서 그리므로 다시 그리기만 하면 됨
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

        //***************************************************************************
        // @brief [추가] "방 만들기" 지름길 버튼(BtnCreateRoom_Click)의 결과.
        //        채팅방 탭(RoomListPanel)의 "방 만들기"로 만든 경우엔 그
        //        패널 자신이 별도로 이 이벤트를 구독해 처리하므로(RoomListPanel.
        //        OnCreateRoomResultReceived), 그 탭이 열려있는 동안엔 이
        //        핸들러도 같이 불린다 — 성공 시 여기서도 동일하게 자동
        //        입장을 시도하지만, 이미 같은 방에 입장 중이면 서버 입장에서
        //        중복 RoomEnterReq는 그냥 "이미 그 방에 있음"으로 안전하게
        //        처리되므로 문제없다.
        //***************************************************************************
        private void OnCreateRoomResultReceived(CreateRoomResData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (!data.Success)
                {
                    AppendSystemLog("[시스템] 방 생성 실패 - " + DescribeRoomResult(data.Reason), ColorSystemError);
                    return;
                }

                // [수정 — 버그] CreateRoomResultReceived는 이 핸들러와
                // RoomListPanel.OnCreateRoomResultReceived() 둘 다 구독하고
                // 있다 — "채팅방" 탭의 자체 "방 만들기"로 생성했을 때도
                // 이 핸들러가 똑같이 불려서, 예전엔 여기서도 무조건
                // RequestRoomEnter()를 또 보냈다. 그러면 입장 요청이 두 번
                // 나가고, 두 번째 RoomEnterRes가 도착했을 때 RoomListPanel
                // 쪽의 _pendingCreatedRoomName은 이미 첫 응답 처리 때
                // null로 비워진 뒤라 "(새 방)"으로 덮어써버리는 경쟁 상태가
                // 있었다(반대 방향도 마찬가지 — RoomListPanel.cs의
                // OnCreateRoomResultReceived() 주석 참고). 이 핸들러 자신의
                // _pendingCreatedRoomName이 비어있다는 건 "이 방 만들기
                // 버튼(채팅 탭 지름길)으로 생성된 게 아니다"라는 뜻이므로,
                // 그 경우엔 아무것도 안 하고 RoomListPanel 쪽 처리에 맡긴다.
                if (_pendingCreatedRoomName == null)
                    return;

                AppendSystemLog($"[시스템] 방 생성 성공(roomId={data.RoomId}) - 입장 중...", ColorSystemOk);

                // 방금 입력했던 이름을 그대로 쓰고, 방장은 나 자신이므로
                // 내 닉네임으로 채운다.
                _currentRoomName = _pendingCreatedRoomName;
                _currentRoomOwnerNickname = _currentNickname;
                _pendingCreatedRoomName = null;

                _client?.RequestRoomEnter(data.RoomId);
            });
        }

        //***************************************************************************
        // @brief [추가] 방 삭제 요청(BtnDeleteRoom_Click)의 결과 — 요청자
        //        본인에게만 온다. 실제로 로비로 옮겨지는 것은 이 응답이
        //        아니라 곧이어 오는 DeleteRoomNotify(그 방 멤버 전원에게)가
        //        처리한다 — 여기서는 성공/실패 로그만 남긴다.
        //***************************************************************************
        private void OnDeleteRoomResultReceived(DeleteRoomResData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (!data.Success)
                    AppendSystemLog("[시스템] 방 삭제 실패 - " + DescribeRoomResult(data.Reason), ColorSystemError);
            });
        }

        //***************************************************************************
        // @brief [추가] 방 삭제 알림 — 삭제된 방에 있던 멤버 전원에게(요청자
        //        포함) 온다. 서버가 이미 그 멤버들을 실제로 로비로 옮긴
        //        뒤에 보내는 알림이므로, 클라이언트는 자기 화면 상태만
        //        로비로 맞춰주면 된다(별도로 RoomLeaveReq를 보낼 필요 없음).
        //***************************************************************************
        private void OnDeleteRoomNotified(DeleteRoomNotifyData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.RoomId != _currentRoomId)
                    return; // 내가 있던 방이 아니면(다른 방 삭제 알림이 잘못 온 경우는 없지만 방어적으로) 무시

                AppendSystemLog("[시스템] 방이 삭제되어 로비로 이동했습니다.", ColorSystemInfo);

                _currentRoomId = ProtocolConstants.LobbyRoomId;
                _currentRoomName = null;
                _currentRoomOwnerNickname = null;
                _txtRoomUserCount.Text = "";
                UpdateRoomStatusUI();
            });
        }

        //***************************************************************************
        // @brief [추가] 방 이름 변경 요청(BtnRenameRoom_Click)의 결과 —
        //        요청자 본인에게만 온다. 실제 이름 반영은 곧이어 오는
        //        RenameRoomNotify(그 방 멤버 전원에게)가 처리한다.
        //***************************************************************************
        private void OnRenameRoomResultReceived(RenameRoomResData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (!data.Success)
                    AppendSystemLog("[시스템] 방 이름 변경 실패 - " + DescribeRoomResult(data.Reason), ColorSystemError);
            });
        }

        //***************************************************************************
        // @brief [추가] 방 이름 변경 알림 — 그 방에 있는 멤버 전원에게
        //        (요청자 포함) 온다.
        //***************************************************************************
        private void OnRenameRoomNotified(RenameRoomNotifyData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.RoomId != _currentRoomId)
                    return;

                _currentRoomName = data.NewName;
                UpdateRoomStatusUI();
                AppendSystemLog($"[시스템] 방 이름이 '{data.NewName}'(으)로 변경되었습니다.", ColorSystemInfo);
            });
        }

        //***************************************************************************
        // @brief [추가] 방장 자동 이양 알림 — 방장이 나가서 서버가 남은
        //        멤버 중 가장 오래 있었던 사람에게 자동으로 넘겼을 때, 그
        //        방에 있는 멤버 전원에게(새 방장 포함) 온다.
        //***************************************************************************
        private void OnRoomOwnerChangedNotified(RoomOwnerChangedNotifyData data)
        {
            Invoke((MethodInvoker)delegate
            {
                if (data.RoomId != _currentRoomId)
                    return;

                _currentRoomOwnerNickname = data.NewOwnerNickname;
                UpdateRoomStatusUI();

                bool isMeNewOwner = data.NewOwnerNickname == _currentNickname;
                AppendSystemLog(isMeNewOwner
                    ? "[시스템] 방장이 나가서 내가 새 방장이 되었습니다."
                    : $"[시스템] 방장이 나가서 '{data.NewOwnerNickname}'님이 새 방장이 되었습니다.",
                    ColorSystemInfo);
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
    }
}