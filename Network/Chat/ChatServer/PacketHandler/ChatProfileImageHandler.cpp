
//***************************************************************************
// ChatProfileImageHandler.cpp : 프로필 이미지 패킷 핸들러 — 갤러리 목록/대표 지정/삭제/URL 등록
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatServerMain.h"
#include "ChatPacketDispatcher.h"
#include <cstring>

namespace
{
	//***************************************************************************
	// @brief 갤러리 목록 조회. 개수가 가변이라 항목마다 ListProfileImagesItemRes를
	//        하나씩 순차 전송하고, 마지막에 ListProfileImagesEndRes로 마무리한다.
	//***************************************************************************
	void HandleListProfileImagesReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestListProfileImages(sessionRef, publicId,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, const std::vector<SProfileImageEntry>& images)
				{
					// [참고] result가 DbError여도 그냥 "항목 0개"로 응답한다 —
					// 별도 실패 응답 타입을 안 만든 단순화. 클라이언트 입장에서
					// "갤러리가 비어 있다"와 "조회에 실패했다"를 구분 못 하는 게
					// 이상적이진 않지만, 데모 범위에서 감수했다.
					if( result == ELoginResult::Ok )
					{
						for( const auto& entry : images )
						{
							ListProfileImagesItemResPacket itemRes{};
							InitPacket(itemRes, EChatPacketType::ListProfileImagesItemRes);
							itemRes.imageId = entry.imageId;
							itemRes.isActive = entry.isActive ? 1 : 0;

							CopyStrUtf8(itemRes.imageRef, entry.imageRef);

							SendPacket(session, itemRes);
						}
					}

					ListProfileImagesEndResPacket endRes{};
					InitPacket(endRes, EChatPacketType::ListProfileImagesEndRes);
					endRes.totalCount = (result == ELoginResult::Ok) ? static_cast<int32>(images.size()) : 0;
					SendPacket(session, endRes);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(ListProfileImagesReq, ListProfileImagesReqPacket, HandleListProfileImagesReq);

namespace
{
	//***************************************************************************
	// @brief 갤러리에 이미 있는 이미지 중 하나를 대표로 지정. 성공하면
	//        세션의 표시용 프로필 이미지 값도 곧바로 갱신한다.
	//***************************************************************************
	void HandleSelectProfileImageReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return;

		const SelectProfileImageReqPacket* packet = reinterpret_cast<const SelectProfileImageReqPacket*>(header);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();
		const int64 imageId = packet->imageId;

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSelectProfileImage(sessionRef, publicId, imageId,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, int64 /*imageId*/, const std::string& selectedImageRef)
				{
					if( result == ELoginResult::Ok )
					{
						// DB의 image_ref는 상대 경로로 저장돼 있을 수 있으므로, 세션에는 로그인/URL 설정
						// 경로와 같은 "표시용 URL"을 넣는다(이미 완전한 URL이면 그대로 반환됨).
						CChatServerMain* srv = session->GetServer();
						session->UpdateProfileImageUrl(srv != nullptr ? srv->ToDisplayImageUrl(selectedImageRef) : selectedImageRef);

						// 접속 중인 친구들에게 바뀐 대표 이미지를 알린다(세션 값 갱신 직후).
						if( srv != nullptr )
							srv->NotifyFriendProfileChanged(session);
					}

					SelectProfileImageResPacket res{};
					InitPacket(res, EChatPacketType::SelectProfileImageRes);
					res.success = (result == ELoginResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(SelectProfileImageReq, SelectProfileImageReqPacket, HandleSelectProfileImageReq);

namespace
{
	//***************************************************************************
	// @brief 갤러리에서 이미지 하나를 삭제. 지운 이미지가 대표였다면 세션의
	//        표시용 프로필 이미지 값을 비운다(다른 이미지로 자동 승격하지
	//        않음 — 사용자가 남은 이미지 중 새로 골라야 한다).
	//***************************************************************************
	void HandleDeleteProfileImageReq(CChatSession& session, const PacketHeader* header)
	{
		const DeleteProfileImageReqPacket* packet = reinterpret_cast<const DeleteProfileImageReqPacket*>(header);

		CChatServerMain* server = GetLoggedInServer(session);
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();
		const int64 imageId = packet->imageId;

		auto sessionRef = GetSessionRef(session);
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestDeleteProfileImage(sessionRef, publicId, imageId,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, int64 /*imageId*/, bool wasActive, const std::string& /*deletedImageRef*/)
				{
					if( result == ELoginResult::Ok && wasActive )
					{
						session->UpdateProfileImageUrl(std::string());

						// 대표 이미지가 사라졌으므로(기본 이미지로 돌아감) 접속 중인 친구들에게 알린다.
						if( CChatServerMain* srv = session->GetServer() )
							srv->NotifyFriendProfileChanged(session);
					}

					DeleteProfileImageResPacket res{};
					InitPacket(res, EChatPacketType::DeleteProfileImageRes);
					res.success = (result == ELoginResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(DeleteProfileImageReq, DeleteProfileImageReqPacket, HandleDeleteProfileImageReq);

namespace
{
	//***************************************************************************
	// @brief 로그인된 세션의 프로필 이미지 URL 변경 요청 처리.
	// @details ChatAccountHandler.cpp와 동일한 구조 — RequestSetProfileImageUrl()로
	//          DB 비동기 워커에 위임한 뒤, 성공 시 세션의 값만 갱신한다.
	//***************************************************************************
	void HandleSetProfileImageUrlReq(CChatSession& session, const PacketHeader* header)
	{
		if( !session.IsLoggedIn() )
			return; // 로그인 전 요청은 조용히 무시

		const SetProfileImageUrlReqPacket* packet = reinterpret_cast<const SetProfileImageUrlReqPacket*>(header);

		// url이 NUL로 안 끝났을 가능성을 방어(경계값)하기 위해 별도 버퍼에
		// 복사 후 문자열화. 빈 문자열이면 "프로필 이미지 해제" 의미이므로
		// (닉네임과 달리) 빈 값이어도 그냥 요청을 진행한다.
		char safeBuf[sizeof(packet->url) + 1] = {};
		::memcpy(safeBuf, packet->url, sizeof(packet->url));
		std::string newUrl(safeBuf);

		CChatServerMain* server = session.GetServer();
		if( server == nullptr )
			return;

		const std::array<BYTE, kPublicIdBytes> publicId = session.GetPublicId();

		auto sessionRef = std::static_pointer_cast<CChatSession>(session.shared_from_this());
		std::weak_ptr<CChatSession> sessionWeak = sessionRef;

		server->RequestSetProfileImageUrl(sessionRef, publicId, newUrl,
			BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ELoginResult result, const std::array<BYTE, kPublicIdBytes>& /*publicId*/,
				const std::string& newUrl, int64 newImageId)
				{
					SetProfileImageUrlResPacket res{};
					InitPacket(res, EChatPacketType::SetProfileImageUrlRes);
					res.success = (result == ELoginResult::Ok) ? 1 : 0;
					res.reason = static_cast<uint8>(result);
					res.imageId = newImageId;

					if( result == ELoginResult::Ok )
					{
						session->UpdateProfileImageUrl(newUrl);

						// 접속 중인 친구들에게 바뀐 프로필 이미지를 알린다(세션 값 갱신 직후).
						if( CChatServerMain* srv = session->GetServer() )
							srv->NotifyFriendProfileChanged(session);
					}

					SendPacket(session, res);
				}));
	}
}

REGISTER_CHAT_PACKET_HANDLER(SetProfileImageUrlReq, SetProfileImageUrlReqPacket, HandleSetProfileImageUrlReq);