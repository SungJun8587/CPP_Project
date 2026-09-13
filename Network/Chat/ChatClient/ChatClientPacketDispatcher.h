
//***************************************************************************
// ChatClientPacketDispatcher.h : 공용 CPacketDispatcher 위에 얹힌 채팅 클라이언트 전용 얇은 어댑터.
//
//***************************************************************************

#ifndef UC_CHATCLIENTPACKETDISPATCHER_H
#define UC_CHATCLIENTPACKETDISPATCHER_H

#include "ChatPacket.h"
#include <Network/PacketDispatcher.h>

class CChatClientSession;

using ChatClientPacketHandler = void(*)(CChatClientSession& session, const PacketHeader* header);

//***************************************************************************
// @brief ChatClientPacketHandler를 공용 PacketHandler(void* 기반) 시그니처로
//        변환하는 컴파일 타임 트램폴린. 서버용 ChatPacketTrampoline과 동일한
//        이유(캡처 없는 순수 함수 포인터가 필요)로 비타입 템플릿 매개변수를 쓴다.
//***************************************************************************
template<ChatClientPacketHandler Handler>
void ChatClientPacketTrampoline(void* context, const PacketHeader* header)
{
	Handler(*static_cast<CChatClientSession*>(context), header);
}

//***************************************************************************
// @class CChatClientPacketDispatcher
// @brief CPacketDispatcher(공용)를 그대로 감싼 채팅 클라이언트 전용 파사드.
//***************************************************************************
class CChatClientPacketDispatcher
{
public:
	static EChatDispatchResult Dispatch(CChatClientSession& session, const PacketHeader* header, size_t bufferSize)
	{
		const EPacketDispatchResult result = CPacketDispatcher::Dispatch(&session, header, bufferSize);
		return static_cast<EChatDispatchResult>(result);
	}
};

//***************************************************************************
// @brief 클라이언트 핸들러 등록 매크로. 기존과 100% 동일한 사용법. 사용 예:
//   REGISTER_CHAT_CLIENT_PACKET_HANDLER(LoginRes, LoginResPacket, HandleLoginRes);
//***************************************************************************
#define UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME_INNER(line) sChatClientPacketRegistrar_##line
#define UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME(line) UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME_INNER(line)

#define REGISTER_CHAT_CLIENT_PACKET_HANDLER(EnumName, PacketType, HandlerFunc) \
	static PacketRegistrar UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME(__LINE__)( \
		static_cast<uint16>(EChatPacketType::EnumName), sizeof(PacketType), \
		&ChatClientPacketTrampoline<&HandlerFunc>)

#endif // ndef UC_CHATCLIENTPACKETDISPATCHER_H