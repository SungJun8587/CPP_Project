
//***************************************************************************
// ChatClientForm.cs : 메인 창 — 접속/채팅/닉네임 변경 UI.
//
// [설계] Designer.cs 없이 코드로 컨트롤을 직접 배치했다(단일 파일 전달을
// 위한 단순화 — 실제 프로젝트라면 Visual Studio 디자이너로 분리하는 걸
// 권장). ChatNetworkClient의 이벤트는 백그라운드 스레드에서 오므로, 전부
// Invoke()로 UI 스레드에 넘긴 뒤에야 컨트롤을 건드린다.
//
// [설계 — 보낸/받은 메시지 구분] 서버(ChatMessageHandler.cpp)는 채팅
// 메시지를 "발신자 포함 전체"에게 브로드캐스트한다(발신자 제외는 서버
// 쪽에서 명시적으로 범위 밖으로 남겨둔 부분). 게다가 ChatPacket 자체에
// 발신자 식별자가 없어서, 클라이언트가 받은 메시지만 보고는 "이게 내가
// 보낸 게 되돌아온 건지"를 프로토콜만으로 구분할 수 없다.
//
// 그래서 클라이언트가 직접 추적한다: 메시지를 보낼 때 그 문자열을
// _pendingSentEchoes 큐에 넣어두고, 즉시 화면엔 "나: ..."로 파란색 표시한다.
// 이후 네트워크로 도착한 메시지가 이 큐의 맨 앞 문자열과 정확히 같으면
// "그건 방금 내가 보낸 것의 서버 에코"로 판단해 큐에서 빼고 화면에 또
// 찍지 않는다(이미 보낼 때 표시했으므로 중복 방지). 일치하지 않으면
// 진짜 다른 사람이 보낸 메시지이므로 "상대: ..."로 표시한다.
//
// [알려진 한계] 이 방식은 "정확히 같은 문자열"로 판단하므로, 다른 유저가
// 나와 똑같은 텍스트를 거의 동시에 보내는 극단적인 경우 오판할 수
// 있다(내 에코 대신 그 사람 메시지를 내 에코로 착각) — 데모 범위에서는
// 감수할 만한 수준의 엣지 케이스로 판단해 남겨뒀다. 완전히 고치려면
// 서버가 발신자를 제외하고 브로드캐스트하도록 바꾸거나, 프로토콜에
// 발신자 식별자를 추가해야 한다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace ChatApp
{
    public class ChatClientForm : Form
    {
        private TextBox _txtServerIp;
        private TextBox _txtServerPort;
        private TextBox _txtProfileName;
        private Button _btnConnect;
        private Button _btnDisconnect;
        private Label _lblStatus;

        private RichTextBox _txtChatLog;
        private TextBox _txtMessage;
        private Button _btnSend;

        private TextBox _txtNewNickname;
        private Button _btnChangeNickname;
        private Button _btnGenerateNickname;

        private ChatNetworkClient _client;
        private string _profileName;

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
            ClientSize = new Size(640, 480);
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            FormBorderStyle = FormBorderStyle.FixedSingle;

            var lblIp = new Label { Text = "서버 IP", Left = 10, Top = 12, Width = 60 };
            _txtServerIp = new TextBox { Left = 75, Top = 9, Width = 120, Text = "127.0.0.1" };

            var lblPort = new Label { Text = "포트", Left = 205, Top = 12, Width = 35 };
            _txtServerPort = new TextBox { Left = 245, Top = 9, Width = 60, Text = "30201" };

            var lblProfile = new Label { Text = "프로필 이름", Left = 10, Top = 42, Width = 80 };
            _txtProfileName = new TextBox { Left = 90, Top = 39, Width = 150 };

            _btnConnect = new Button { Text = "접속", Left = 315, Top = 8, Width = 70 };
            _btnConnect.Click += BtnConnect_Click;

            _btnDisconnect = new Button { Text = "연결 끊기", Left = 315, Top = 38, Width = 70, Enabled = false };
            _btnDisconnect.Click += BtnDisconnect_Click;

            _lblStatus = new Label { Text = "연결 안 됨", Left = 395, Top = 14, Width = 230, ForeColor = Color.Gray };

            _txtChatLog = new RichTextBox
            {
                Left = 10,
                Top = 75,
                Width = 605,
                Height = 290,
                ReadOnly = true,
                ScrollBars = RichTextBoxScrollBars.Vertical,
                Font = new Font("Consolas", 9.5f),
                BackColor = Color.White
            };

            _txtMessage = new TextBox { Left = 10, Top = 375, Width = 505 };
            _txtMessage.KeyDown += TxtMessage_KeyDown;

            _btnSend = new Button { Text = "보내기", Left = 525, Top = 373, Width = 90, Enabled = false };
            _btnSend.Click += BtnSend_Click;

            var lblNewNick = new Label { Text = "새 닉네임", Left = 10, Top = 412, Width = 62 };
            _txtNewNickname = new TextBox { Left = 75, Top = 409, Width = 150 };

            _btnChangeNickname = new Button { Text = "닉네임 변경", Left = 235, Top = 407, Width = 90, Enabled = false };
            _btnChangeNickname.Click += BtnChangeNickname_Click;

            _btnGenerateNickname = new Button { Text = "자동 생성 요청", Left = 335, Top = 407, Width = 110, Enabled = false };
            _btnGenerateNickname.Click += BtnGenerateNickname_Click;

            Controls.AddRange(new Control[]
            {
                lblIp, _txtServerIp, lblPort, _txtServerPort, lblProfile, _txtProfileName,
                _btnConnect, _btnDisconnect, _lblStatus,
                _txtChatLog, _txtMessage, _btnSend,
                lblNewNick, _txtNewNickname, _btnChangeNickname, _btnGenerateNickname,
            });

            FormClosing += (s, e) => _client?.Close();
        }

        //***************************************************************************
        // @brief "접속" 버튼 — 로컬에 저장된 계정이 있으면 재접속, 없으면
        //        입력한 프로필 이름을 원하는 닉네임으로 신규 가입을 시도합니다.
        //***************************************************************************
        private void BtnConnect_Click(object sender, EventArgs e)
        {
            string profileName = _txtProfileName.Text.Trim();
            if (string.IsNullOrEmpty(profileName))
            {
                MessageBox.Show(this, "프로필 이름을 입력하세요.", "알림");
                return;
            }

            if (!int.TryParse(_txtServerPort.Text.Trim(), out int port))
            {
                MessageBox.Show(this, "포트 번호가 올바르지 않습니다.", "알림");
                return;
            }

            _profileName = profileName;

            bool hasToken = AccountStorage.TryLoad(profileName, out byte[] publicId, out byte[] token);

            // 참고용 — "지금 이 계정 파일을 찾고 있다"를 눈으로 바로 확인할 수 있게.
            AppendColoredLog("[디버그] 계정 파일 경로: " + AccountStorage.GetAccountFilePath(profileName)
                + " (존재함: " + hasToken + ")", ColorSystemInfo);

            _client = new ChatNetworkClient();
            _client.LoginResultReceived += OnLoginResultReceived;
            _client.ChatMessageReceived += OnChatMessageReceived;
            _client.NicknameGenerated += OnNicknameGenerated;
            _client.NicknameChangeResultReceived += OnNicknameChangeResultReceived;
            _client.Disconnected += OnDisconnected;
            _client.ErrorOccurred += OnErrorOccurred;

            _client.Connect(_txtServerIp.Text.Trim(), port, hasToken, profileName, publicId, token);

            SetStatus(hasToken ? "재접속 중..." : "가입 중...", Color.Orange);
            AppendColoredLog(hasToken ? "[시스템] 저장된 계정으로 재접속을 시도합니다." : "[시스템] 신규 가입을 시도합니다.", ColorSystemInfo);

            _btnConnect.Enabled = false;
            _btnDisconnect.Enabled = true;
        }

        private void BtnDisconnect_Click(object sender, EventArgs e)
        {
            _client?.Close();
        }

        private void TxtMessage_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true; // 입력창에서 개행 문자가 같이 들어가는 것 방지
                BtnSend_Click(sender, e);
            }
        }

        //***************************************************************************
        // @brief 메시지 전송. 서버가 발신자에게도 그대로 에코해주므로, 여기서
        //        미리 "나: ..."로 화면에 찍고 _pendingSentEchoes에 등록해둔다
        //        (클래스 상단 설명 참고) — 나중에 그 에코가 도착하면 중복
        //        표시하지 않기 위함이다.
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

            AppendColoredLog("나: " + msg, ColorSent);

            _client?.SendChat(msg);
            _txtMessage.Clear();
        }

        private void BtnChangeNickname_Click(object sender, EventArgs e)
        {
            string newNick = _txtNewNickname.Text.Trim();
            if (string.IsNullOrEmpty(newNick))
                return;

            _client?.RequestChangeNickname(newNick);
        }

        private void BtnGenerateNickname_Click(object sender, EventArgs e)
        {
            _client?.RequestNicknameGeneration();
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

                    SetStatus("로그인 성공", Color.Green);
                    AppendColoredLog("[시스템] 로그인 성공 - 닉네임: " + res.Nickname, ColorSystemOk);
                    _btnSend.Enabled = true;
                    _btnChangeNickname.Enabled = true;
                    _btnGenerateNickname.Enabled = true;
                }
                else
                {
                    // 서버가 채워 보낸 닉네임이 있으면(신규 가입 시도 닉네임, 또는
                    // 재접속 대상 계정의 현재 닉네임) 같이 보여준다 — AccountNotFound처럼
                    // 애초에 계정을 못 찾은 경우엔 빈 문자열이라 자연스럽게 생략된다.
                    string nicknameSuffix = string.IsNullOrEmpty(res.Nickname) ? "" : $" (닉네임: {res.Nickname})";

                    SetStatus("로그인 실패", Color.Red);
                    AppendColoredLog("[시스템] 로그인 실패 - " + DescribeLoginResult(res.Reason) + nicknameSuffix, ColorSystemError);
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

            Invoke((MethodInvoker)delegate { AppendColoredLog(data.SenderNickname + ": " + data.Message, ColorReceived); });
        }

        private void OnNicknameGenerated(string nickname)
        {
            Invoke((MethodInvoker)delegate
            {
                AppendColoredLog("[시스템] 추천 닉네임: " + nickname, ColorSystemInfo);
                _txtNewNickname.Text = nickname;
            });
        }

        private void OnNicknameChangeResultReceived(ChangeNicknameResPacketData res)
        {
            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                    AppendColoredLog("[시스템] 닉네임 변경 성공", ColorSystemOk);
                else
                    AppendColoredLog("[시스템] 닉네임 변경 실패 - " + DescribeLoginResult(res.Reason), ColorSystemError);
            });
        }

        private void OnDisconnected()
        {
            Invoke((MethodInvoker)delegate
            {
                SetStatus("연결 끊김", Color.Gray);
                AppendColoredLog("[시스템] 서버와 연결이 끊어졌습니다.", ColorSystemError);
                _btnConnect.Enabled = true;
                _btnDisconnect.Enabled = false;
                _btnSend.Enabled = false;
                _btnChangeNickname.Enabled = false;
                _btnGenerateNickname.Enabled = false;

                lock (_pendingSentEchoesLock)
                {
                    _pendingSentEchoes.Clear();
                }
            });
        }

        private void OnErrorOccurred(Exception ex)
        {
            Invoke((MethodInvoker)delegate { AppendColoredLog("[오류] " + ex.Message, ColorSystemError); });
        }

        //***************************************************************************
        // @brief RichTextBox 맨 끝에 색깔 있는 한 줄을 추가한다.
        // @details 반드시 UI 스레드에서만 호출할 것(호출부가 이미 Invoke()로
        //          넘긴 뒤이거나, BtnSend_Click처럼 애초에 UI 스레드인 경우).
        //***************************************************************************
        private void AppendColoredLog(string line, Color color)
        {
            _txtChatLog.SelectionStart = _txtChatLog.TextLength;
            _txtChatLog.SelectionLength = 0;
            _txtChatLog.SelectionColor = color;
            _txtChatLog.AppendText(line + Environment.NewLine);
            _txtChatLog.SelectionColor = _txtChatLog.ForeColor; // 다음 AppendText가 기본색부터 시작하도록 원복
            _txtChatLog.ScrollToCaret();
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