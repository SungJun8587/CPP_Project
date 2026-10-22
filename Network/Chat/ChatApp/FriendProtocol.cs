
//***************************************************************************
// FriendProtocol.cs : 친구 기능 패킷 정의 + 직렬화/역직렬화.
//
// 서버 ChatPacket.h / ChatPacketTypes.h의 친구 패킷(50~73번)과 바이트 레이아웃이
// 1:1로 일치해야 한다. PacketBuilder/PacketParser는 ChatProtocol.cs와 같은
// 클래스의 partial 정의이며, 이 파일은 친구 기능 부분만 담는다.
//***************************************************************************

using System;
using System.IO;

namespace ChatApp
{
    //***************************************************************************
    // @brief 친구 기능 Res 패킷의 reason 값. 서버 ChatPacketTypes.h::EFriendResult와
    //        정확히 같은 값을 유지해야 한다.
    //***************************************************************************
    public enum FriendResult : byte
    {
        Ok = 0,
        DbError = 1,
        UserNotFound = 2,
        SelfTarget = 3,
        AlreadyFriends = 4,
        AlreadyRequested = 5,
        NoPendingRequest = 6,
        NotFriends = 7,
        BlockedByMe = 8,
        FriendLimit = 9,
        AlreadyBlocked = 10,
        NotBlocked = 11,
    }

    //***************************************************************************
    // @brief 서버 ChatPacketTypes.h의 kMaxFriends / kMaxPendingPageSize와 같은 값.
    //***************************************************************************
    public static class FriendConstants
    {
        public const int MaxFriends = 200;
        public const int MaxPendingPageSize = 50;
    }

    //***************************************************************************
    // @brief 친구 요청 결과. PublicId는 Success일 때 대상 계정 식별자.
    //        AutoAccepted는 상대의 기존 요청과 맞물려 즉시 친구가 된 경우.
    //***************************************************************************
    public class FriendRequestResData
    {
        public bool Success;
        public FriendResult Reason;
        public bool AutoAccepted;
        public byte[] PublicId;
    }

    //***************************************************************************
    // @brief 받은 요청 수락/거절 결과. Online은 수락 성공 시 새 친구의 접속 여부.
    //***************************************************************************
    public class FriendRespondResData
    {
        public bool Success;
        public FriendResult Reason;
        public bool Accepted;
        public byte[] PublicId;
        public bool Online;
    }

    //***************************************************************************
    // @brief 친구 삭제 / 차단 / 차단 해제 결과 공용(대상 식별자만 담는 응답).
    //***************************************************************************
    public class FriendTargetResData
    {
        public bool Success;
        public FriendResult Reason;
        public byte[] PublicId;
    }

    public class FriendItemData
    {
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
        public bool Online;
    }

    //***************************************************************************
    // @brief 친구 목록 / 차단 목록 전송 완료 공용. TotalCount는 전송된 항목 수.
    //***************************************************************************
    public class FriendListEndData
    {
        public bool Success;
        public FriendResult Reason;
        public int TotalCount;
    }

    public class FriendPendingItemData
    {
        public int RequestId;
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
    }

    //***************************************************************************
    // @brief 받은 요청 한 페이지 전송 완료. TotalCount는 받은 요청 전체 개수,
    //        Page/PageSize는 서버가 보정한 뒤 실제로 적용한 값이다.
    //***************************************************************************
    public class FriendPendingEndData
    {
        public int RequestId;
        public bool Success;
        public FriendResult Reason;
        public int TotalCount;
        public int Page;
        public int PageSize;
    }

    public class BlockItemData
    {
        public byte[] PublicId;
        public string Nickname;
    }

    public class FriendRequestNotifyData
    {
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
    }

    public class FriendAcceptedNotifyData
    {
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
        public bool Online;
    }

    public class FriendRemovedNotifyData
    {
        public byte[] PublicId;
    }

    public class FriendProfileChangedNotifyData
    {
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
    }

    public class FriendStatusNotifyData
    {
        public byte[] PublicId;
        public bool Online;
    }

