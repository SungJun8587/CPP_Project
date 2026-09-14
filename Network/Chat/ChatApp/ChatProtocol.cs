
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
    }

    // RoomEnterResPacket::reason
    public enum RoomResult : byte
    {
        Ok = 0,
        InvalidRoomId = 1,
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
    // @brief ChatPacket.h의 프로토콜 상수와 정확히 같은 값을 유지해야 한다.
    //***************************************************************************
    public static class ProtocolConstants
    {
        public const int HeaderBytes = 4;           // PacketHeader: ushort size + ushort type
        public const int TokenBytes = 32;           // kTokenBytes
        public const int PublicIdBytes = 16;        // kPublicIdBytes
        public const int NicknameBytes = 50;        // kNicknameBytes (LoginReqPacket::userId, ChangeNicknameReqPacket::newNickname)
        public const int ChatMessageBytes = 256;    // ChatPacket::message
        public const int GeneratedNicknameBytes = 32;   // NicknameGenerateResPacket::nickname — kNicknameBytes와 별개 상수라 혼동 주의

        public const int ProfileImageUrlBytes = 256;    // kProfileImageUrlBytes

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
        // @brief 채팅 메시지 전송 패킷을 만든다. nickname 필드는 서버가 무시하므로
        //        (세션의 실제 닉네임으로 채워 재브로드캐스트함) 0으로 채워 보낸다.
        //***************************************************************************
        public static byte[] BuildChat(string message)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.NicknameBytes + ProtocolConstants.ChatMessageBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.Chat);
                bw.Write(new byte[ProtocolConstants.NicknameBytes]); // 서버가 무시 — 0으로 채움
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
        // @brief 업로드 시작 요청. fileExtension은 ".png" 등(점 포함).
        //***************************************************************************
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
        // @brief 방 입장 요청. roomId는 1~ProtocolConstants.MaxRoomId만 유효(서버가 재검증).
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
        public string SenderNickname;   // Server -> Client 방향에서만 의미 있음(브로드캐스트 시점의 발신자 닉네임)
        public string SenderProfileImageUrl;    // 위와 동일한 의미 — 미설정이면 빈 문자열
        public string Message;
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
                string senderNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string senderProfileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                string message = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ChatMessageBytes));
                return new ChatPacketData { SenderNickname = senderNickname, SenderProfileImageUrl = senderProfileImageUrl, Message = message };
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