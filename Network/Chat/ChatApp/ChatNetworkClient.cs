
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
using System.Collections.Generic;
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
        public event Action<ServerUserCountResData> ServerUserCountReceived;
        public event Action<SetProfileImageUrlResPacketData> SetProfileImageUrlResultReceived;

        // [추가] 프로필 이미지 업로드/다운로드/갤러리 관련 이벤트.
        public event Action<RequestUploadTokenResData> UploadTokenReceived;
        public event Action<ProfileImageListItemData> ProfileImageListItemReceived;
        public event Action<ListProfileImagesEndResData> ProfileImageListEndReceived;
        public event Action<SelectProfileImageResData> SelectProfileImageResultReceived;
        public event Action<DeleteProfileImageResData> DeleteProfileImageResultReceived;
        // [추가] 채팅 파일 첨부 삭제 브로드캐스트 — 같은 방/로비의 누군가(나 포함)가
        // 파일을 지우면 도착한다.
        public event Action<DeleteChatMessageResData> DeleteChatMessageResultReceived;
        public event Action<DeleteChatMessageNotifyData> DeleteChatMessageNotified;

        // [추가] 방 생성/삭제/이름변경/목록조회 + 방장 자동 이양 알림.
        public event Action<CreateRoomResData> CreateRoomResultReceived;
        public event Action<DeleteRoomResData> DeleteRoomResultReceived;
        public event Action<DeleteRoomNotifyData> DeleteRoomNotified;
        public event Action<RenameRoomResData> RenameRoomResultReceived;
        public event Action<RenameRoomNotifyData> RenameRoomNotified;
        public event Action<RoomListItemData> RoomListItemReceived;
        public event Action<ListRoomsEndResData> RoomListEndReceived;
        public event Action<RoomMemberItemData> RoomMemberListItemReceived;
        public event Action<RoomMemberListEndResData> RoomMemberListEndReceived;
        public event Action<RoomOwnerChangedNotifyData> RoomOwnerChangedNotified;

        // [추가] 방 프로필 이미지 설정 결과 + 변경 알림.
        public event Action<SetRoomImageResData> SetRoomImageResultReceived;
        public event Action<RoomImageChangedNotifyData> RoomImageChangedNotified;

        // [추가] 방 입장 시 자동으로 오는 과거 대화 기록.
        public event Action<ChatHistoryItemData> ChatHistoryItemReceived;
        public event Action<ChatHistoryEndData> ChatHistoryEndReceived;

        // [추가] 친구 기능 — 요청/수락/삭제/목록/차단 응답 + 서버 알림.
        public event Action<FriendRequestResData> FriendRequestResultReceived;
        public event Action<FriendRespondResData> FriendRespondResultReceived;
        public event Action<FriendTargetResData> FriendRemoveResultReceived;
        public event Action<List<FriendItemData>> FriendListBatchReceived;   // 친구 목록 한 묶음(패킷 하나)
        public event Action<FriendListEndData> FriendListEndReceived;
        public event Action<FriendPendingItemData> FriendPendingItemReceived;
        public event Action<FriendPendingEndData> FriendPendingEndReceived;
        public event Action<FriendTargetResData> BlockResultReceived;
        public event Action<FriendTargetResData> UnblockResultReceived;
        public event Action<BlockItemData> BlockListItemReceived;
        public event Action<FriendListEndData> BlockListEndReceived;
        public event Action<FriendRequestNotifyData> FriendRequestNotified;
        public event Action<FriendAcceptedNotifyData> FriendAcceptedNotified;
        public event Action<FriendRemovedNotifyData> FriendRemovedNotified;
        public event Action<FriendProfileChangedNotifyData> FriendProfileChangedNotified;
        public event Action<FriendStatusNotifyData> FriendStatusNotified;

        // [추가] 대화방(1:1/그룹) — 열기/생성/초대/나가기/목록/메시지/기록/읽음/멤버.
        public event Action<ConvOpenResData> ConvOpenResultReceived;
        public event Action<ConvResultData> ConvInviteResultReceived;
        public event Action<ConvResultData> ConvLeaveResultReceived;
        public event Action<ConvSummaryData> ConvListItemReceived;
        public event Action<ConvListEndData> ConvListEndReceived;
        public event Action<ConvResultData> ConvSendResultReceived;
        public event Action<ConvMessageData> ConvMessageReceived;
        public event Action<ConvMessageData> ConvHistoryItemReceived;
        public event Action<ConvHistoryEndData> ConvHistoryEndReceived;
        public event Action<ConvReadNotifyData> ConvReadNotified;
        public event Action<ConvMemberData> ConvMemberItemReceived;
        public event Action<ConvMembersEndData> ConvMembersEndReceived;
        public event Action<ConvSummaryData> ConvUpdatedNotified;
        public event Action<ConvResultData> ConvDeleteResultReceived;
        public event Action<ConvDeletedData> ConvDeletedNotified;
        public event Action<ConvResultData> ConvRenameResultReceived;
        public event Action<ConvRenamedData> ConvRenamedNotified;

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
        public void RequestSetProfileImageUrl(string url) => SendRaw(PacketBuilder.BuildSetProfileImageUrlReq(url));
        public void RequestRoomEnter(int roomId) => SendRaw(PacketBuilder.BuildRoomEnterReq(roomId));

        // [추가] 방 생성/삭제/이름변경/목록조회 요청.
        public void RequestCreateRoom(string roomName) => SendRaw(PacketBuilder.BuildCreateRoomReq(roomName));
        public void RequestDeleteRoom(int roomId) => SendRaw(PacketBuilder.BuildDeleteRoomReq(roomId));
        public void RequestRenameRoom(int roomId, string newName) => SendRaw(PacketBuilder.BuildRenameRoomReq(roomId, newName));

        private int _listRoomsRequestSeq;

        //***************************************************************************
        // @brief 방 목록 한 페이지를 요청하고, 이 요청의 식별자를 돌려준다.
        // @details 응답 패킷(RoomListItemReceived/RoomListEndReceived)이 이 식별자를
        //          그대로 되돌려주므로, 호출부는 마지막으로 보낸 요청의 식별자와
        //          비교해 그보다 오래된 요청의 늦은 응답을 버릴 수 있다.
        //***************************************************************************
        public int RequestListRooms(RoomListScope scope, int page, int pageSize, string keyword)
        {
            int requestId = System.Threading.Interlocked.Increment(ref _listRoomsRequestSeq);
            SendRaw(PacketBuilder.BuildListRoomsReq(requestId, scope, page, pageSize, keyword));
            return requestId;
        }

        private int _roomMemberListRequestSeq;

        //***************************************************************************
        // @brief 지금 자신이 있는 방(로비 포함)에 입장한 적이 있는 유저 목록
        //        (전체 이력)의 한 페이지를 요청하고, 이 요청의 식별자를 돌려준다.
        //***************************************************************************
        public int RequestRoomMemberList(int roomId, RoomMemberListScope scope, int page, int pageSize)
        {
            int requestId = System.Threading.Interlocked.Increment(ref _roomMemberListRequestSeq);
            SendRaw(PacketBuilder.BuildRoomMemberListReq(requestId, roomId, scope, page, pageSize));
            return requestId;
        }

        // [추가] 방 프로필 이미지 설정/교체/해제 요청.
        public void RequestSetRoomImage(int roomId, string imageUrl) => SendRaw(PacketBuilder.BuildSetRoomImageReq(roomId, imageUrl));
        public void RequestRoomLeave() => SendRaw(PacketBuilder.BuildRoomLeaveReq());
        public void RequestServerUserCount() => SendRaw(PacketBuilder.BuildServerUserCountReq());

        // [추가] 프로필 이미지 업로드/다운로드/갤러리 관련 전송 메서드.
        public void RequestUploadToken() => SendRaw(PacketBuilder.BuildRequestUploadTokenReq());
        public void RequestListProfileImages() => SendRaw(PacketBuilder.BuildListProfileImagesReq());
        public void RequestSelectProfileImage(long imageId) => SendRaw(PacketBuilder.BuildSelectProfileImageReq(imageId));
        public void RequestDeleteProfileImage(long imageId) => SendRaw(PacketBuilder.BuildDeleteProfileImageReq(imageId));
        // [추가] 파일 서버에서 이미 삭제된 파일임을 채팅 서버에 알려서,
        // 같은 방/로비의 다른 사람들 화면에서도 지워지도록 브로드캐스트를 유도한다.
        public void RequestDeleteChatMessage(long messageId) => SendRaw(PacketBuilder.BuildDeleteChatMessageReq(messageId));

        // [추가] 친구 기능 요청.
        public void RequestFriend(string nickname) => SendRaw(PacketBuilder.BuildFriendRequestReq(nickname));
        public void RespondFriendRequest(byte[] publicId, bool accept) => SendRaw(PacketBuilder.BuildFriendRespondReq(publicId, accept));
        public void RequestFriendRemove(byte[] publicId) => SendRaw(PacketBuilder.BuildFriendRemoveReq(publicId));
        public void RequestFriendList() => SendRaw(PacketBuilder.BuildFriendListReq());
        public void RequestBlock(string nickname) => SendRaw(PacketBuilder.BuildBlockReq(nickname));
        public void RequestUnblock(byte[] publicId) => SendRaw(PacketBuilder.BuildUnblockReq(publicId));
        public void RequestBlockList() => SendRaw(PacketBuilder.BuildBlockListReq());

        // [추가] 대화방 요청.
        public void RequestConvOpenDirect(byte[] publicId) => SendRaw(PacketBuilder.BuildConvOpenDirectReq(publicId));
        public void RequestConvCreateGroup(string name, System.Collections.Generic.IList<byte[]> members) => SendRaw(PacketBuilder.BuildConvCreateGroupReq(name, members));
        public void RequestConvInvite(long convId, System.Collections.Generic.IList<byte[]> members) => SendRaw(PacketBuilder.BuildConvInviteReq(convId, members));
        public void RequestConvLeave(long convId) => SendRaw(PacketBuilder.BuildConvLeaveReq(convId));
        public void RequestConvList() => SendRaw(PacketBuilder.BuildConvListReq());
        public void RequestConvSend(long convId, string message, ConvMessageKind kind = ConvMessageKind.Text, long replyToMsgId = 0)
            => SendRaw(PacketBuilder.BuildConvSendReq(convId, message, kind, replyToMsgId));
        public void RequestConvRename(long convId, string name) => SendRaw(PacketBuilder.BuildConvRenameReq(convId, name));
        public void RequestConvDelete(long convId, long msgId) => SendRaw(PacketBuilder.BuildConvDeleteReq(convId, msgId));
        public void RequestConvRead(long convId, long upToMsgId) => SendRaw(PacketBuilder.BuildConvReadReq(convId, upToMsgId));
        public void RequestConvMembers(long convId) => SendRaw(PacketBuilder.BuildConvMembersReq(convId));

        private int _convHistoryRequestSeq;

        //***************************************************************************
        // @brief 대화방의 지난 메시지를 요청하고, 이 요청의 식별자를 돌려준다.
        //        응답(End)이 식별자를 그대로 되돌려주므로, 호출부가 오래된 요청의 늦은 응답을 버릴 수 있다.
        //***************************************************************************
        public int RequestConvHistory(long convId, long beforeMsgId, int count)
        {
            int requestId = System.Threading.Interlocked.Increment(ref _convHistoryRequestSeq);
            SendRaw(PacketBuilder.BuildConvHistoryReq(convId, beforeMsgId, count, requestId));
            return requestId;
        }

        private int _friendPendingRequestSeq;

        //***************************************************************************
        // @brief 받은 친구 요청 목록 한 페이지를 요청하고, 이 요청의 식별자를 돌려준다.
        //        응답(Item/End)이 이 식별자를 그대로 되돌려주므로, 호출부는 마지막으로
        //        보낸 요청의 식별자와 비교해 오래된 요청의 늦은 응답을 버릴 수 있다.
        //***************************************************************************
        public int RequestFriendPendingList(int page, int pageSize)
        {
            int requestId = System.Threading.Interlocked.Increment(ref _friendPendingRequestSeq);
            SendRaw(PacketBuilder.BuildFriendPendingListReq(requestId, page, pageSize));
            return requestId;
        }

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

                case PacketType.SetProfileImageUrlRes:
                    SetProfileImageUrlResultReceived?.Invoke(PacketParser.ParseSetProfileImageUrlRes(full));
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

                case PacketType.ServerUserCountRes:
                    ServerUserCountReceived?.Invoke(PacketParser.ParseServerUserCountRes(full));
                    break;

                case PacketType.RequestUploadTokenRes:
                    UploadTokenReceived?.Invoke(PacketParser.ParseRequestUploadTokenRes(full));
                    break;

                case PacketType.ListProfileImagesItemRes:
                    ProfileImageListItemReceived?.Invoke(PacketParser.ParseListProfileImagesItemRes(full));
                    break;

                case PacketType.ListProfileImagesEndRes:
                    ProfileImageListEndReceived?.Invoke(PacketParser.ParseListProfileImagesEndRes(full));
                    break;

                case PacketType.SelectProfileImageRes:
                    SelectProfileImageResultReceived?.Invoke(PacketParser.ParseSelectProfileImageRes(full));
                    break;

                case PacketType.DeleteProfileImageRes:
                    DeleteProfileImageResultReceived?.Invoke(PacketParser.ParseDeleteProfileImageRes(full));
                    break;

                case PacketType.DeleteChatMessageRes:
                    DeleteChatMessageResultReceived?.Invoke(PacketParser.ParseDeleteChatMessageRes(full));
                    break;

                case PacketType.DeleteChatMessageNotify:
                    DeleteChatMessageNotified?.Invoke(PacketParser.ParseDeleteChatMessageNotify(full));
                    break;

                case PacketType.CreateRoomRes:
                    CreateRoomResultReceived?.Invoke(PacketParser.ParseCreateRoomRes(full));
                    break;

                case PacketType.DeleteRoomRes:
                    DeleteRoomResultReceived?.Invoke(PacketParser.ParseDeleteRoomRes(full));
                    break;

                case PacketType.DeleteRoomNotify:
                    DeleteRoomNotified?.Invoke(PacketParser.ParseDeleteRoomNotify(full));
                    break;

                case PacketType.RenameRoomRes:
                    RenameRoomResultReceived?.Invoke(PacketParser.ParseRenameRoomRes(full));
                    break;

                case PacketType.RenameRoomNotify:
                    RenameRoomNotified?.Invoke(PacketParser.ParseRenameRoomNotify(full));
                    break;

                case PacketType.ListRoomsItemRes:
                    RoomListItemReceived?.Invoke(PacketParser.ParseListRoomsItemRes(full));
                    break;

                case PacketType.ListRoomsEndRes:
                    RoomListEndReceived?.Invoke(PacketParser.ParseListRoomsEndRes(full));
                    break;

                case PacketType.RoomOwnerChangedNotify:
                    RoomOwnerChangedNotified?.Invoke(PacketParser.ParseRoomOwnerChangedNotify(full));
                    break;

                case PacketType.SetRoomImageRes:
                    SetRoomImageResultReceived?.Invoke(PacketParser.ParseSetRoomImageRes(full));
                    break;

                case PacketType.RoomImageChangedNotify:
                    RoomImageChangedNotified?.Invoke(PacketParser.ParseRoomImageChangedNotify(full));
                    break;

                case PacketType.ChatHistoryItemRes:
                    ChatHistoryItemReceived?.Invoke(PacketParser.ParseChatHistoryItemRes(full));
                    break;

                case PacketType.ChatHistoryEndRes:
                    ChatHistoryEndReceived?.Invoke(PacketParser.ParseChatHistoryEndRes(full));
                    break;

                case PacketType.RoomMemberListItemRes:
                    RoomMemberListItemReceived?.Invoke(PacketParser.ParseRoomMemberListItemRes(full));
                    break;

                case PacketType.RoomMemberListEndRes:
                    RoomMemberListEndReceived?.Invoke(PacketParser.ParseRoomMemberListEndRes(full));
                    break;

                case PacketType.FriendRequestRes:
                    FriendRequestResultReceived?.Invoke(PacketParser.ParseFriendRequestRes(full));
                    break;

                case PacketType.FriendRespondRes:
                    FriendRespondResultReceived?.Invoke(PacketParser.ParseFriendRespondRes(full));
                    break;

                case PacketType.FriendRemoveRes:
                    FriendRemoveResultReceived?.Invoke(PacketParser.ParseFriendTargetRes(full));
                    break;

                case PacketType.FriendListBatchRes:
                    FriendListBatchReceived?.Invoke(PacketParser.ParseFriendListBatchRes(full));
                    break;

                case PacketType.FriendListEndRes:
                    FriendListEndReceived?.Invoke(PacketParser.ParseFriendListEndRes(full));
                    break;

                case PacketType.FriendPendingItemRes:
                    FriendPendingItemReceived?.Invoke(PacketParser.ParseFriendPendingItemRes(full));
                    break;

                case PacketType.FriendPendingEndRes:
                    FriendPendingEndReceived?.Invoke(PacketParser.ParseFriendPendingEndRes(full));
                    break;

                case PacketType.BlockRes:
                    BlockResultReceived?.Invoke(PacketParser.ParseFriendTargetRes(full));
                    break;

                case PacketType.UnblockRes:
                    UnblockResultReceived?.Invoke(PacketParser.ParseFriendTargetRes(full));
                    break;

                case PacketType.BlockListItemRes:
                    BlockListItemReceived?.Invoke(PacketParser.ParseBlockListItemRes(full));
                    break;

                case PacketType.BlockListEndRes:
                    BlockListEndReceived?.Invoke(PacketParser.ParseFriendListEndRes(full));
                    break;

                case PacketType.FriendRequestNotify:
                    FriendRequestNotified?.Invoke(PacketParser.ParseFriendRequestNotify(full));
                    break;

                case PacketType.FriendAcceptedNotify:
                    FriendAcceptedNotified?.Invoke(PacketParser.ParseFriendAcceptedNotify(full));
                    break;

                case PacketType.FriendRemovedNotify:
                    FriendRemovedNotified?.Invoke(PacketParser.ParseFriendRemovedNotify(full));
                    break;

                case PacketType.FriendProfileChangedNotify:
                    FriendProfileChangedNotified?.Invoke(PacketParser.ParseFriendProfileChangedNotify(full));
                    break;

                case PacketType.FriendStatusNotify:
                    FriendStatusNotified?.Invoke(PacketParser.ParseFriendStatusNotify(full));
                    break;

                case PacketType.ConvOpenRes:
                    ConvOpenResultReceived?.Invoke(PacketParser.ParseConvOpenRes(full));
                    break;

                case PacketType.ConvInviteRes:
                    ConvInviteResultReceived?.Invoke(PacketParser.ParseConvResult(full));
                    break;

                case PacketType.ConvLeaveRes:
                    ConvLeaveResultReceived?.Invoke(PacketParser.ParseConvResult(full));
                    break;

                case PacketType.ConvListItemRes:
                    ConvListItemReceived?.Invoke(PacketParser.ParseConvSummaryPacket(full));
                    break;

                case PacketType.ConvListEndRes:
                    ConvListEndReceived?.Invoke(PacketParser.ParseConvListEndRes(full));
                    break;

                case PacketType.ConvSendRes:
                    ConvSendResultReceived?.Invoke(PacketParser.ParseConvSendRes(full));
                    break;

                case PacketType.ConvDeleteRes:
                    ConvDeleteResultReceived?.Invoke(PacketParser.ParseConvDeleteRes(full));
                    break;

                case PacketType.ConvRenameRes:
                    ConvRenameResultReceived?.Invoke(PacketParser.ParseConvResult(full));
                    break;

                case PacketType.ConvRenamedNotify:
                    ConvRenamedNotified?.Invoke(PacketParser.ParseConvRenamedNotify(full));
                    break;

                case PacketType.ConvDeletedNotify:
                    ConvDeletedNotified?.Invoke(PacketParser.ParseConvDeletedNotify(full));
                    break;

                case PacketType.ConvMessageNotify:
                    ConvMessageReceived?.Invoke(PacketParser.ParseConvMessage(full));
                    break;

                case PacketType.ConvHistoryItemRes:
                    ConvHistoryItemReceived?.Invoke(PacketParser.ParseConvMessage(full));
                    break;

                case PacketType.ConvHistoryEndRes:
                    ConvHistoryEndReceived?.Invoke(PacketParser.ParseConvHistoryEndRes(full));
                    break;

                case PacketType.ConvReadNotify:
                    ConvReadNotified?.Invoke(PacketParser.ParseConvReadNotify(full));
                    break;

                case PacketType.ConvMemberItemRes:
                    ConvMemberItemReceived?.Invoke(PacketParser.ParseConvMemberItemRes(full));
                    break;

                case PacketType.ConvMembersEndRes:
                    ConvMembersEndReceived?.Invoke(PacketParser.ParseConvMembersEndRes(full));
                    break;

                case PacketType.ConvUpdatedNotify:
                    ConvUpdatedNotified?.Invoke(PacketParser.ParseConvSummaryPacket(full));
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