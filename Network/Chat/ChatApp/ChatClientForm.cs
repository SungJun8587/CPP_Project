
//***************************************************************************
// ChatClientForm.cs : 메인 창 — 접속/채팅/닉네임 변경/방 입퇴장 UI.
//
// [설계] Designer.cs 없이 코드로 컨트롤을 직접 배치했다(단일 파일 전달을
// 위한 단순화 — 실제 프로젝트라면 Visual Studio 디자이너로 분리하는 걸
// 권장). ChatNetworkClient의 이벤트는 백그라운드 스레드에서 오므로, 전부
// Invoke()로 UI 스레드에 넘긴 뒤에야 컨트롤을 건드린다.
//
// [UI 구조 — 참고 프로젝트와의 차이] 이 레이아웃은 사용자가 준 SignalR
// 기반 채팅 클라이언트 예시(그룹박스 2개 + OwnerDraw 리스트박스 2개 +
// 방 콤보박스/입장/퇴장/방 인원수)의 골격을 그대로 따랐다 — 서버에
// 로비/룸 개념이 추가되면서 이제 이 컨트롤들이 실제로 동작한다. 다만
// 아래는 여전히 대응 기능이 없어서 뺐다:
//   - 동접자수(전체 서버 접속자 수) 조회 — 프로토콜에 없음. 방 인원수는 있음.
//   - URL/FpID 필드 — SignalR(HTTP) 전용 개념. 이 프로젝트는 순수 TCP라
//     "서버 IP + 포트 + 프로필 이름"으로 대체했다.
//   - "유저 가입"/"연결" 버튼 분리 — 이 프로젝트의 Connect()는 로컬 저장된
//     계정 유무로 신규가입/재접속을 자동 판별하므로 버튼 하나로 통합.
//
// [설계 — 로비/룸] 로그인하면 서버가 자동으로 로비(RoomId=0)에 배정한다.
// cbChatRoomId에서 방을 골라 "입장" 하면 그 방으로 이동, "나가기"를 누르면
// 로비로 돌아간다. 방 인원수는 서버가 RoomEnterRes/RoomLeaveRes 응답
// 뿐만 아니라 RoomUserCountNotify로도 실시간 갱신해준다 — 내가 직접
// 입/퇴장하지 않아도 같은 방에 있는 "다른" 누군가가 들고나면 자동으로
// 화면 숫자가 바뀐다.
//
// [설계 — 보낸/받은 메시지 구분] 서버(ChatMessageHandler.cpp)는 채팅
// 메시지를 발신자 포함, 지금 있는 방/로비 전체에게 브로드캐스트한다.
// "이게 내가 보낸 게 되돌아온 건지"는 프로토콜만으로 구분이 안 돼서,
// 메시지를 보낼 때 그 문자열을 _pendingSentEchoes 큐에 넣어두고, 도착한
// 메시지가 큐 맨 앞과 일치하면 그 서버 에코를 조용히 소비해 중복 표시를 막는다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace ChatApp
{
    public class ChatClientForm : Form
    {
        //***************************************************************************
        // @brief listBoxChat/listBoxMsg에 OwnerDraw로 색깔 있는 한 줄을 넣기 위한 항목.
        //***************************************************************************
        private class ColoredEntry
        {
            public string Text;
            public Color Color;
            public override string ToString() => Text; // 접근성 등 폴백 경로용
        }

        private TextBox _txtServerIp;
        private TextBox _txtServerPort;
        private TextBox _txtProfileName;
        private Button _btnConnect;
        private Button _btnDisconnect;
        private Label _lblStatus;

        private string _currentNickname; // 화면에 별도로 표시하지 않고, 닉네임 변경 다이얼로그에 넘겨줄 용도로만 보관
        private Button _btnOpenNicknameDialog;

        private ComboBox _cbChatRoomId;
        private Button _btnRoomEnter;
        private Button _btnRoomLeave;
        private TextBox _txtRoomUserCount;
        private Label _lblCurrentRoom;

        private TextBox _txtMessage;
        private Button _btnSend;
        private ListBox _listBoxChat;

        private ListBox _listBoxMsg;

        private ChatNetworkClient _client;
        private string _profileName;
        private int _currentRoomId = -1; // -1: 아직 로그인 전(로비 배정 전). 로그인하면 서버가 0(로비)으로 넣어준다.

        // 보낸/받은 메시지 구분용 — UI 스레드(Send)와 네트워크 수신 스레드
        // (OnChatMessageReceived, Invoke() 이전) 양쪽에서 건드리므로 락 필요.
        private readonly Queue<string> _pendingSentEchoes = new Queue<string>();
        private readonly object _pendingSentEchoesLock = new object();

        private static readonly Color ColorSent = Color.Blue;
        private static readonly Color ColorReceived = Color.Black;
        private static readonly Color ColorSystemOk = Color.Green;
        private static readonly Color ColorSystemError = Color.Firebrick;
        private static readonly Color ColorSystemInfo = Color.Gray;

        public ChatClientForm()
        {
            InitializeComponents();
        }

        private void InitializeComponents()
        {
            Text = "채팅 클라이언트 (WinForms)";
            ClientSize = new Size(577, 590);
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            DoubleBuffered = true;

            // ── 그룹박스 1: 서버 접속 ──────────────────────────────────────
            var groupBox1 = new GroupBox { Text = "서버 접속", Left = 10, Top = 9, Width = 557, Height = 85 };

            var lblIp = new Label { Text = "서버 IP", Left = 11, Top = 25, Width = 45 };
            _txtServerIp = new TextBox { Left = 60, Top = 21, Width = 120, Text = "127.0.0.1" };

            var lblPort = new Label { Text = "포트", Left = 190, Top = 25, Width = 35 };
            _txtServerPort = new TextBox { Left = 225, Top = 21, Width = 60, Text = "30201" };

            var lblProfile = new Label { Text = "프로필 이름", Left = 11, Top = 51, Width = 80 };
            _txtProfileName = new TextBox { Left = 90, Top = 47, Width = 150 };

            // [추가] 프로필 이름 옆의 닉네임 변경 버튼 — 서버에 로그인 성공한
            // 뒤에만 보인다(Visible, 단순 Enabled가 아님 — 로그인 전에는
            // 아예 존재를 드러내지 않는다는 요구사항). 누르면 별도 팝업
            // (NicknameChangeDialog)이 뜨고, 그 안에 자동 생성/변경 버튼이 있다.
            _btnOpenNicknameDialog = new Button { Text = "닉네임 변경", Left = 245, Top = 46, Width = 90, Height = 25, Visible = false };
            _btnOpenNicknameDialog.Click += BtnOpenNicknameDialog_Click;

            _btnConnect = new Button { Text = "접속", Left = 390, Top = 19, Width = 52, Height = 25 };
            _btnConnect.Click += BtnConnect_Click;

            _btnDisconnect = new Button { Text = "끊기", Left = 445, Top = 19, Width = 52, Height = 25, Enabled = false };
            _btnDisconnect.Click += BtnDisconnect_Click;

            _lblStatus = new Label { Text = "연결 안 됨", Left = 390, Top = 47, Width = 160, ForeColor = Color.Gray };

            groupBox1.Controls.AddRange(new Control[]
            {
                lblIp, _txtServerIp, lblPort, _txtServerPort, lblProfile, _txtProfileName, _btnOpenNicknameDialog,
                _btnConnect, _btnDisconnect, _lblStatus,
            });

            // ── 그룹박스 2: 채팅방(로비/룸) ──────────────────────────────
            var groupBox2 = new GroupBox { Text = "채팅방", Left = 10, Top = 100, Width = 557, Height = 340 };

            _cbChatRoomId = new ComboBox { Left = 11, Top = 23, Width = 71, DropDownStyle = ComboBoxStyle.DropDownList, Enabled = false };
            for (int roomId = 1; roomId <= ProtocolConstants.MaxRoomId; roomId++)
                _cbChatRoomId.Items.Add(roomId);
            if (_cbChatRoomId.Items.Count > 0)
                _cbChatRoomId.SelectedIndex = 0;

            _btnRoomEnter = new Button { Text = "방 입장", Left = 88, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomEnter.Click += BtnRoomEnter_Click;

            _btnRoomLeave = new Button { Text = "방 나가기", Left = 163, Top = 22, Width = 70, Height = 25, Enabled = false };
            _btnRoomLeave.Click += BtnRoomLeave_Click;

            var lblRoomUserCount = new Label { Text = "방 유저수 : ", Left = 242, Top = 27, Width = 70 };
            _txtRoomUserCount = new TextBox { Left = 312, Top = 23, Width = 50, ReadOnly = true };

            _lblCurrentRoom = new Label { Text = "위치: (로그인 전)", Left = 375, Top = 27, Width = 172, ForeColor = Color.Gray };

            _txtMessage = new TextBox { Left = 10, Top = 56, Width = 465, Height = 23, Enabled = false };
            _txtMessage.KeyDown += TxtMessage_KeyDown;

            _btnSend = new Button { Text = "채팅", Left = 480, Top = 55, Width = 67, Height = 26, Enabled = false };
            _btnSend.Click += BtnSend_Click;

            _listBoxChat = new ListBox
            {
                Left = 10,
                Top = 90,
                Width = 537,
                Height = 233,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 16,
                ScrollAlwaysVisible = true,
            };
            _listBoxChat.DrawItem += ColoredListBox_DrawItem;

            groupBox2.Controls.AddRange(new Control[]
            {
                _cbChatRoomId, _btnRoomEnter, _btnRoomLeave, lblRoomUserCount, _txtRoomUserCount, _lblCurrentRoom,
                _txtMessage, _btnSend, _listBoxChat,
            });

            // ── 하단: 시스템 로그(로그인/닉네임 변경/방 입퇴장 결과/연결 끊김/오류 등) ──
            _listBoxMsg = new ListBox
            {
                Left = 10,
                Top = 446,
                Width = 557,
                Height = 139,
                DrawMode = DrawMode.OwnerDrawFixed,
                ItemHeight = 16,
                ScrollAlwaysVisible = true,
            };
            _listBoxMsg.DrawItem += ColoredListBox_DrawItem;

            Controls.AddRange(new Control[] { groupBox1, groupBox2, _listBoxMsg });

            FormClosing += (s, e) => _client?.Close();
        }

        //***************************************************************************
        // @brief listBoxChat/listBoxMsg 공용 OwnerDraw 핸들러. Items에는
        //        ColoredEntry만 들어간다는 전제.
        //***************************************************************************
        private void ColoredListBox_DrawItem(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0)
                return;

            var listBox = (ListBox)sender;
            var entry = (ColoredEntry)listBox.Items[e.Index];

            e.DrawBackground();
            using (var brush = new SolidBrush(entry.Color))
            {
                e.Graphics.DrawString(entry.Text, listBox.Font, brush, e.Bounds);
            }
            e.DrawFocusRectangle();
        }

        private void AppendChat(string text, Color color)
        {
            _listBoxChat.Items.Add(new ColoredEntry { Text = text, Color = color });
            _listBoxChat.TopIndex = _listBoxChat.Items.Count - 1;
        }

        private void AppendSystemLog(string text, Color color)
        {
            _listBoxMsg.Items.Add(new ColoredEntry { Text = text, Color = color });
            _listBoxMsg.TopIndex = _listBoxMsg.Items.Count - 1;
        }

        //***************************************************************************
        // @brief 현재 위치(로비/방 번호) 표시와 방 입장/나가기 버튼의
        //        활성화 상태를 한 번에 갱신한다.
        //***************************************************************************
        private void UpdateRoomStatusUI()
        {
            if (_currentRoomId < 0)
            {
                _lblCurrentRoom.Text = "위치: (로그인 전)";
                _btnRoomEnter.Enabled = false;
                _btnRoomLeave.Enabled = false;
                return;
            }

            bool inLobby = (_currentRoomId == ProtocolConstants.LobbyRoomId);
            _lblCurrentRoom.Text = inLobby ? "위치: 로비" : $"위치: {_currentRoomId}번 방";
            _btnRoomEnter.Enabled = true;
            _btnRoomLeave.Enabled = !inLobby;
        }

        //***************************************************************************
        // @brief "접속" 버튼 — 로컬에 저장된 계정이 있으면 재접속, 없으면
        //        입력한 프로필 이름을 원하는 닉네임으로 신규 가입을 시도합니다.
        //***************************************************************************
        private void BtnConnect_Click(object sender, EventArgs e)
        {
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
            _currentRoomId = -1;
            UpdateRoomStatusUI();

            bool hasToken = AccountStorage.TryLoad(profileName, out byte[] publicId, out byte[] token);

            //AppendSystemLog("[디버그] 계정 파일 경로: " + AccountStorage.GetAccountFilePath(profileName)
            //    + " (존재함: " + hasToken + ")", ColorSystemInfo);

            _client = new ChatNetworkClient();
            _client.LoginResultReceived += OnLoginResultReceived;
            _client.ChatMessageReceived += OnChatMessageReceived;
            _client.RoomEnterResultReceived += OnRoomEnterResultReceived;
            _client.RoomLeaveResultReceived += OnRoomLeaveResultReceived;
            _client.RoomUserCountChanged += OnRoomUserCountChanged;
            _client.Disconnected += OnDisconnected;
            _client.ErrorOccurred += OnErrorOccurred;

            _client.Connect(_txtServerIp.Text.Trim(), port, hasToken, profileName, publicId, token);

            SetStatus(hasToken ? "재접속 중..." : "가입 중...", Color.Orange);
            AppendSystemLog(hasToken ? "[시스템] 저장된 계정으로 재접속을 시도합니다." : "[시스템] 신규 가입을 시도합니다.", ColorSystemInfo);

            _btnConnect.Enabled = false;
            _btnDisconnect.Enabled = true;

            // 접속 시도 중/접속된 동안엔 이 값들을 바꿀 수 없게 잠근다 —
            // 이미 시작된(또는 진행 중인) 연결의 대상을 몰래 바꾸는 걸 막기 위함.
            // Disconnected 콜백에서 다시 풀어준다.
            _txtServerIp.Enabled = false;
            _txtServerPort.Enabled = false;
            _txtProfileName.Enabled = false;
        }

        private void BtnDisconnect_Click(object sender, EventArgs e)
        {
            _client?.Close();
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
        //        미리 "나: ..."로 채팅 로그에 찍고 _pendingSentEchoes에 등록해둔다
        //        — 나중에 그 에코가 도착하면 중복 표시하지 않기 위함이다.
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

            AppendChat("나: " + msg, ColorSent);

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

        private void BtnRoomEnter_Click(object sender, EventArgs e)
        {
            if (_cbChatRoomId.SelectedItem == null)
                return;

            int roomId = (int)_cbChatRoomId.SelectedItem;
            _client?.RequestRoomEnter(roomId);
        }

        private void BtnRoomLeave_Click(object sender, EventArgs e)
        {
            _client?.RequestRoomLeave();
        }

        // ── 네트워크 콜백들 — 전부 백그라운드 수신 스레드에서 호출된다.
        // UI 컨트롤은 반드시 Invoke()로 UI 스레드에 넘긴 뒤에만 건드린다. ──

        private void OnLoginResultReceived(LoginResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    AccountStorage.Save(_profileName, res.PublicId, res.Token);

                    // [수정] "로그인 성공" 텍스트를 따로 띄우지 않는다 — 채팅/방
                    // 관련 컨트롤이 활성화되는 것 자체가 성공의 신호다.
                    SetStatus(string.Empty, Color.Gray);

                    _currentNickname = res.Nickname;
                    AppendSystemLog("[시스템] 로그인 성공 - 닉네임: " + res.Nickname, ColorSystemOk);

                    // 서버가 로그인 직후 자동으로 로비에 배정한다 — 클라이언트도
                    // 그 전제로 현재 위치를 로비로 잡아둔다(서버의 RoomUserCountNotify가
                    // 곧이어 도착해 실제 인원수도 채워줄 것).
                    _currentRoomId = ProtocolConstants.LobbyRoomId;
                    UpdateRoomStatusUI();

                    _btnSend.Enabled = true;
                    _cbChatRoomId.Enabled = true;
                    _txtMessage.Enabled = true;
                    _btnOpenNicknameDialog.Visible = true;
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
                return;

            Invoke((MethodInvoker)delegate { AppendChat(data.SenderNickname + ": " + data.Message, ColorReceived); });
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
                    string reasonText = (res.Reason == RoomResult.InvalidRoomId)
                        ? $"유효하지 않은 방 번호입니다(1~{ProtocolConstants.MaxRoomId})."
                        : "알 수 없는 오류";
                    AppendSystemLog("[시스템] 방 입장 실패 - " + reasonText, ColorSystemError);
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
                    _txtRoomUserCount.Clear(); // 로비 자체 인원수는 이 텍스트박스가 아니라 RoomUserCountNotify로 별도 관리하지 않음(단순화)
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

        private void OnDisconnected()
        {
            Invoke((MethodInvoker)delegate
            {
                SetStatus("연결 끊김", Color.Gray);
                AppendSystemLog("[시스템] 서버와 연결이 끊어졌습니다.", ColorSystemError);
                _btnConnect.Enabled = true;
                _btnDisconnect.Enabled = false;
                _btnSend.Enabled = false;
                _cbChatRoomId.Enabled = false;
                _txtMessage.Enabled = false;
                _btnOpenNicknameDialog.Visible = false;

                // [추가] 접속 시도 때 잠갔던 필드를 다시 풀어준다.
                _txtServerIp.Enabled = true;
                _txtServerPort.Enabled = true;
                _txtProfileName.Enabled = true;

                _currentRoomId = -1;
                _txtRoomUserCount.Clear();
                UpdateRoomStatusUI();

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Clear();
                }
            });
        }

        private void OnErrorOccurred(Exception ex)
        {
            Invoke((MethodInvoker)delegate { AppendSystemLog("[오류] " + ex.Message, ColorSystemError); });
        }

        private void SetStatus(string text, Color color)
        {
            _lblStatus.Text = text;
            _lblStatus.ForeColor = color;
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
    }
}