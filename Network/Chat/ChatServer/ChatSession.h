
//***************************************************************************
// ChatSession.h : interface for the CChatSession class.
//
//***************************************************************************

#ifndef UC_CHATSESSION_H
#define UC_CHATSESSION_H

#include <Network/IOCP/IocpSession.h>
#include "ChatPacket.h"

#include <string>
#include <array>

class CChatServerMain;

//***************************************************************************
// @class CChatSession
// @brief CIocpSession을 상속받는 채팅 서버 전용 세션.
// @details
// 로그인 상태(_loggedIn/_publicId/_nickname)는 이 세션 객체 안에서만 관리하고,
// 실제 Redis 반영(등록/삭제)은 CChatServerMain에 위임합니다 — 세션은 프로토콜
// 처리와 자기 상태만 알고, 인프라(Redis) 연동은 서버 파사드가 전담하는 구조입니다.
//
// [설계 변경 — 아이덴티티/표시명 분리] users.nickname이 더 이상 PRIMARY
// KEY가 아니게 되면서(create_chat_db.sql 참고), 세션의 "진짜 정체성"과
// "화면에 보이는 이름"이 서로 다른 값이 됐다:
//   - _publicId : 로그인 성공 시 서버가 확정하는 안정 식별자. 닉네임이
//     바뀌어도 절대 안 바뀐다 — Redis 키/DB 조회는 전부 이 값 기준.
//   - _nickname : 표시용. ChatChangeNicknameHandler.cpp가 언제든 바꿀 수
//     있고, 그래도 _publicId/Redis 키/세션 자체는 전혀 영향받지 않는다.
//***************************************************************************
class CChatSession : public CIocpSession
{
public:
	explicit CChatSession(CChatServerMain* server);
	virtual ~CChatSession() = default;

protected:
	// CIocpSession의 상위 콘텐츠 레이어 훅 오버라이드
	virtual void	OnConnected() override;
	virtual void	OnDisconnected() override;
	virtual int32	OnRecv(BYTE* buffer, int32 len) override;

public:
	//***************************************************************************
	// @brief 현재 로그인 상태를 반환합니다.
	//***************************************************************************
	bool IsLoggedIn() const { return _loggedIn; }

	//***************************************************************************
	// @brief 로그인한 계정의 안정 식별자를 반환합니다(로그인 전이면 전부 0).
	// @details 닉네임 변경과 무관하게 고정 — Redis 키/DB 조회는 이 값을 쓴다.
	//***************************************************************************
	const std::array<BYTE, kPublicIdBytes>& GetPublicId() const { return _publicId; }

	//***************************************************************************
	// @brief 로그인한 유저의 표시용 닉네임을 반환합니다(로그인 전이면 빈 문자열).
	// @details ChatChangeNicknameHandler.cpp가 성공적으로 바꾸면 이 값만
	//          갱신된다 — GetPublicId()는 영향받지 않는다.
	//***************************************************************************
	const std::string& GetNickname() const { return _nickname; }

	//***************************************************************************
	// @brief 이 세션이 속한 채팅 서버를 반환합니다.
	//***************************************************************************
	CChatServerMain* GetServer() const { return _server; }

	//***************************************************************************
	// @brief 로그인 완료 상태를 세션에 반영합니다.
	// @details [설계 노트] 자체 등록형 패킷 핸들러가 별도 파일의 자유 함수로
	//          분리되면서 더 이상 CChatSession의 private 멤버에 직접 접근할 수
	//          없다 — 그 대가로 캡슐화를 "핸들러가 필요로 하는 최소한의 public
	//          API"로 한 단계 완화했다. 이 메서드는 로그인 처리 핸들러
	//          (ChatLoginHandler.cpp)만 호출하는 것을 의도한 좁은 용도의 API이며,
	//          컴파일러가 강제하지는 못하므로 컨벤션으로 지킨다.
	//***************************************************************************
	void MarkLoggedIn(const std::array<BYTE, kPublicIdBytes>& publicId, std::string nickname)
	{
		_publicId = publicId;
		_nickname = std::move(nickname);
		_loggedIn = true;
	}

	//***************************************************************************
	// @brief 닉네임 변경 성공 후 세션의 표시용 닉네임만 갱신합니다.
	// @details MarkLoggedIn()과 동일한 좁은 용도 API — ChatChangeNicknameHandler.cpp만
	//          호출하는 것을 의도한다. _publicId/로그인 상태는 건드리지
	//          않는다(닉네임 변경은 계정 식별자에 영향을 주지 않으므로).
	//***************************************************************************
	void UpdateNickname(std::string newNickname) { _nickname = std::move(newNickname); }

	//***************************************************************************
	// @brief 현재 있는 위치(로비=kLobbyRoomId 또는 특정 룸 ID)를 반환합니다.
	// @details 로그인 전이거나 아직 로비 배정도 안 된 극히 짧은 순간에는 -1
	//          (어디에도 속하지 않음)일 수 있다 — CChatServerMain::LeaveCurrentRoom()이
	//          이 값을 보고 "정리할 방이 없음"을 판단한다.
	//***************************************************************************
	int32 GetRoomId() const { return _roomId; }

	//***************************************************************************
	// @brief 현재 위치를 갱신합니다.
	// @details [설계 노트] MarkLoggedIn()과 동일한 좁은 용도 API — CChatServerMain::MoveToRoom()만
	//          호출하는 것을 의도한다. 세션 스스로 방을 옮기는 게 아니라,
	//          방 멤버십을 총괄하는 CChatServerMain의 결정을 세션에 반영만
	//          하는 역할이다(실제 멤버십 목록 갱신은 CChatServerMain 쪽 책임).
	//***************************************************************************
	void SetRoomId(int32 roomId) { _roomId = roomId; }

private:
	void	HandlePacket(const PacketHeader* header);

private:
	CChatServerMain* _server = nullptr;	// 뒤로 참조 — 서버 소유 세션이라 세션보다 오래 살아있음이 보장됨
	std::array<BYTE, kPublicIdBytes>	_publicId{};	// 로그인된 계정의 안정 식별자(로그인 전엔 전부 0)
	std::string		_nickname;						// 표시용 닉네임(변경 가능)
	bool			_loggedIn = false;
	int32			_roomId = -1;					// 현재 위치. -1=아직 미배정, 0=로비, 1~kMaxRoomId=특정 룸
};

#endif // ndef UC_CHATSESSION_H