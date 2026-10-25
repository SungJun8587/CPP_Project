
//***************************************************************************
// ChatProtocol.cs : 서버(ChatPacket.h)와 정확히 같은 바이트 레이아웃을
//                   재현하는 패킷 정의 + 직렬화/역직렬화.
//
// [중요] 아래 상수/필드 순서/크기는 C++ ChatPacket.h와 반드시 1:1로
// 일치해야 한다. 서버가 #pragma pack(push,1)로 패딩 없이 정의하므로,
// 여기서도 필드 순서 그대로, 패딩 없이 직렬화한다. BinaryWriter/
// BinaryReader는 기본적으로 리틀 엔디안으로 동작하는데, x86/x64는 전부
// 리틀 엔디안이라 서버(Windows/MSVC, x86/x64)와 그대로 호환된다.
//***************************************************************************

using System;
using System.IO;
using System.Text;

namespace ChatApp
{
    public enum PacketType : ushort
    {
        LoginReq = 1,
        LoginRes = 2,
        Chat = 3,
        NicknameGenerateReq = 4,
        NicknameGenerateRes = 5,
        ChangeNicknameReq = 6,
        ChangeNicknameRes = 7,
        RoomEnterReq = 8,
        RoomEnterRes = 9,
        RoomLeaveReq = 10,
        RoomLeaveRes = 11,
        RoomUserCountNotify = 12,
        ServerUserCountReq = 13,
        ServerUserCountRes = 14,
        SetProfileImageUrlReq = 15,
        SetProfileImageUrlRes = 16,

        // [설계 변경] 이미지 저장/서빙을 별도 파일 서버로 분리하면서, 채팅
        // 서버는 더 이상 이미지 바이트를 청크로 주고받지 않는다 — 대신
        // "업로드 허가 토큰"만 발급해주고, 실제 업로드는 클라이언트가
        // 파일 서버와 HTTP로 직접 한다(그 결과 URL만 SetProfileImageUrlReq로
        // 다시 채팅 서버에 등록). 서버 쪽 ChatPacketTypes.h와 번호를
        // 정확히 맞춰야 한다 — 하나라도 어긋나면 프로토콜 자체가 깨진다.
        RequestUploadTokenReq = 17,
        RequestUploadTokenRes = 18,

        ListProfileImagesReq = 19,
        ListProfileImagesItemRes = 20,
        ListProfileImagesEndRes = 21,
        SelectProfileImageReq = 22,
        SelectProfileImageRes = 23,
        DeleteProfileImageReq = 24,
        DeleteProfileImageRes = 25,

        // [추가] 채팅 메시지 삭제 — 서버 쪽 ChatPacketTypes.h와 번호를
        // 정확히 맞춰야 한다(ChatMessageHandler.cpp 참고).
        DeleteChatMessageReq = 26,      // Client -> Server
        DeleteChatMessageRes = 27,      // Server -> Client, 요청자에게만
        DeleteChatMessageNotify = 28,   // Server -> Client, 그 메시지가 원래 브로드캐스트된 방 전체에(요청자 포함)

        // [추가] 방 생성/삭제/이름변경/목록조회 + 방장 자동 이양 알림 —
        // 서버 쪽 ChatPacketTypes.h와 번호를 정확히 맞춰야 한다.
        CreateRoomReq = 29,             // Client -> Server, 방 생성 요청(생성자가 방장이 됨)
        CreateRoomRes = 30,             // Server -> Client, 방 생성 결과 응답
        DeleteRoomReq = 31,             // Client -> Server, 방 삭제 요청(방장만 가능)
        DeleteRoomRes = 32,             // Server -> Client, 방 삭제 결과 응답(요청자에게만)
        DeleteRoomNotify = 33,          // Server -> Client, 방 삭제 시 그 방 멤버 전원에게
        RenameRoomReq = 34,             // Client -> Server, 방 이름 변경 요청(방장만 가능)
        RenameRoomRes = 35,             // Server -> Client, 방 이름 변경 결과 응답(요청자에게만)
        RenameRoomNotify = 36,          // Server -> Client, 방 이름 변경 시 그 방 멤버 전원에게
        ListRoomsReq = 37,              // Client -> Server, 방 목록 한 페이지 조회 요청(범위/페이지/검색어)
        ListRoomsItemRes = 38,          // Server -> Client, 방 목록 항목 단건 응답(가변 개수 스트리밍)
        ListRoomsEndRes = 39,           // Server -> Client, 방 목록 한 페이지 전송 완료(조건에 맞는 전체 개수/페이지 번호)
        RoomOwnerChangedNotify = 40,    // Server -> Client, 방장이 나가서 다른 멤버에게 자동 이양됐을 때 그 방 멤버 전원에게

