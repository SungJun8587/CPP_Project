
//***************************************************************************
// ConvProtocol.cs : 대화방(1:1/그룹) 패킷 정의 + 직렬화/역직렬화.
//
// 서버 ChatPacket.h / ChatPacketTypes.h의 대화방 패킷(74~101번)과 바이트 레이아웃이
// 1:1로 일치해야 한다. PacketBuilder/PacketParser는 ChatProtocol.cs와 같은
// 클래스의 partial 정의이며, 이 파일은 대화방 부분만 담는다.
//***************************************************************************

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace ChatApp
{
    //***************************************************************************
    // @brief 대화방 Res 패킷의 reason 값. 서버 ChatPacketTypes.h::EConvResult와
    //        정확히 같은 값을 유지해야 한다.
    //***************************************************************************
    public enum ConvResult : byte
    {
        Ok = 0,
        DbError = 1,
        NotFriends = 2,
        Blocked = 3,
        NotMember = 4,
        NotGroup = 5,
        InvalidTarget = 6,
        MemberLimit = 7,
        InvalidName = 8,
        InvalidMessage = 9,
        InvalidReply = 10,
        NotFound = 11,
        NotSender = 12,
    }

    public enum ConvType : byte
    {
        Direct = 1,
        Group = 2,
    }

    public enum ConvMessageKind : byte
    {
        Text = 0,
        System = 1,
        Image = 2,      // Message = 이미지 URL
        Deleted = 3,    // 발신자가 삭제함(Message 비어 있음)
    }

    //***************************************************************************
    // @brief 서버 ChatPacketTypes.h의 kConv* 상수와 같은 값.
    //***************************************************************************
    public static class ConvConstants
    {
        public const int NameBytes = 50;            // kConvNameBytes
        public const int MessageBytes = 512;        // kConvMessageBytes
        public const int LastMessageBytes = 100;    // ConvSummaryFields::lastMessage
        public const int MaxInvite = 20;            // kMaxConvInvite
        public const int MaxMembers = 100;          // kMaxConvMembers
        public const int MaxHistoryPage = 50;       // kMaxConvHistoryPage
        public const int ReplyPreviewBytes = 100;   // kConvReplyPreviewBytes
    }

    //***************************************************************************
    // @brief 대화방 요약(목록 항목/열기 결과/갱신 알림 공통).
    //        Direct면 Name은 비어 있고 Peer* 가 상대 정보다. LastMsgId==0이면 보여줄 메시지가 없다.
    //***************************************************************************
    public class ConvSummaryData
    {
        public long ConvId;
        public ConvType Type;
        public string Name;
        public int MemberCount;
        public int UnreadCount;
        public long LastMsgId;
        public long LastMsgTime;            // Unix 초
        public ConvMessageKind LastKind;
        public string LastMessage;
        public long MyLastReadMsgId;
        public byte[] PeerPublicId;
        public string PeerNickname;
        public string PeerImageUrl;
    }

    public class ConvOpenResData
    {
        public bool Success;
        public ConvResult Reason;
        public bool IsNew;
        public ConvSummaryData Summary;
    }

    //***************************************************************************
    // @brief 초대/나가기/전송 결과 공용. MsgId는 전송 성공 시에만 의미 있다.
    //***************************************************************************
    public class ConvResultData
    {
        public bool Success;
        public ConvResult Reason;
        public long ConvId;
        public long MsgId;
    }

    public class ConvListEndData
    {
        public bool Success;
        public ConvResult Reason;
        public int TotalCount;
    }

    //***************************************************************************
    // @brief 메시지 한 건. 새 메시지 푸시와 기록 항목이 같은 모양이다.
    //***************************************************************************
    public class ConvMessageData
    {
        public long ConvId;
        public long MsgId;
        public ConvMessageKind Kind;
        public byte[] SenderPublicId;
        public string SenderNickname;
        public string SenderImageUrl;
        public long Timestamp;              // Unix 초
        public string Message;
        public long ReplyToMsgId;           // 0이면 답장 아님
        public ConvMessageKind ReplyKind;   // 답장 대상 종류 (보이지 않거나 삭제됐으면 Deleted)
        public string ReplySenderNickname;
        public string ReplyPreview;         // 대상이 Text일 때 첫 줄
    }

    public class ConvRenamedData
    {
        public long ConvId;
        public string Name;     // 빈 문자열이면 이름 해제
    }

    public class ConvDeletedData
    {
        public long ConvId;
        public long MsgId;
    }

    public class ConvHistoryEndData
    {
        public bool Success;
        public ConvResult Reason;
        public bool HasMore;
        public long ConvId;
        public int Count;
        public int RequestId;
    }

    public class ConvReadNotifyData
    {
        public long ConvId;
        public byte[] ReaderPublicId;
        public long UpToMsgId;
    }

    public class ConvMemberData
    {
        public long ConvId;
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
        public long LastReadMsgId;
        public bool Online;
    }

    public class ConvMembersEndData
    {
        public bool Success;
        public ConvResult Reason;
        public long ConvId;
        public int TotalCount;
    }

    public static partial class PacketBuilder
    {
        // 헤더(size, type)를 쓰고 BinaryWriter를 돌려준다. 호출부가 바디를 쓴 뒤 Finish()로 크기를 채운다.
        private static byte[] FinishPacket(MemoryStream ms)
        {
            byte[] bytes = ms.ToArray();
            ushort size = (ushort)bytes.Length;
            bytes[0] = (byte)(size & 0xFF);
            bytes[1] = (byte)(size >> 8);
            return bytes;
        }

        private static void WritePublicIdList(BinaryWriter bw, IList<byte[]> members)
        {
            int count = Math.Min(members?.Count ?? 0, ConvConstants.MaxInvite);
            bw.Write((byte)count);
            for (int i = 0; i < ConvConstants.MaxInvite; i++)
                bw.Write(PadOrTrim(i < count ? members[i] : null, ProtocolConstants.PublicIdBytes));
        }

        public static byte[] BuildConvOpenDirectReq(byte[] publicId) => BuildPublicIdOnly(PacketType.ConvOpenDirectReq, publicId);

        public static byte[] BuildConvCreateGroupReq(string name, IList<byte[]> members)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvCreateGroupReq);
                bw.Write(FixedUtf8(name ?? string.Empty, ConvConstants.NameBytes));
                WritePublicIdList(bw, members);
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvInviteReq(long convId, IList<byte[]> members)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvInviteReq);
                bw.Write(convId);
                WritePublicIdList(bw, members);
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        private static byte[] BuildConvIdOnly(PacketType type, long convId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)type);
                bw.Write(convId);
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvLeaveReq(long convId) => BuildConvIdOnly(PacketType.ConvLeaveReq, convId);
        public static byte[] BuildConvListReq() => BuildHeaderOnly(PacketType.ConvListReq);
        public static byte[] BuildConvMembersReq(long convId) => BuildConvIdOnly(PacketType.ConvMembersReq, convId);

        public static byte[] BuildConvSendReq(long convId, string message, ConvMessageKind kind = ConvMessageKind.Text, long replyToMsgId = 0)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvSendReq);
                bw.Write(convId);
                bw.Write(replyToMsgId);
                bw.Write((byte)kind);
                bw.Write(FixedUtf8(message ?? string.Empty, ConvConstants.MessageBytes));
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvRenameReq(long convId, string name)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvRenameReq);
                bw.Write(convId);
                bw.Write(FixedUtf8(name ?? string.Empty, ConvConstants.NameBytes));
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvDeleteReq(long convId, long msgId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvDeleteReq);
                bw.Write(convId);
                bw.Write(msgId);
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvHistoryReq(long convId, long beforeMsgId, int count, int requestId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvHistoryReq);
                bw.Write(convId);
                bw.Write(beforeMsgId);
                bw.Write(count);
                bw.Write(requestId);
                bw.Flush();
                return FinishPacket(ms);
            }
        }

        public static byte[] BuildConvReadReq(long convId, long upToMsgId)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write((ushort)0);
                bw.Write((ushort)PacketType.ConvReadReq);
                bw.Write(convId);
                bw.Write(upToMsgId);
                bw.Flush();
                return FinishPacket(ms);
            }
        }
    }

    public static partial class PacketParser
    {
        // 헤더(size, type)를 건너뛴 BinaryReader를 만든다.
        private static BinaryReader OpenBody(byte[] buffer)
        {
            var br = new BinaryReader(new MemoryStream(buffer));
            br.ReadUInt16();
            br.ReadUInt16();
            return br;
        }

        private static ConvSummaryData ReadConvSummary(BinaryReader br)
        {
            var s = new ConvSummaryData();
            s.ConvId = br.ReadInt64();
            s.Type = (ConvType)br.ReadByte();
            s.Name = Utf8FromFixed(br.ReadBytes(ConvConstants.NameBytes));
            s.MemberCount = br.ReadInt32();
            s.UnreadCount = br.ReadInt32();
            s.LastMsgId = br.ReadInt64();
            s.LastMsgTime = br.ReadInt64();
            s.LastKind = (ConvMessageKind)br.ReadByte();
            s.LastMessage = Utf8FromFixed(br.ReadBytes(ConvConstants.LastMessageBytes));
            s.MyLastReadMsgId = br.ReadInt64();
            s.PeerPublicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
            s.PeerNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
            s.PeerImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
            return s;
        }

        public static ConvOpenResData ParseConvOpenRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvOpenResData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.IsNew = br.ReadByte() != 0;
                d.Summary = ReadConvSummary(br);
                return d;
            }
        }

        //***************************************************************************
        // @brief ConvUpdatedNotify / ConvListItemRes 공용 — 요약 하나만 담는다.
        //***************************************************************************
        public static ConvSummaryData ParseConvSummaryPacket(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
                return ReadConvSummary(br);
        }

        //***************************************************************************
        // @brief ConvInviteRes / ConvLeaveRes — success + reason + convId.
        //***************************************************************************
        public static ConvResultData ParseConvResult(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvResultData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.ConvId = br.ReadInt64();
                return d;
            }
        }

        //***************************************************************************
        // @brief ConvSendRes — success + reason + convId + msgId.
        //***************************************************************************
        public static ConvResultData ParseConvSendRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvResultData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.ConvId = br.ReadInt64();
                d.MsgId = br.ReadInt64();
                return d;
            }
        }

        public static ConvListEndData ParseConvListEndRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvListEndData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.TotalCount = br.ReadInt32();
                return d;
            }
        }

        //***************************************************************************
        // @brief ConvMessageNotify / ConvHistoryItemRes 공용 — 메시지 한 건.
        //***************************************************************************
        public static ConvMessageData ParseConvMessage(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvMessageData();
                d.ConvId = br.ReadInt64();
                d.MsgId = br.ReadInt64();
                d.Kind = (ConvMessageKind)br.ReadByte();
                d.SenderPublicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                d.SenderNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                d.SenderImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                d.Timestamp = br.ReadInt64();
                d.Message = Utf8FromFixed(br.ReadBytes(ConvConstants.MessageBytes));
                d.ReplyToMsgId = br.ReadInt64();
                d.ReplyKind = (ConvMessageKind)br.ReadByte();
                d.ReplySenderNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                d.ReplyPreview = Utf8FromFixed(br.ReadBytes(ConvConstants.ReplyPreviewBytes));
                return d;
            }
        }

        // ConvDeleteRes는 ConvSendRes와 같은 모양(success, reason, convId, msgId)이다.
        public static ConvResultData ParseConvDeleteRes(byte[] buffer)
        {
            return ParseConvSendRes(buffer);
        }

        public static ConvRenamedData ParseConvRenamedNotify(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvRenamedData();
                d.ConvId = br.ReadInt64();
                d.Name = Utf8FromFixed(br.ReadBytes(ConvConstants.NameBytes));
                return d;
            }
        }

        public static ConvDeletedData ParseConvDeletedNotify(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvDeletedData();
                d.ConvId = br.ReadInt64();
                d.MsgId = br.ReadInt64();
                return d;
            }
        }

        public static ConvHistoryEndData ParseConvHistoryEndRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvHistoryEndData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.HasMore = br.ReadByte() != 0;
                d.ConvId = br.ReadInt64();
                d.Count = br.ReadInt32();
                d.RequestId = br.ReadInt32();
                return d;
            }
        }

        public static ConvReadNotifyData ParseConvReadNotify(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvReadNotifyData();
                d.ConvId = br.ReadInt64();
                d.ReaderPublicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                d.UpToMsgId = br.ReadInt64();
                return d;
            }
        }

        public static ConvMemberData ParseConvMemberItemRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvMemberData();
                d.ConvId = br.ReadInt64();
                d.PublicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                d.Nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                d.ProfileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                d.LastReadMsgId = br.ReadInt64();
                d.Online = br.ReadByte() != 0;
                return d;
            }
        }

        public static ConvMembersEndData ParseConvMembersEndRes(byte[] buffer)
        {
            using (var br = OpenBody(buffer))
            {
                var d = new ConvMembersEndData();
                d.Success = br.ReadByte() != 0;
                d.Reason = (ConvResult)br.ReadByte();
                d.ConvId = br.ReadInt64();
                d.TotalCount = br.ReadInt32();
                return d;
            }
        }
    }
}