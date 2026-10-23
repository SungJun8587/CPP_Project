
//***************************************************************************
// RoomProtocol.cs : 방(Room) 기능 패킷 정의 + 직렬화/역직렬화.
//
// 서버 ChatPacket.h / ChatPacketTypes.h의 방 관련 패킷(입장/퇴장/생성/삭제/이름 변경/
// 목록/멤버 목록/대표 이미지/인원 수 알림)과 바이트 레이아웃이 1:1로 일치해야 한다.
// PacketBuilder/PacketParser는 ChatProtocol.cs와 같은 클래스의 partial 정의이며,
// 이 파일은 방 기능 부분만 담는다. PacketType 열거형과 공용 도우미(FixedUtf8,
// Utf8FromFixed 등)는 ChatProtocol.cs에 있다.
//***************************************************************************

using System;
using System.IO;
using System.Text;

namespace ChatApp
{

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

    // ListRoomsReqPacket::scope — 서버 ChatPacketTypes.h::ERoomListScope와
    // 정확히 같은 값을 유지해야 한다.
    public enum RoomListScope : byte
    {
        All = 0,        // 존재하는 모든 방(최신 생성순)
        Joined = 1,     // 내가 입장한 적이 있는 방(최근 입장순)
    }

    // RoomMemberListReqPacket::scope — 서버 ChatPacketTypes.h::ERoomMemberListScope와
    // 정확히 같은 값을 유지해야 한다.
    public enum RoomMemberListScope : byte
    {
        Present = 0,    // 지금 그 방에 실제로 접속해 있는 사람만(전원 온라인)
        History = 1,    // 그 방에 입장한 적이 있는 전체 유저(접속 상태 별도 표시)
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
        public int RequestId;      // 이 항목이 속한 목록 요청의 식별자(늦게 도착한 응답 걸러내기용)
        public int RoomId;
        public string Name;
        public string OwnerNickname;
        public int UserCount;          // 지금 그 방에 접속해 있는 인원수
        public int TotalMemberCount;   // [추가] 그 방에 입장한 적이 있는 전체 유저 수(접속 여부 무관)
        public string ImageUrl;    // [추가] 방 프로필 이미지. 비어있으면 기본 이미지
    }

    //***************************************************************************
    // @brief 방 목록 한 페이지 전송 완료.
    // @details TotalCount는 이 페이지의 항목 수가 아니라 조건(범위/검색어)에
    //          맞는 전체 방 수다. Page/PageSize는 서버가 보정한 뒤 실제로
    //          적용한 값이다.
    //***************************************************************************
    public class ListRoomsEndResData
    {
        public int RequestId;
        public RoomListScope Scope;
        public int TotalCount;
        public int Page;
        public int PageSize;
    }

    //***************************************************************************
    // @brief 방 멤버 목록 항목 단건.
    // @details PublicId는 로그인 응답으로 이미 받아 저장해둔 자신의 PublicId와
    //          비교해 "나 자신"을 구분하는 데 쓸 수 있다.
    //***************************************************************************
    public class RoomMemberItemData
    {
        public int RequestId;
        public byte[] PublicId;
        public string Nickname;
        public string ProfileImageUrl;
        public bool Online; // 이 유저가 "지금 이 방에 있는지"가 아니라 "지금 서버 어딘가에 로그인해 있는지"
    }

    //***************************************************************************
    // @brief 방 멤버(입장 이력) 목록 한 페이지 전송 완료.
    // @details TotalCount는 이 페이지의 항목 수가 아니라 이 방에 입장 이력이
    //          있는 전체 유저 수다. Page/PageSize는 서버가 보정한 뒤 실제로
    //          적용한 값이다.
    //***************************************************************************
    public class RoomMemberListEndResData
    {
        public int RequestId;
        public int RoomId;
        public RoomMemberListScope Scope;
        public int TotalCount;
        public int Page;
        public int PageSize;
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

        // [추가] 성공했고 로비가 아니면 서버가 이 방의 지금 이름/방장/
        // 이미지를 같이 실어 보낸다(RoomEnterHandler.cpp가
        // CChatServerMain::RequestGetRoomInfo()로 채움) — 별도로 방 목록
        // 캐시를 참조하거나 그 캐시가 준비될 때까지 기다릴 필요가 없다.
        // 로비 입장이거나 실패면 전부 빈 문자열.
        public string RoomName;
        public string RoomOwnerNickname;
        public string RoomImageUrl;
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