        // [추가] 방 프로필 이미지 설정/교체/해제(방장 전용) + 변경 알림.
        SetRoomImageReq = 41,           // Client -> Server, 방 프로필 이미지 설정/교체/해제 요청(방장만 가능, 빈 URL이면 해제)
        SetRoomImageRes = 42,           // Server -> Client, 설정 결과 응답(요청자에게만)
        RoomImageChangedNotify = 43,    // Server -> Client, 이미지가 바뀌었을 때 그 방 멤버 전원에게

        RoomMemberListReq = 47,        // Client -> Server, 지금 자신이 있는 방(로비 포함)의 멤버 목록 조회 요청
        RoomMemberListItemRes = 48,    // Server -> Client, 멤버 목록 항목 단건 응답(가변 개수 스트리밍)
        RoomMemberListEndRes = 49,     // Server -> Client, 멤버 목록 전송 완료 및 총 인원수 통지

        // [추가] 방 입장 시 서버가 자동으로 스트리밍해주는 과거 대화 기록.
        ChatHistoryItemRes = 44,        // Server -> Client, 기록 항목 단건(가변 개수 스트리밍) — 요청자에게만
        ChatHistoryEndRes = 45,         // Server -> Client, 기록 전송 완료 — 요청자에게만

        // [추가] 친구 기능 — 서버 쪽 ChatPacketTypes.h와 번호를 정확히 맞춰야 한다
        // (패킷 구조/직렬화는 FriendProtocol.cs).
        FriendRequestReq = 50,              // Client -> Server, 닉네임으로 친구 요청
        FriendRequestRes = 51,
        FriendRespondReq = 52,              // Client -> Server, 받은 요청 수락/거절
        FriendRespondRes = 53,
        FriendRemoveReq = 54,               // Client -> Server, 친구 삭제
        FriendRemoveRes = 55,
        FriendListReq = 56,                 // Client -> Server, 친구 목록 전체 조회
        FriendListBatchRes = 57,            // Server -> Client, 친구 목록 항목 묶음(한 패킷에 최대 20개)
        FriendListEndRes = 58,
        FriendPendingListReq = 59,          // Client -> Server, 받은 요청 목록 한 페이지 조회
        FriendPendingItemRes = 60,
        FriendPendingEndRes = 61,
        FriendRequestNotify = 62,           // Server -> Client, 친구 요청 도착
        FriendAcceptedNotify = 63,          // Server -> Client, 내 요청이 수락됨(또는 즉시 친구)
        FriendRemovedNotify = 64,           // Server -> Client, 친구 관계 해제(상대의 삭제/차단)
        FriendStatusNotify = 65,            // Server -> Client, 친구 접속/종료
        BlockReq = 66,                      // Client -> Server, 닉네임으로 차단
        BlockRes = 67,
        UnblockReq = 68,                    // Client -> Server, 차단 해제
        UnblockRes = 69,
        BlockListReq = 70,                  // Client -> Server, 차단 목록 조회
        BlockListItemRes = 71,
        BlockListEndRes = 72,
        FriendProfileChangedNotify = 73,    // Server -> Client, 친구의 닉네임/프로필 이미지 변경

        // 대화방(1:1/그룹) — ConvProtocol.cs 참고.
        ConvOpenDirectReq = 74,
        ConvCreateGroupReq = 75,
        ConvOpenRes = 76,
        ConvInviteReq = 77,
        ConvInviteRes = 78,
        ConvLeaveReq = 79,
        ConvLeaveRes = 80,
        ConvListReq = 81,
        ConvListItemRes = 82,
        ConvListEndRes = 83,
        ConvSendReq = 84,
        ConvSendRes = 85,
        ConvMessageNotify = 86,
        ConvHistoryReq = 87,
        ConvHistoryItemRes = 88,
        ConvHistoryEndRes = 89,
        ConvReadReq = 90,
        ConvReadNotify = 91,
        ConvMembersReq = 92,
        ConvMemberItemRes = 93,
        ConvMembersEndRes = 94,
        ConvUpdatedNotify = 95,
        ConvDeleteReq = 96,
        ConvDeleteRes = 97,
        ConvDeletedNotify = 98,
        ConvRenameReq = 99,
        ConvRenameRes = 100,
        ConvRenamedNotify = 101,
    }

