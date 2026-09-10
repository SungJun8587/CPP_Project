
//***************************************************************************
// NicknameChangeDialog.cs : 닉네임 변경 전용 팝업 창 — 자동 생성 + 변경.
//
// [설계] ChatClientForm이 갖고 있는 ChatNetworkClient를 그대로 공유받아
// 쓴다(별도 연결을 만들지 않음). 이 창이 열려 있는 동안만 서버의
// NicknameGenerated/NicknameChangeResultReceived 이벤트를 구독하고,
// 닫히면 즉시 구독 해지한다 — 구독 해지를 안 하면, 창을 닫은 뒤에도
// 네트워크 스레드가 죽은(Dispose된) 창의 핸들로 계속 Invoke()를 시도하다
// ObjectDisposedException이 날 수 있다.
//***************************************************************************

using System;
using System.Drawing;
using System.Windows.Forms;

namespace ChatApp
{
    public class NicknameChangeDialog : Form
    {
        private readonly ChatNetworkClient _client;

        //***************************************************************************
        // @brief 이 창에서 실제로 성공적으로 적용된 닉네임. 실패했거나 아직
        //        변경을 안 했으면 null. 호출부(ChatClientForm)가 ShowDialog()
        //        반환 후 이 값을 읽어 메인 화면 상태를 갱신한다 — 메인 폼이
        //        굳이 NicknameChangeResultReceived를 별도로 구독하지 않아도
        //        되게 하기 위함(이 다이얼로그가 그 이벤트의 유일한 구독자가
        //        되어 책임을 한 곳에 모은다).
        //***************************************************************************
        public string AppliedNickname { get; private set; }

        private TextBox _txtCurrentNickname;
        private TextBox _txtNewNickname;
        private Button _btnGenerate;
        private Button _btnChange;
        private Label _lblStatus;

        //***************************************************************************
        // @param client 메인 폼이 이미 연결해둔 네트워크 클라이언트(공유)
        // @param currentNickname 지금 로그인돼 있는 닉네임 — 창 위쪽에 참고용으로 표시
        //***************************************************************************
        public NicknameChangeDialog(ChatNetworkClient client, string currentNickname)
        {
            _client = client;
            InitializeComponents(currentNickname);
        }

        private void InitializeComponents(string currentNickname)
        {
            Text = "닉네임 변경";
            ClientSize = new Size(300, 175);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;
            ShowInTaskbar = false;

            var lblCurrent = new Label { Text = "현재 닉네임", Left = 10, Top = 16, Width = 70 };
            _txtCurrentNickname = new TextBox { Left = 85, Top = 13, Width = 195, ReadOnly = true, Text = currentNickname };

            var lblNew = new Label { Text = "새 닉네임", Left = 10, Top = 51, Width = 70 };
            _txtNewNickname = new TextBox { Left = 85, Top = 48, Width = 195 };

            _btnGenerate = new Button { Text = "자동 생성", Left = 10, Top = 83, Width = 130, Height = 30 };
            _btnGenerate.Click += BtnGenerate_Click;

            _btnChange = new Button { Text = "변경", Left = 150, Top = 83, Width = 130, Height = 30 };
            _btnChange.Click += BtnChange_Click;

            _lblStatus = new Label { Left = 10, Top = 123, Width = 270, Height = 40, ForeColor = Color.Gray };

            Controls.AddRange(new Control[]
            {
                lblCurrent, _txtCurrentNickname, lblNew, _txtNewNickname, _btnGenerate, _btnChange, _lblStatus,
            });

            // 이 창이 열려 있는 동안만 구독 — 클래스 상단 설명 참고.
            Load += (s, e) =>
            {
                _client.NicknameGenerated += OnNicknameGenerated;
                _client.NicknameChangeResultReceived += OnNicknameChangeResultReceived;
            };
            FormClosed += (s, e) =>
            {
                _client.NicknameGenerated -= OnNicknameGenerated;
                _client.NicknameChangeResultReceived -= OnNicknameChangeResultReceived;
            };
        }

        private void BtnGenerate_Click(object sender, EventArgs e)
        {
            _lblStatus.ForeColor = Color.Gray;
            _lblStatus.Text = "추천 닉네임 요청 중...";
            _client.RequestNicknameGeneration();
        }

        private void BtnChange_Click(object sender, EventArgs e)
        {
            string newNick = _txtNewNickname.Text.Trim();
            if (string.IsNullOrEmpty(newNick))
                return;

            _lblStatus.ForeColor = Color.Gray;
            _lblStatus.Text = "변경 요청 중...";
            _client.RequestChangeNickname(newNick);
        }

        // ── 네트워크 콜백 — 백그라운드 수신 스레드에서 호출된다. ──

        private void OnNicknameGenerated(string nickname)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                _txtNewNickname.Text = nickname;
                _lblStatus.ForeColor = Color.Gray;
                _lblStatus.Text = "추천 닉네임을 받았습니다. 마음에 들면 [변경]을 누르세요.";
            });
        }

        private void OnNicknameChangeResultReceived(ChangeNicknameResPacketData res)
        {
            if (IsDisposed)
                return;

            Invoke((MethodInvoker)delegate
            {
                if (res.Success)
                {
                    AppliedNickname = _txtNewNickname.Text.Trim();
                    _txtCurrentNickname.Text = AppliedNickname; // 창을 안 닫고 연속으로 또 바꿀 수도 있으니 "현재 닉네임"도 갱신
                    _lblStatus.ForeColor = Color.Green;
                    _lblStatus.Text = "닉네임이 변경되었습니다.";
                }
                else
                {
                    _lblStatus.ForeColor = Color.Firebrick;
                    _lblStatus.Text = "변경 실패 - " + DescribeReason(res.Reason);
                }
            });
        }

        private static string DescribeReason(LoginResult reason)
        {
            switch (reason)
            {
                case LoginResult.NicknameTaken: return "이미 사용 중인 닉네임입니다.";
                case LoginResult.InvalidNickname: return "닉네임 형식이 올바르지 않습니다(영문/숫자/밑줄/한글, 1~16자).";
                case LoginResult.DbError: return "서버 오류 - 잠시 후 다시 시도해주세요.";
                default: return reason.ToString();
            }
        }
    }
}