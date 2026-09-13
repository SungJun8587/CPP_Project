
//***************************************************************************
// ChatPacketDispatcher.h : 공용 CPacketDispatcher 위에 얹힌 채팅 서버 전용 얇은 어댑터.
//
//***************************************************************************

#ifndef UC_CHATPACKETDISPATCHER_H
#define UC_CHATPACKETDISPATCHER_H

#include "ChatPacket.h"
#include <Network/PacketDispatcher.h>

class CChatSession;

//***************************************************************************
// @brief 패킷 핸들러 함수 시그니처(채팅 서버 전용 — CChatSession&로 받음).
// @details 실제 등록/조회는 전부 공용 CPacketDispatcher(void* context 기반)가
//          담당한다 — 이 시그니처는 각 핸들러 .cpp가 void* 캐스팅을 직접
//          쓰지 않고 타입 안전하게 CChatSession&을 받을 수 있게 해주는
//          겉면일 뿐이다. 순수 함수 포인터로 고정 — 캡처가 필요한 상태
//          있는 핸들러는 다루지 않는다.
//***************************************************************************
using ChatPacketHandler = void(*)(CChatSession& session, const PacketHeader* header);

//***************************************************************************
// @brief ChatPacketHandler(CChatSession& 기반)를 공용 PacketHandler(void*
//        기반) 시그니처로 변환하는 컴파일 타임 트램폴린.
// @details Handler를 비타입 템플릿 매개변수로 받는다 — 그래서 이 함수는
//          런타임에 "어떤 핸들러를 호출할지"를 캡처해서 들고 있는 게
//          아니라, 컴파일 타임에 그 정보가 이미 확정된 "고정된 함수"가
//          된다. 이 덕분에 캡처 있는 람다 없이도(PacketHandler가 순수
//          함수 포인터라 캡처된 상태를 담을 수 없다) 각 등록마다 서로
//          다른 트램폴린 인스턴스(ChatPacketTrampoline<&HandleLoginReq>,
//          ChatPacketTrampoline<&HandleChat> 등)가 자동으로 만들어진다.
//***************************************************************************
template<ChatPacketHandler Handler>
void ChatPacketTrampoline(void* context, const PacketHeader* header)
{
	Handler(*static_cast<CChatSession*>(context), header);
}

//***************************************************************************
// @class CChatPacketDispatcher
// @brief CPacketDispatcher(공용)를 그대로 감싼 채팅 서버 전용 파사드.
// @details 실제 테이블/등록/조회 로직은 전부 CPacketDispatcher가 갖고
//          있다 — 이 클래스는 (1) CChatSession& <-> void* 변환(트램폴린을
//          통해), (2) EChatPacketType -> uint16 변환, (3)
//          EPacketDispatchResult -> EChatDispatchResult 변환만 담당한다.
//          기존 호출부(ChatSession.cpp, 각 핸들러 .cpp의
//          REGISTER_CHAT_PACKET_HANDLER)는 이번 리팩토링으로 한 줄도
//          바뀌지 않는다 — 공개 인터페이스(매크로 사용법 포함)가
//          그대로이기 때문이다.
//***************************************************************************
class CChatPacketDispatcher
{
public:
	//***************************************************************************
	// @brief 패킷을 등록된 핸들러로 디스패치합니다.
	// @param bufferSize header가 가리키는 실제 수신 버퍼에 지금 남아있는
	//        바이트 수 — CPacketDispatcher::Dispatch()로 그대로 전달돼
	//        방어적으로 한 번 더 검증된다(CChatSession::OnRecv()가 이미
	//        프레이밍을 검증하지만, 공용 컴포넌트가 된 이상 호출부를
	//        전적으로 믿지 않는 편이 안전하다).
	//***************************************************************************
	static EChatDispatchResult Dispatch(CChatSession& session, const PacketHeader* header, size_t bufferSize)
	{
		const EPacketDispatchResult result = CPacketDispatcher::Dispatch(&session, header, bufferSize);
		return static_cast<EChatDispatchResult>(result);
	}
};

//***************************************************************************
// @brief 핸들러 등록 매크로. 기존과 100% 동일한 사용법 — 이번 리팩토링으로
//        바뀌는 건 매크로 내부 구현뿐이다(트램폴린을 거쳐 공용
//        PacketRegistrar/CPacketDispatcher로 등록됨).
//
// 사용 예 (파일 스코프에 배치):
//   REGISTER_CHAT_PACKET_HANDLER(LoginReq, LoginReqPacket, HandleLoginReq);
//***************************************************************************
#define UC_CHAT_PACKET_REGISTRAR_NAME_INNER(line) sChatPacketRegistrar_##line
#define UC_CHAT_PACKET_REGISTRAR_NAME(line) UC_CHAT_PACKET_REGISTRAR_NAME_INNER(line)

#define REGISTER_CHAT_PACKET_HANDLER(EnumName, PacketType, HandlerFunc) \
	static PacketRegistrar UC_CHAT_PACKET_REGISTRAR_NAME(__LINE__)( \
		static_cast<uint16>(EChatPacketType::EnumName), sizeof(PacketType), \
		&ChatPacketTrampoline<&HandlerFunc>)

#endif // ndef UC_CHATPACKETDISPATCHER_H