    // LoginResPacket::reason / ChangeNicknameResPacket::reason 공용.
    public enum LoginResult : byte
    {
        Ok = 0,
        NicknameTaken = 1,
        DbError = 2,
        InvalidNickname = 3,
        AccountNotFound = 4,
        TokenMismatch = 5,
    }

    //***************************************************************************
    // @brief [추가] DeleteChatMessageResPacket::reason — 서버 CChatServerMain::
    //        EDeleteMessageResult와 정확히 같은 값을 유지해야 한다.
    //***************************************************************************
    public enum DeleteMessageResult : byte
    {
        Ok = 0,
        NotFound = 1,   // messageId가 서버의 추적 창(최근 N개)을 벗어났거나 애초에 존재한 적 없음
        NotOwner = 2,   // 요청자가 이 메시지의 작성자가 아님
    }

    //***************************************************************************
    // @brief ChatPacket.h의 프로토콜 상수와 정확히 같은 값을 유지해야 한다.
    //***************************************************************************
    public static class ProtocolConstants
    {
        public const int HeaderBytes = 4;           // PacketHeader: ushort size + ushort type
        public const int TokenBytes = 32;           // kTokenBytes
        public const int PublicIdBytes = 16;        // kPublicIdBytes
        public const int NicknameBytes = 50;        // kNicknameBytes (LoginReqPacket::userId, ChangeNicknameReqPacket::newNickname)
        public const int MessageIdBytes = 8;        // ChatPacket::messageId / DeleteChatMessage*Packet::messageId (int64)
        public const int ChatMessageBytes = 256;    // ChatPacket::message
        public const int GeneratedNicknameBytes = 32;   // NicknameGenerateResPacket::nickname — kNicknameBytes와 별개 상수라 혼동 주의

        public const int ProfileImageUrlBytes = 256;    // kProfileImageUrlBytes
        public const int MaxFriendsPerBatch = 20;       // kMaxFriendsPerBatch — FriendListBatchRes 한 패킷의 최대 항목 수

        public const int RoomNameBytes = 100;    // kRoomNameBytes

        public const int UploadTokenBytes = 64;         // kUploadTokenBytes 대응 — RequestUploadTokenResPacket::uploadToken 필드 크기(16진 인코딩된 32바이트 토큰)

        public const int LobbyRoomId = 0;
        public const int MaxRoomId = 10;
    }

    //***************************************************************************
    // @brief 서버로 보내는 패킷들을 바이트 배열로 조립한다.
    //***************************************************************************
    public static partial class PacketBuilder
    {
        //***************************************************************************
        // @brief 고정 크기 UTF-8 필드를 만든다. 마지막 1바이트는 NUL 종단
        //        여유로 남겨둔다(서버 쪽 안전 복사 관례와 동일).
        //***************************************************************************
        private static byte[] FixedUtf8(string s, int fixedLen)
        {
            var buf = new byte[fixedLen];
            if (string.IsNullOrEmpty(s))
                return buf;

            byte[] bytes = Encoding.UTF8.GetBytes(s);
            int copyLen = Math.Min(bytes.Length, fixedLen - 1);
            Array.Copy(bytes, buf, copyLen);
            return buf;
        }

        private static byte[] PadOrTrim(byte[] src, int fixedLen)
        {
            var buf = new byte[fixedLen];
            if (src != null)
                Array.Copy(src, buf, Math.Min(src.Length, fixedLen));
            return buf;
        }

        //***************************************************************************
        // @brief 신규 가입용 LoginReq. publicId/token 필드는 서버가 무시하므로
        //        0으로 채워 보낸다(hasToken=0).
        //***************************************************************************
        public static byte[] BuildLoginReqNewAccount(string desiredNickname)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.NicknameBytes
                    + ProtocolConstants.PublicIdBytes + 1 + ProtocolConstants.TokenBytes);

