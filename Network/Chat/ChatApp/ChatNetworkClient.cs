
//***************************************************************************
// ChatNetworkClient.cs : TCP 연결 + 수신 루프 + 패킷 디스패치.
//
// [중요 — 스레드] 이 클래스의 이벤트(LoginResultReceived 등)는 전부 백그라운드
// 수신 스레드에서 직접 호출된다. WinForms 컨트롤은 자신을 만든 스레드
// (UI 스레드)에서만 안전하게 건드릴 수 있으므로, 이 이벤트를 구독하는 쪽
// (ChatClientForm)이 반드시 Control.Invoke()로 UI 스레드에 넘겨야 한다 —
// C++ 콘솔 클라이언트에서 "IOCP 워커 스레드 콜백" 주석과 정확히 같은 이유다.
//***************************************************************************

using System;
using System.Net.Sockets;
using System.Threading;

namespace ChatApp
{
    public class ChatNetworkClient : IDisposable
    {
        private TcpClient _tcpClient;
        private NetworkStream _stream;
        private Thread _workerThread;
        private volatile bool _running;

        public event Action<LoginResPacketData> LoginResultReceived;
        public event Action<ChatPacketData> ChatMessageReceived;
        public event Action<string> NicknameGenerated;
        public event Action<ChangeNicknameResPacketData> NicknameChangeResultReceived;
        public event Action<RoomEnterResPacketData> RoomEnterResultReceived;
        public event Action<RoomLeaveResPacketData> RoomLeaveResultReceived;
        public event Action<RoomUserCountNotifyData> RoomUserCountChanged;
        public event Action Disconnected;
        public event Action<Exception> ErrorOccurred;

        public bool IsConnected => _tcpClient != null && _tcpClient.Connected;

        //***************************************************************************
        // @brief 접속을 게시합니다. 실제 TCP 연결/로그인 요청 전송까지 전부
        //        백그라운드 스레드에서 수행하므로 UI 스레드를 블로킹하지 않습니다.
        //        연결 실패는 예외를 던지는 대신 ErrorOccurred/Disconnected
        //        이벤트로 통지됩니다(Connect() 호출 자체는 항상 즉시 반환).
        //***************************************************************************
        public void Connect(string host, int port, bool hasToken, string desiredNickname, byte[] publicId, byte[] token)
        {
            _running = true;
            _workerThread = new Thread(() => ConnectAndRunRecvLoop(host, port, hasToken, desiredNickname, publicId, token))
            {
                IsBackground = true
            };
            _workerThread.Start();
        }

        private void ConnectAndRunRecvLoop(string host, int port, bool hasToken, string desiredNickname, byte[] publicId, byte[] token)
        {
            try
            {
                _tcpClient = new TcpClient();
                _tcpClient.Connect(host, port);
                _stream = _tcpClient.GetStream();

                byte[] loginReq = hasToken
                    ? PacketBuilder.BuildLoginReqReconnect(publicId, token)
                    : PacketBuilder.BuildLoginReqNewAccount(desiredNickname);

                _stream.Write(loginReq, 0, loginReq.Length);
            }
            catch (Exception ex)
            {
                _running = false;
                ErrorOccurred?.Invoke(ex);
                Disconnected?.Invoke();
                return;
            }

            RecvLoop();
        }

        public void SendChat(string message) => SendRaw(PacketBuilder.BuildChat(message));
        public void RequestNicknameGeneration() => SendRaw(PacketBuilder.BuildNicknameGenerateReq());
        public void RequestChangeNickname(string newNickname) => SendRaw(PacketBuilder.BuildChangeNicknameReq(newNickname));
        public void RequestRoomEnter(int roomId) => SendRaw(PacketBuilder.BuildRoomEnterReq(roomId));
        public void RequestRoomLeave() => SendRaw(PacketBuilder.BuildRoomLeaveReq());

        private void SendRaw(byte[] packet)
        {
            if (!IsConnected)
                return;

            try
            {
                _stream.Write(packet, 0, packet.Length);
            }
            catch (Exception ex)
            {
                ErrorOccurred?.Invoke(ex);
            }
        }

        //***************************************************************************
        // @brief 서버(CChatSession::OnRecv())와 동일한 프레이밍 규칙으로 읽는다:
        //        먼저 헤더 4바이트(size+type)를 읽고, size-4만큼 나머지를 읽는다.
        //***************************************************************************
        private void RecvLoop()
        {
            try
            {
                while (_running)
                {
                    byte[] header = ReadExact(ProtocolConstants.HeaderBytes);
                    if (header == null)
                        break;

                    ushort size = BitConverter.ToUInt16(header, 0);
                    ushort type = BitConverter.ToUInt16(header, 2);

                    if (size < ProtocolConstants.HeaderBytes)
                        break; // 프로토콜 위반

                    byte[] rest = size > ProtocolConstants.HeaderBytes
                        ? ReadExact(size - ProtocolConstants.HeaderBytes)
                        : new byte[0];
                    if (rest == null)
                        break;

                    byte[] full = new byte[size];
                    Array.Copy(header, 0, full, 0, ProtocolConstants.HeaderBytes);
                    Array.Copy(rest, 0, full, ProtocolConstants.HeaderBytes, rest.Length);

                    Dispatch((PacketType)type, full);
                }
            }
            catch (Exception ex)
            {
                if (_running)
                    ErrorOccurred?.Invoke(ex);
            }
            finally
            {
                _running = false;
                Disconnected?.Invoke();
            }
        }

        private byte[] ReadExact(int count)
        {
            byte[] buf = new byte[count];
            int offset = 0;
            while (offset < count)
            {
                int n = _stream.Read(buf, offset, count - offset);
                if (n <= 0)
                    return null; // 연결 종료

                offset += n;
            }
            return buf;
        }

        private void Dispatch(PacketType type, byte[] full)
        {
            switch (type)
            {
                case PacketType.LoginRes:
                    LoginResultReceived?.Invoke(PacketParser.ParseLoginRes(full));
                    break;

                case PacketType.Chat:
                    ChatMessageReceived?.Invoke(PacketParser.ParseChat(full));
                    break;

                case PacketType.NicknameGenerateRes:
                    NicknameGenerated?.Invoke(PacketParser.ParseNicknameGenerateRes(full));
                    break;

                case PacketType.ChangeNicknameRes:
                    NicknameChangeResultReceived?.Invoke(PacketParser.ParseChangeNicknameRes(full));
                    break;

                case PacketType.RoomEnterRes:
                    RoomEnterResultReceived?.Invoke(PacketParser.ParseRoomEnterRes(full));
                    break;

                case PacketType.RoomLeaveRes:
                    RoomLeaveResultReceived?.Invoke(PacketParser.ParseRoomLeaveRes(full));
                    break;

                case PacketType.RoomUserCountNotify:
                    RoomUserCountChanged?.Invoke(PacketParser.ParseRoomUserCountNotify(full));
                    break;

                default:
                    break; // 알 수 없는 타입 — 무시
            }
        }

        //***************************************************************************
        // @brief 연결을 끊습니다. 수신 스레드가 블로킹 Read 중이면 스트림을
        //        닫는 순간 예외가 나서 RecvLoop()가 자연스럽게 빠져나온다.
        //***************************************************************************
        public void Close()
        {
            _running = false;
            try { _stream?.Close(); } catch { /* 이미 끊겼을 수 있음 — 무시 */ }
            try { _tcpClient?.Close(); } catch { /* 위와 동일 */ }
        }

        public void Dispose() => Close();
    }
}