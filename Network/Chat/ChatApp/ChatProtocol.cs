
//***************************************************************************
// ChatProtocol.cs : 서버(ChatPacket.h)와 정확히 같은 바이트 레이아웃을
//                   재현하는 패킷 정의 + 직렬화/역직렬화.
//
// [중요] 아래 상수/필드 순서/크기는 C++ ChatPacket.h와 반드시 1:1로
// 일치해야 한다. 서버가 #pragma pack(push,1)로 패딩 없이 정의하므로,
// 여기서도 필드 순서 그대로, 패딩 없이 직렬화한다.
// BinaryWriter/BinaryReader는 기본적으로 리틀 엔디안으로 동작하는데,
// x86/x64는 전부 리틀 엔디안이라 서버(Windows/MSVC, x86/x64)와 그대로 호환된다.
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
        // 정확히 맞춰야 한다(DeleteChatMessageHandler.cpp 참고).
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
        ListRoomsReq = 37,              // Client -> Server, 존재하는 모든 방 목록 조회 요청
        ListRoomsItemRes = 38,          // Server -> Client, 방 목록 항목 단건 응답(가변 개수 스트리밍)
        ListRoomsEndRes = 39,           // Server -> Client, 방 목록 전송 완료
        RoomOwnerChangedNotify = 40,    // Server -> Client, 방장이 나가서 다른 멤버에게 자동 이양됐을 때 그 방 멤버 전원에게

        // [추가] 방 프로필 이미지 설정/교체/해제(방장 전용) + 변경 알림.
        SetRoomImageReq = 41,           // Client -> Server, 방 프로필 이미지 설정/교체/해제 요청(방장만 가능, 빈 URL이면 해제)
        SetRoomImageRes = 42,           // Server -> Client, 설정 결과 응답(요청자에게만)
        RoomImageChangedNotify = 43,    // Server -> Client, 이미지가 바뀌었을 때 그 방 멤버 전원에게

        // [추가] 방 입장 시 서버가 자동으로 스트리밍해주는 과거 대화 기록.
        ChatHistoryItemRes = 44,        // Server -> Client, 기록 항목 단건(가변 개수 스트리밍) — 요청자에게만
        ChatHistoryEndRes = 45,         // Server -> Client, 기록 전송 완료 — 요청자에게만
    }

    // RoomEnterResPacket::reason 및 방 생성/삭제/이름변경 결과 공용.
    // [수정] 방 생성/삭제/이름변경 기능 도입으로 사유가 늘었다 — 서버
    // ChatPacketTypes.h::ERoomResult와 정확히 같은 값을 유지해야 한다.
    public enum RoomResult : byte
    {
        Ok = 0,
        InvalidRoomId = 1,
        RoomNotFound = 2,
        NotOwner = 3,
        RoomLimitExceeded = 4,
        InvalidName = 5,
        DbError = 6,
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

        public const int RoomNameBytes = 100;    // kRoomNameBytes

        public const int UploadTokenBytes = 64;         // kUploadTokenBytes 대응 — RequestUploadTokenResPacket::uploadToken 필드 크기(16진 인코딩된 32바이트 토큰)

        public const int LobbyRoomId = 0;
        public const int MaxRoomId = 10;
    }

    //***************************************************************************
    // @brief 서버로 보내는 패킷들을 바이트 배열로 조립한다.
    //***************************************************************************
    public static class PacketBuilder
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
        // @brief 방 입장 요청. [수정] roomId는 더 이상 고정 범위(1~MaxRoomId)가
        //        아니다 — 서버가 CChatServerMain::RoomExists()로 실제 존재
        //        여부를 확인한다(동적 생성/삭제되는 방).
        //***************************************************************************
        public static byte[] BuildRoomEnterReq(int roomId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int));
                bw.Write(size);
                bw.Write((ushort)PacketType.RoomEnterReq);
                bw.Write(roomId);
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 방 생성 요청. 성공하면 요청자가 방장이 된다.
        //***************************************************************************
        public static byte[] BuildCreateRoomReq(string roomName)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.RoomNameBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.CreateRoomReq);
                bw.Write(FixedUtf8(roomName, ProtocolConstants.RoomNameBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 방 삭제 요청. 요청자가 그 방의 현재 방장이어야 한다.
        //***************************************************************************
        public static byte[] BuildDeleteRoomReq(int roomId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int));
                bw.Write(size);
                bw.Write((ushort)PacketType.DeleteRoomReq);
                bw.Write(roomId);
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 방 이름 변경 요청. 요청자가 그 방의 현재 방장이어야 한다.
        //***************************************************************************
        public static byte[] BuildRenameRoomReq(int roomId, string newName)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int) + ProtocolConstants.RoomNameBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.RenameRoomReq);
                bw.Write(roomId);
                bw.Write(FixedUtf8(newName, ProtocolConstants.RoomNameBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 존재하는 모든 방 목록 조회 요청. 바디 없음 — 로그인한
        //        사용자면 누구나 조회 가능.
        //***************************************************************************
        public static byte[] BuildListRoomsReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)PacketType.ListRoomsReq);
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief [추가] 방 프로필 이미지 설정/교체/해제 요청. imageUrl이 빈
        //        문자열이면 해제(기본 이미지로 되돌림). 요청자가 그 방의
        //        현재 방장이어야 한다(서버가 검증).
        //***************************************************************************
        public static byte[] BuildSetRoomImageReq(int roomId, string imageUrl)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int) + ProtocolConstants.ProfileImageUrlBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.SetRoomImageReq);
                bw.Write(roomId);
                bw.Write(FixedUtf8(imageUrl ?? string.Empty, ProtocolConstants.ProfileImageUrlBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 방 퇴장(로비 복귀) 요청. 바디 없음.
        //***************************************************************************
        public static byte[] BuildRoomLeaveReq()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)PacketType.RoomLeaveReq);
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

    //***************************************************************************
    // @brief [추가] 방 생성 응답.
    //***************************************************************************
    public class CreateRoomResData
    {
        public bool Success;
        public RoomResult Reason;
        public int RoomId;  // Success==true일 때만 유효
    }

    //***************************************************************************
    // @brief [추가] 방 삭제 응답(요청자에게만).
    //***************************************************************************
    public class DeleteRoomResData
    {
        public bool Success;
        public RoomResult Reason;
    }

    //***************************************************************************
    // @brief [추가] 방 삭제 알림 — 그 방에 있던 멤버 전원에게(요청자 포함).
    //        받는 쪽은 로비로 돌아간 것으로 화면을 갱신하면 된다(서버도
    //        실제로 그렇게 이동시킨 뒤 이 알림을 보낸다).
    //***************************************************************************
    public class DeleteRoomNotifyData
    {
        public int RoomId;
    }

    //***************************************************************************
    // @brief [추가] 방 이름 변경 응답(요청자에게만).
    //***************************************************************************
    public class RenameRoomResData
    {
        public bool Success;
        public RoomResult Reason;
    }

    //***************************************************************************
    // @brief [추가] 방 이름 변경 알림 — 그 방에 있는 멤버 전원에게(요청자 포함).
    //***************************************************************************
    public class RenameRoomNotifyData
    {
        public int RoomId;
        public string NewName;
    }

    //***************************************************************************
    // @brief [추가] 방 목록 항목 하나.
    //***************************************************************************
    public class RoomListItemData
    {
        public int RoomId;
        public string Name;
        public string OwnerNickname;
        public int UserCount;
        public string ImageUrl;    // [추가] 방 프로필 이미지. 비어있으면 기본 이미지
    }

    //***************************************************************************
    // @brief [추가] 방 목록 전송 완료.
    //***************************************************************************
    public class ListRoomsEndResData
    {
        public int TotalCount;
    }

    //***************************************************************************
    // @brief [추가] 방장 자동 이양 알림 — 그 방에 있는 멤버 전원에게(새 방장 포함).
    //***************************************************************************
    public class RoomOwnerChangedNotifyData
    {
        public int RoomId;
        public string NewOwnerNickname;
    }

    //***************************************************************************
    // @brief [추가] 방 프로필 이미지 설정 응답(요청자에게만).
    //***************************************************************************
    public class SetRoomImageResData
    {
        public bool Success;
        public RoomResult Reason;
    }

    //***************************************************************************
    // @brief [추가] 방 프로필 이미지 변경 알림 — 그 방 멤버 전원에게(요청자 포함).
    //***************************************************************************
    public class RoomImageChangedNotifyData
    {
        public int RoomId;
        public string ImageUrl;    // 빈 문자열이면 기본 이미지로 되돌아간 것
    }

    public class RoomEnterResPacketData
    {
        public bool Success;
        public RoomResult Reason;
        public int RoomId;
        public int RoomUserCount;   // Success==true일 때만 유효
    }

    public class RoomLeaveResPacketData
    {
        public bool Success;
        public int RoomId;          // 방금까지 있었던 방(로비였으면 LobbyRoomId)
        public int RoomUserCount;   // Success==true일 때만 유효 — 퇴장 후 그 방에 남은 인원
    }

    public class RoomUserCountNotifyData
    {
        public int RoomId;
        public int UserCount;
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
    public static class PacketParser
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
                long messageId = br.ReadInt64();
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                string message = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ChatMessageBytes));
                long timestampMs = br.ReadInt64();
                return new ChatHistoryItemData
                {
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

        //***************************************************************************
        // @brief [추가] 방 생성 응답 파싱.
        //***************************************************************************
        public static CreateRoomResData ParseCreateRoomRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new CreateRoomResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (RoomResult)br.ReadByte(),
                    RoomId = br.ReadInt32(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 삭제 응답 파싱.
        //***************************************************************************
        public static DeleteRoomResData ParseDeleteRoomRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new DeleteRoomResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (RoomResult)br.ReadByte(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 삭제 알림 파싱.
        //***************************************************************************
        public static DeleteRoomNotifyData ParseDeleteRoomNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new DeleteRoomNotifyData { RoomId = br.ReadInt32() };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 이름 변경 응답 파싱.
        //***************************************************************************
        public static RenameRoomResData ParseRenameRoomRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new RenameRoomResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (RoomResult)br.ReadByte(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 이름 변경 알림 파싱.
        //***************************************************************************
        public static RenameRoomNotifyData ParseRenameRoomNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int roomId = br.ReadInt32();
                string newName = Utf8FromFixed(br.ReadBytes(ProtocolConstants.RoomNameBytes));
                return new RenameRoomNotifyData { RoomId = roomId, NewName = newName };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 목록 항목 단건 파싱.
        //***************************************************************************
        public static RoomListItemData ParseListRoomsItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int roomId = br.ReadInt32();
                string name = Utf8FromFixed(br.ReadBytes(ProtocolConstants.RoomNameBytes));
                string ownerNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                int userCount = br.ReadInt32();
                string imageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new RoomListItemData { RoomId = roomId, Name = name, OwnerNickname = ownerNickname, UserCount = userCount, ImageUrl = imageUrl };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 프로필 이미지 설정 응답 파싱.
        //***************************************************************************
        public static SetRoomImageResData ParseSetRoomImageRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new SetRoomImageResData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (RoomResult)br.ReadByte(),
                };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 프로필 이미지 변경 알림 파싱.
        //***************************************************************************
        public static RoomImageChangedNotifyData ParseRoomImageChangedNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int roomId = br.ReadInt32();
                string imageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new RoomImageChangedNotifyData { RoomId = roomId, ImageUrl = imageUrl };
            }
        }

        //***************************************************************************
        // @brief [추가] 방 목록 전송 완료 파싱.
        //***************************************************************************
        public static ListRoomsEndResData ParseListRoomsEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new ListRoomsEndResData { TotalCount = br.ReadInt32() };
            }
        }

        //***************************************************************************
        // @brief [추가] 방장 자동 이양 알림 파싱.
        //***************************************************************************
        public static RoomOwnerChangedNotifyData ParseRoomOwnerChangedNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int roomId = br.ReadInt32();
                string newOwnerNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                return new RoomOwnerChangedNotifyData { RoomId = roomId, NewOwnerNickname = newOwnerNickname };
            }
        }

        public static RoomEnterResPacketData ParseRoomEnterRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new RoomEnterResPacketData
                {
                    Success = br.ReadByte() != 0,
                    Reason = (RoomResult)br.ReadByte(),
                    RoomId = br.ReadInt32(),
                    RoomUserCount = br.ReadInt32(),
                };
            }
        }

        public static RoomLeaveResPacketData ParseRoomLeaveRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                return new RoomLeaveResPacketData
                {
                    Success = success,
                    RoomId = br.ReadInt32(),
                    RoomUserCount = br.ReadInt32(),
                };
            }
        }

        public static RoomUserCountNotifyData ParseRoomUserCountNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new RoomUserCountNotifyData
                {
                    RoomId = br.ReadInt32(),
                    UserCount = br.ReadInt32(),
                };
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