                bw.Write(size);
                bw.Write((ushort)PacketType.LoginReq);
                bw.Write(FixedUtf8(desiredNickname, ProtocolConstants.NicknameBytes));
                bw.Write(new byte[ProtocolConstants.PublicIdBytes]);
                bw.Write((byte)0); // hasToken = false
                bw.Write(new byte[ProtocolConstants.TokenBytes]);

                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 재접속용 LoginReq. userId 필드는 서버가 무시하므로 0으로 채운다(hasToken=1).
        //***************************************************************************
        public static byte[] BuildLoginReqReconnect(byte[] publicId, byte[] token)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.NicknameBytes
                    + ProtocolConstants.PublicIdBytes + 1 + ProtocolConstants.TokenBytes);

                bw.Write(size);
                bw.Write((ushort)PacketType.LoginReq);
                bw.Write(new byte[ProtocolConstants.NicknameBytes]);
                bw.Write(PadOrTrim(publicId, ProtocolConstants.PublicIdBytes));
                bw.Write((byte)1); // hasToken = true
                bw.Write(PadOrTrim(token, ProtocolConstants.TokenBytes));

                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 채팅 메시지 전송 패킷을 만든다. nickname/messageId 필드는
        //        서버가 무시하므로(세션의 실제 닉네임으로 채우고, ID는 새로
        //        발급함) 0으로 채워 보낸다.
        //***************************************************************************
        public static byte[] BuildChat(string message)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                // [수정] ChatPacket.h::ChatPacket에 messageId(int64) 필드가
                // 맨 앞(nickname보다 먼저)에 추가됐다 — 메시지 삭제 기능을
                // 위해 서버가 브로드캐스트마다 고유 ID를 부여한다. 클라이언트가
                // 보내는 값은 서버가 무시하므로 0으로 채운다.
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.MessageIdBytes + ProtocolConstants.NicknameBytes
                    + ProtocolConstants.ProfileImageUrlBytes + ProtocolConstants.ChatMessageBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.Chat);
                bw.Write((long)0); // messageId — 서버가 무시하고 새로 부여
                bw.Write(new byte[ProtocolConstants.NicknameBytes]); // 서버가 무시 — 0으로 채움
                bw.Write(new byte[ProtocolConstants.ProfileImageUrlBytes]); // 서버가 무시 — 0으로 채움(nickname과 동일한 규칙)
                bw.Write(FixedUtf8(message, ProtocolConstants.ChatMessageBytes));
                return ms.ToArray();
            }
        }

        public static byte[] BuildNicknameGenerateReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes); // 바디 없음
                bw.Write((ushort)PacketType.NicknameGenerateReq);
                return ms.ToArray();
            }
        }

        public static byte[] BuildChangeNicknameReq(string newNickname)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.NicknameBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.ChangeNicknameReq);
                bw.Write(FixedUtf8(newNickname, ProtocolConstants.NicknameBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 내 프로필 이미지 URL 설정/변경 요청. 빈 문자열이면 "해제".
        //***************************************************************************
        public static byte[] BuildSetProfileImageUrlReq(string url)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.ProfileImageUrlBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.SetProfileImageUrlReq);
                bw.Write(FixedUtf8(url, ProtocolConstants.ProfileImageUrlBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 파일 서버 업로드용 임시 토큰 발급 요청. 바디 없음.
        //***************************************************************************
        public static byte[] BuildRequestUploadTokenReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)PacketType.RequestUploadTokenReq);
                return ms.ToArray();
            }
        }

        public static byte[] BuildListProfileImagesReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)PacketType.ListProfileImagesReq);
                return ms.ToArray();
            }
        }

        public static byte[] BuildSelectProfileImageReq(long imageId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(long));
                bw.Write(size);
                bw.Write((ushort)PacketType.SelectProfileImageReq);
                bw.Write(imageId);
                return ms.ToArray();
            }
        }

        public static byte[] BuildDeleteProfileImageReq(long imageId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(long));
                bw.Write(size);
                bw.Write((ushort)PacketType.DeleteProfileImageReq);
                bw.Write(imageId);
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 채팅 메시지 삭제 요청. messageId는 서버가 그 메시지를
        //        브로드캐스트할 때(ChatPacketData.MessageId) 실어 보낸 값을
        //        그대로 되돌려주면 된다.
        //***************************************************************************
        public static byte[] BuildDeleteChatMessageReq(long messageId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(long));
                bw.Write(size);
                bw.Write((ushort)PacketType.DeleteChatMessageReq);
                bw.Write(messageId);
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 서버 전체 접속자 수(동접자수) 조회 요청. 바디 없음. 폴링용 —
        //        호출부가 주기적으로(예: 39초마다) 이 패킷을 보낸다.
        //***************************************************************************
        public static byte[] BuildServerUserCountReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)PacketType.ServerUserCountReq);
                return ms.ToArray();
            }
        }
    }

    public class LoginResPacketData
    {
        public bool Success;
        public LoginResult Reason;
        public string Nickname; // 서버가 채워 보낸 값 그대로 — 실패 상황에 따라 빈 문자열일 수 있음
        public byte[] PublicId; // Success==true일 때만 유효
        public byte[] Token;        // Success==true일 때만 유효
        public string ProfileImageUrl;  // Success==true일 때만 유효 — 미설정이면 빈 문자열
    }

    public class ChangeNicknameResPacketData
    {
        public bool Success;
        public LoginResult Reason;
    }

    public class SetProfileImageUrlResPacketData
    {
        public bool Success;
        public LoginResult Reason;
        public long ImageId;
    }

    public class RequestUploadTokenResData
    {
        public bool Success;
        public LoginResult Reason;
        public string UploadToken;      // success==true일 때만 유효 — 16진 인코딩된 임시 토큰
        public string FileServerUrl;    // success==true일 때만 유효 — 예: "http://192.168.0.10:8081"
    }

    public class ProfileImageListItemData
    {
        public long ImageId;
        public string ImageRef;
        public bool IsActive;
    }

    public class ListProfileImagesEndResData
    {
        public int TotalCount;
    }

    public class SelectProfileImageResData
    {
        public bool Success;
        public LoginResult Reason;
    }

    public class DeleteProfileImageResData
    {
        public bool Success;
        public LoginResult Reason;
    }

    public class ChatPacketData
    {
        // [추가] 서버가 부여한 고유 메시지 ID — Server -> Client 방향에서만
        // 유효(브로드캐스트 시점에 채워짐). 이 메시지를 나중에 삭제하려면
        // DeleteChatMessageReq에 그대로 실어 보낸다.
        public long MessageId;
        public string SenderNickname;   // Server -> Client 방향에서만 의미 있음(브로드캐스트 시점의 발신자 닉네임)
        public string SenderProfileImageUrl;    // 위와 동일한 의미 — 미설정이면 빈 문자열
        public string Message;
    }

    //***************************************************************************
    // @brief [추가] 방 입장 시 자동으로 스트리밍되는 과거 대화 기록 항목 하나.
    //        ChatPacketData와 필드 구성이 거의 같지만(발신 당시 닉네임/
    //        프로필이미지 스냅샷 + 메시지 + MessageId), 실시간 채팅이 아니라
    //        서버가 Redis에서 읽어온 과거 기록이라는 점과 발신 시각
    //        (TimestampMs)이 추가로 있다는 점이 다르다. MessageId는 실시간
    //        메시지와 동일한 값 체계라 DeleteChatMessageReq에 그대로 쓸 수 있다.
    //***************************************************************************
    public class ChatHistoryItemData
    {
        public int RoomId;    // [추가] 이 기록이 어느 방 것인지 — 늦게 도착한 기록을 걸러내는 용도
        public long MessageId;
        public string SenderNickname;
        public string SenderProfileImageUrl;
        public string Message;
        public long TimestampMs;    // Unix epoch 밀리초
    }

    //***************************************************************************
    // @brief [추가] 과거 대화 기록 전송 완료.
    //***************************************************************************
    public class ChatHistoryEndData
    {
        public int RoomId;
        public int TotalCount;
    }

    public class ServerUserCountResData
    {
        public int UserCount;
        public int LobbyUserCount;
    }

    //***************************************************************************
    // @brief [추가] 채팅 메시지 삭제 요청에 대한 응답(요청자에게만 옴).
    //***************************************************************************
    public class DeleteChatMessageResData
    {
        public bool Success;
        public DeleteMessageResult Reason;
    }

    //***************************************************************************
    // @brief [추가] 채팅 메시지 삭제 알림 — messageId가 일치하는 메시지를
    //        받는 쪽 화면에서 찾아 제거하면 된다. 요청자 본인에게도 온다.
    //***************************************************************************
    public class DeleteChatMessageNotifyData
    {
        public long MessageId;
    }

    //***************************************************************************
    // @brief 서버 -> 클라이언트 패킷을 파싱한다. buffer는 헤더(4바이트)를
    //        포함한 완전한 패킷 하나 전체라고 가정한다(ChatNetworkClient의
    //        수신 루프가 프레이밍을 이미 처리했음).
    //***************************************************************************
    public static partial class PacketParser
    {
        private static string Utf8FromFixed(byte[] fixedBuf)
        {
            int len = Array.IndexOf(fixedBuf, (byte)0);
            if (len < 0)
                len = fixedBuf.Length;
            return Encoding.UTF8.GetString(fixedBuf, 0, len);
        }

        public static LoginResPacketData ParseLoginRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16(); // size
                br.ReadUInt16(); // type
                bool success = br.ReadByte() != 0;
                LoginResult reason = (LoginResult)br.ReadByte();
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                byte[] token = br.ReadBytes(ProtocolConstants.TokenBytes);
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new LoginResPacketData
                {
                    Success = success,
                    Reason = reason,
                    Nickname = nickname,
                    PublicId = publicId,
                    Token = token,
                    ProfileImageUrl = profileImageUrl,
                };
            }
        }

        public static ChatPacketData ParseChat(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                long messageId = br.ReadInt64();
                string senderNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string senderProfileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                string message = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ChatMessageBytes));
                return new ChatPacketData
                {
                    MessageId = messageId,
                    SenderNickname = senderNickname,
                    SenderProfileImageUrl = senderProfileImageUrl,
                    Message = message,
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 과거 대화 기록 항목 파싱. 필드 순서가 ChatPacket과
        //        같되(messageId, nickname, profileImageUrl, message) 끝에
        //        timestampMs(long)가 하나 더 붙는다.
        //***************************************************************************
        public static ChatHistoryItemData ParseChatHistoryItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int roomId = br.ReadInt32();
                long messageId = br.ReadInt64();
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                string message = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ChatMessageBytes));
                long timestampMs = br.ReadInt64();
                return new ChatHistoryItemData
                {
                    RoomId = roomId,
                    MessageId = messageId,
                    SenderNickname = nickname,
                    SenderProfileImageUrl = profileImageUrl,
                    Message = message,
                    TimestampMs = timestampMs,
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 과거 대화 기록 전송 완료 파싱.
        //***************************************************************************
        public static ChatHistoryEndData ParseChatHistoryEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new ChatHistoryEndData
                {
                    RoomId = br.ReadInt32(),
                    TotalCount = br.ReadInt32(),
                };
            }
        }

        public static string ParseNicknameGenerateRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return Utf8FromFixed(br.ReadBytes(ProtocolConstants.GeneratedNicknameBytes));
            }
        }

        public static ChangeNicknameResPacketData ParseChangeNicknameRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new ChangeNicknameResPacketData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (LoginResult)br.ReadByte(),
                };
            }
        }

        public static SetProfileImageUrlResPacketData ParseSetProfileImageUrlRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new SetProfileImageUrlResPacketData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (LoginResult)br.ReadByte(),
                    ImageId = br.ReadInt64(),
                };
            }
        }

        public static RequestUploadTokenResData ParseRequestUploadTokenRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                LoginResult reason = (LoginResult)br.ReadByte();
                string uploadToken = Utf8FromFixed(br.ReadBytes(ProtocolConstants.UploadTokenBytes));
                string fileServerUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new RequestUploadTokenResData
                {
                    Success = success,
                    Reason = reason,
                    UploadToken = uploadToken,
                    FileServerUrl = fileServerUrl,
                };
            }
        }

        public static ProfileImageListItemData ParseListProfileImagesItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                long imageId = br.ReadInt64();
                string imageRef = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                bool isActive = br.ReadByte() != 0;
                return new ProfileImageListItemData { ImageId = imageId, ImageRef = imageRef, IsActive = isActive };
            }
        }

        public static ListProfileImagesEndResData ParseListProfileImagesEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new ListProfileImagesEndResData { TotalCount = br.ReadInt32() };
            }
        }

        public static SelectProfileImageResData ParseSelectProfileImageRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new SelectProfileImageResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (LoginResult)br.ReadByte(),
                };
            }
        }

        public static DeleteProfileImageResData ParseDeleteProfileImageRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new DeleteProfileImageResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (LoginResult)br.ReadByte(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 채팅 메시지 삭제 응답 파싱.
        //***************************************************************************
        public static DeleteChatMessageResData ParseDeleteChatMessageRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new DeleteChatMessageResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (DeleteMessageResult)br.ReadByte(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 채팅 메시지 삭제 알림 파싱.
        //***************************************************************************
        public static DeleteChatMessageNotifyData ParseDeleteChatMessageNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new DeleteChatMessageNotifyData { MessageId = br.ReadInt64() };
            }
        }

        public static ServerUserCountResData ParseServerUserCountRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new ServerUserCountResData
                {
                    UserCount = br.ReadInt32(),
                    LobbyUserCount = br.ReadInt32(),
                };
            }
        }
    }
}