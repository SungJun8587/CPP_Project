
//***************************************************************************
// ChatClientPacketDispatcher.h : interface for the CChatClientPacketDispatcher class.
//
//***************************************************************************

#ifndef UC_CHATCLIENTPACKETDISPATCHER_H
#define UC_CHATCLIENTPACKETDISPATCHER_H

#include "ChatPacket.h"		// EChatDispatchResult 포함

#include <unordered_map>

class CChatClientSession;

using ChatClientPacketHandler = void(*)(CChatClientSession& session, const PacketHeader* header);

//***************************************************************************
// @class CChatClientPacketDispatcher
// @brief 클라이언트 수신 패킷 타입 → 핸들러 매핑을 전역적으로 관리하는
//        레지스트리. 설계/동시성/링킹 관련 세부사항은 서버용
//        CChatPacketDispatcher와 동일하므로 그쪽 주석 참고.
//***************************************************************************
class CChatClientPacketDispatcher
{
public:
	static void Register(EChatPacketType type, uint16 minSize, ChatClientPacketHandler handler);
	static EChatDispatchResult Dispatch(CChatClientSession& session, const PacketHeader* header);

private:
	struct Entry
	{
		uint16					minSize;
		ChatClientPacketHandler	handler;
	};

	static std::unordered_map<uint16, Entry>& GetTable();
};

//***************************************************************************
// @brief 클라이언트 패킷 핸들러 모듈이 자신을 등록하기 위한 RAII 헬퍼.
//***************************************************************************
struct ChatClientPacketRegistrar
{
	ChatClientPacketRegistrar(EChatPacketType type, uint16 minSize, ChatClientPacketHandler handler)
	{
		CChatClientPacketDispatcher::Register(type, minSize, handler);
	}
};

//***************************************************************************
// @brief 클라이언트 핸들러 등록 매크로. 사용 예:
//   REGISTER_CHAT_CLIENT_PACKET_HANDLER(LoginRes, LoginResPacket, HandleLoginRes);
//***************************************************************************
#define UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME_INNER(line) sChatClientPacketRegistrar_##line
#define UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME(line) UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME_INNER(line)

#define REGISTER_CHAT_CLIENT_PACKET_HANDLER(EnumName, PacketType, HandlerFunc) \
	static ChatClientPacketRegistrar UC_CHAT_CLIENT_PACKET_REGISTRAR_NAME(__LINE__)( \
		EChatPacketType::EnumName, sizeof(PacketType), &HandlerFunc)

#endif // ndef UC_CHATCLIENTPACKETDISPATCHER_H