    public static partial class PacketBuilder
    {
        //***************************************************************************
        // @brief 바디가 publicId 하나뿐인 요청(삭제/차단 해제)을 만든다.
        //***************************************************************************
        private static byte[] BuildPublicIdOnly(PacketType type, byte[] publicId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.PublicIdBytes));
                bw.Write((ushort)type);
                bw.Write(PadOrTrim(publicId, ProtocolConstants.PublicIdBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 바디가 닉네임 하나뿐인 요청(친구 요청/차단)을 만든다.
        //***************************************************************************
        private static byte[] BuildNicknameOnly(PacketType type, string nickname)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.NicknameBytes));
                bw.Write((ushort)type);
                bw.Write(FixedUtf8(nickname ?? string.Empty, ProtocolConstants.NicknameBytes));
                return ms.ToArray();
            }
        }

        private static byte[] BuildHeaderOnly(PacketType type)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)ProtocolConstants.HeaderBytes);
                bw.Write((ushort)type);
                return ms.ToArray();
            }
        }

        public static byte[] BuildFriendRequestReq(string nickname) => BuildNicknameOnly(PacketType.FriendRequestReq, nickname);

        public static byte[] BuildFriendRespondReq(byte[] publicId, bool accept)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)(ProtocolConstants.HeaderBytes + ProtocolConstants.PublicIdBytes + sizeof(byte)));
                bw.Write((ushort)PacketType.FriendRespondReq);
                bw.Write(PadOrTrim(publicId, ProtocolConstants.PublicIdBytes));
                bw.Write((byte)(accept ? 1 : 0));
                return ms.ToArray();
            }
        }

        public static byte[] BuildFriendRemoveReq(byte[] publicId) => BuildPublicIdOnly(PacketType.FriendRemoveReq, publicId);
        public static byte[] BuildFriendListReq() => BuildHeaderOnly(PacketType.FriendListReq);

        public static byte[] BuildFriendPendingListReq(int requestId, int page, int pageSize)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)(ProtocolConstants.HeaderBytes + sizeof(int) * 3));
                bw.Write((ushort)PacketType.FriendPendingListReq);
                bw.Write(requestId);
                bw.Write(page);
                bw.Write(pageSize);
                return ms.ToArray();
            }
        }

        public static byte[] BuildBlockReq(string nickname) => BuildNicknameOnly(PacketType.BlockReq, nickname);
        public static byte[] BuildUnblockReq(byte[] publicId) => BuildPublicIdOnly(PacketType.UnblockReq, publicId);
        public static byte[] BuildBlockListReq() => BuildHeaderOnly(PacketType.BlockListReq);
    }

    public static partial class PacketParser
    {
        public static FriendRequestResData ParseFriendRequestRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                var reason = (FriendResult)br.ReadByte();
                bool autoAccepted = br.ReadByte() != 0;
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                return new FriendRequestResData { Success = success, Reason = reason, AutoAccepted = autoAccepted, PublicId = publicId };
            }
        }

        public static FriendRespondResData ParseFriendRespondRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                var reason = (FriendResult)br.ReadByte();
                bool accepted = br.ReadByte() != 0;
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                bool online = br.ReadByte() != 0;
                return new FriendRespondResData { Success = success, Reason = reason, Accepted = accepted, PublicId = publicId, Online = online };
            }
        }

        //***************************************************************************
        // @brief FriendRemoveRes / BlockRes / UnblockRes 공용 — 세 패킷 모두
        //        success + reason + publicId 레이아웃이 같다.
        //***************************************************************************
        public static FriendTargetResData ParseFriendTargetRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                var reason = (FriendResult)br.ReadByte();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                return new FriendTargetResData { Success = success, Reason = reason, PublicId = publicId };
            }
        }

        public static FriendItemData ParseFriendListItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                bool online = br.ReadByte() != 0;
                return new FriendItemData { PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl, Online = online };
            }
        }

        //***************************************************************************
        // @brief FriendListEndRes / BlockListEndRes 공용 — success + reason + totalCount.
        //***************************************************************************
        public static FriendListEndData ParseFriendListEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                bool success = br.ReadByte() != 0;
                var reason = (FriendResult)br.ReadByte();
                int totalCount = br.ReadInt32();
                return new FriendListEndData { Success = success, Reason = reason, TotalCount = totalCount };
            }
        }

        public static FriendPendingItemData ParseFriendPendingItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int requestId = br.ReadInt32();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new FriendPendingItemData { RequestId = requestId, PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl };
            }
        }

        public static FriendPendingEndData ParseFriendPendingEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int requestId = br.ReadInt32();
                bool success = br.ReadByte() != 0;
                var reason = (FriendResult)br.ReadByte();
                int totalCount = br.ReadInt32();
                int page = br.ReadInt32();
                int pageSize = br.ReadInt32();
                return new FriendPendingEndData { RequestId = requestId, Success = success, Reason = reason, TotalCount = totalCount, Page = page, PageSize = pageSize };
            }
        }

        public static BlockItemData ParseBlockListItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                return new BlockItemData { PublicId = publicId, Nickname = nickname };
            }
        }

        public static FriendRequestNotifyData ParseFriendRequestNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new FriendRequestNotifyData { PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl };
            }
        }

        public static FriendAcceptedNotifyData ParseFriendAcceptedNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                bool online = br.ReadByte() != 0;
                return new FriendAcceptedNotifyData { PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl, Online = online };
            }
        }

        public static FriendRemovedNotifyData ParseFriendRemovedNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                return new FriendRemovedNotifyData { PublicId = br.ReadBytes(ProtocolConstants.PublicIdBytes) };
            }
        }

        public static FriendProfileChangedNotifyData ParseFriendProfileChangedNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new FriendProfileChangedNotifyData { PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl };
            }
        }

        public static FriendStatusNotifyData ParseFriendStatusNotify(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                bool online = br.ReadByte() != 0;
                return new FriendStatusNotifyData { PublicId = publicId, Online = online };
            }
        }
    }
}