    public static partial class PacketBuilder
    {
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
        // @brief 방 목록 한 페이지 조회 요청. 로그인한 사용자면 누구나 조회 가능.
        // @param requestId 응답(Item/End)이 그대로 되돌려주는 요청 식별자. 이미
        //        다른 요청으로 넘어간 뒤 늦게 도착한 응답을 걸러내는 데 쓴다
        // @param scope All=전체 방, Joined=내가 참여한 방
        // @param page 0부터 시작하는 페이지 번호(서버가 범위를 넘으면 마지막
        //        페이지로 보정한다)
        // @param pageSize 한 페이지 항목 수(서버가 1~50으로 보정한다)
        // @param keyword 방 이름 검색어. 비어있으면 검색 조건 없음
        //***************************************************************************
        public static byte[] BuildListRoomsReq(int requestId, RoomListScope scope, int page, int pageSize, string keyword)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int) + sizeof(byte) + sizeof(int) + sizeof(int) + ProtocolConstants.RoomNameBytes);
                bw.Write(size);
                bw.Write((ushort)PacketType.ListRoomsReq);
                bw.Write(requestId);
                bw.Write((byte)scope);
                bw.Write(page);
                bw.Write(pageSize);
                bw.Write(FixedUtf8(keyword ?? string.Empty, ProtocolConstants.RoomNameBytes));
                return ms.ToArray();
            }
        }

        //***************************************************************************
        // @brief 지금 자신이 있는 방(로비 포함)에 입장한 적이 있는 유저 목록
        //        (전체 이력)의 한 페이지 조회 요청.
        // @param requestId 응답(Item/End)이 그대로 되돌려주는 요청 식별자
        // @param roomId 지금 자신이 있는 방(RoomEnterRes 등으로 이미 알고 있는 값)
        // @param scope Present=지금 접속 중인 사람만, History=입장 이력 전체
        // @param page 0부터 시작하는 페이지 번호(서버가 범위를 넘으면 마지막
        //        페이지로 보정한다)
        // @param pageSize 한 페이지 항목 수(서버가 1~50으로 보정한다)
        //***************************************************************************
        public static byte[] BuildRoomMemberListReq(int requestId, int roomId, RoomMemberListScope scope, int page, int pageSize)
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                ushort size = (ushort)(ProtocolConstants.HeaderBytes + sizeof(int) + sizeof(int) + sizeof(byte) + sizeof(int) + sizeof(int));
                bw.Write(size);
                bw.Write((ushort)PacketType.RoomMemberListReq);
                bw.Write(requestId);
                bw.Write(roomId);
                bw.Write((byte)scope);
                bw.Write(page);
                bw.Write(pageSize);
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
    }

    public static partial class PacketParser
    {
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
                int requestId = br.ReadInt32();
                int roomId = br.ReadInt32();
                string name = Utf8FromFixed(br.ReadBytes(ProtocolConstants.RoomNameBytes));
                string ownerNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                int userCount = br.ReadInt32();
                int totalMemberCount = br.ReadInt32();
                string imageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new RoomListItemData { RequestId = requestId, RoomId = roomId, Name = name, OwnerNickname = ownerNickname, UserCount = userCount, TotalMemberCount = totalMemberCount, ImageUrl = imageUrl };
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
        // @brief 방 목록 한 페이지 전송 완료 파싱.
        //***************************************************************************
        public static ListRoomsEndResData ParseListRoomsEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int requestId = br.ReadInt32();
                var scope = (RoomListScope)br.ReadByte();
                int totalCount = br.ReadInt32();
                int page = br.ReadInt32();
                int pageSize = br.ReadInt32();
                return new ListRoomsEndResData { RequestId = requestId, Scope = scope, TotalCount = totalCount, Page = page, PageSize = pageSize };
            }
        }

        //***************************************************************************
        // @brief 방 멤버(입장 이력) 목록 항목 파싱.
        //***************************************************************************
        public static RoomMemberItemData ParseRoomMemberListItemRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int requestId = br.ReadInt32();
                byte[] publicId = br.ReadBytes(ProtocolConstants.PublicIdBytes);
                string nickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string profileImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                bool online = br.ReadByte() != 0;
                return new RoomMemberItemData { RequestId = requestId, PublicId = publicId, Nickname = nickname, ProfileImageUrl = profileImageUrl, Online = online };
            }
        }

        //***************************************************************************
        // @brief 방 멤버(입장 이력) 목록 한 페이지 전송 완료 파싱.
        //***************************************************************************
        public static RoomMemberListEndResData ParseRoomMemberListEndRes(byte[] buffer)
        {
            using (var br = new BinaryReader(new MemoryStream(buffer)))
            {
                br.ReadUInt16();
                br.ReadUInt16();
                int requestId = br.ReadInt32();
                int roomId = br.ReadInt32();
                var scope = (RoomMemberListScope)br.ReadByte();
                int totalCount = br.ReadInt32();
                int page = br.ReadInt32();
                int pageSize = br.ReadInt32();
                return new RoomMemberListEndResData { RequestId = requestId, RoomId = roomId, Scope = scope, TotalCount = totalCount, Page = page, PageSize = pageSize };
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
                bool success = br.ReadByte() != 0;
                RoomResult reason = (RoomResult)br.ReadByte();
                int roomId = br.ReadInt32();
                int roomUserCount = br.ReadInt32();
                string roomName = Utf8FromFixed(br.ReadBytes(ProtocolConstants.RoomNameBytes));
                string roomOwnerNickname = Utf8FromFixed(br.ReadBytes(ProtocolConstants.NicknameBytes));
                string roomImageUrl = Utf8FromFixed(br.ReadBytes(ProtocolConstants.ProfileImageUrlBytes));
                return new RoomEnterResPacketData
                {
                    Success = success,
                    Reason = reason,
                    RoomId = roomId,
                    RoomUserCount = roomUserCount,
                    RoomName = roomName,
                    RoomOwnerNickname = roomOwnerNickname,
                    RoomImageUrl = roomImageUrl,
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
    }
}