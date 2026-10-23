
//***************************************************************************
// ChatClientForm.Notify.cs : 메신저 새 메시지 데스크톱 알림 (partial).
//
// ConversationPanel이 "창이 비활성일 때 새 메시지가 왔다"고 알리면(MessageNotifyRequested)
// 작업 표시줄 깜박임 + 트레이 풍선 알림 + 알림음으로 알려 준다. 풍선을 누르면 창을 앞으로 가져와
// 메신저 탭에서 해당 대화방을 연다. 앱이 실행 중일 때만 동작한다(오프라인 푸시와는 별개).
//***************************************************************************

using System;
using System.Drawing;
using System.Media;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace ChatApp
{
    public partial class ChatClientForm
    {
        private NotifyIcon _messengerNotifyIcon;
        private long _notifyConvId;     // 마지막으로 띄운 풍선의 대화방(풍선 클릭 시 열 방)

        [StructLayout(LayoutKind.Sequential)]
        private struct FLASHWINFO
        {
            public uint cbSize;
            public IntPtr hwnd;
            public uint dwFlags;
            public uint uCount;
            public uint dwTimeout;
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlashWindowEx(ref FLASHWINFO pwfi);

        private const uint FLASHW_TRAY = 0x2;
        private const uint FLASHW_TIMERNOFG = 0xC;     // 창이 앞으로 올 때까지 계속 깜박임

        private void InitMessengerNotifications()
        {
            _messengerNotifyIcon = new NotifyIcon
            {
                Icon = Icon ?? SystemIcons.Application,
                Text = "채팅",
                Visible = true,
            };
            _messengerNotifyIcon.BalloonTipClicked += (s, e) => ActivateMessengerFromNotification();
            _messengerNotifyIcon.MouseClick += (s, e) =>
            {
                if (e.Button == MouseButtons.Left)
                    ActivateMessengerFromNotification();
            };

            _convPanel.MessageNotifyRequested += OnMessengerNotifyRequested;
        }

        private void DisposeMessengerNotifications()
        {
            if (_messengerNotifyIcon == null)
                return;
            _messengerNotifyIcon.Visible = false;   // 종료 후 트레이에 아이콘이 남지 않도록
            _messengerNotifyIcon.Dispose();
            _messengerNotifyIcon = null;
        }

        private void OnMessengerNotifyRequested(long convId, string title, string body, bool sound)
        {
            if (IsDisposed || !IsHandleCreated)
                return;

            _notifyConvId = convId;

            var info = new FLASHWINFO
            {
                cbSize = (uint)Marshal.SizeOf(typeof(FLASHWINFO)),
                hwnd = Handle,
                dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG,
                uCount = uint.MaxValue,
                dwTimeout = 0,
            };
            FlashWindowEx(ref info);

            if (_messengerNotifyIcon != null)
                _messengerNotifyIcon.ShowBalloonTip(4000, title, body, ToolTipIcon.None);

            if (sound)
            {
                try
                {
                    SystemSounds.Asterisk.Play();
                }
                catch (Exception)
                {
                    // 오디오 장치가 없는 환경 — 소리만 생략한다.
                }
            }
        }

        private void ActivateMessengerFromNotification()
        {
            if (IsDisposed)
                return;

            Show();
            if (WindowState == FormWindowState.Minimized)
                WindowState = FormWindowState.Normal;
            Activate();

            _tabControl.SelectedIndex = MessengerTabIndex;
            _convPanel.OpenConversationFromNotification(_notifyConvId);
        }
    }
}