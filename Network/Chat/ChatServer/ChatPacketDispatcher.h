
//***************************************************************************
// ChatPacketDispatcher.h : interface for the CChatPacketDispatcher class.
//
//***************************************************************************

#ifndef UC_CHATPACKETDISPATCHER_H
#define UC_CHATPACKETDISPATCHER_H

#include "ChatPacket.h"

#include <unordered_map>

class CChatSession;

//***************************************************************************
// @brief 패킷 핸들러 함수 시그니처. std::function이 아닌 순수 함수 포인터로
//        고정 — 등록/조회 양쪽 모두 캡처가 필요 없는 상태 없는(stateless)
//        핸들러만 다루면 충분하고, 그편이 std::function보다 가볍다.
//***************************************************************************
using ChatPacketHandler = void(*)(CChatSession& session, const PacketHeader* header);

//***************************************************************************
// @class CChatPacketDispatcher
// @brief 패킷 타입 → 핸들러 매핑을 전역적으로 관리하는 레지스트리.
// @details
// 각 패킷 핸들러 모듈(.cpp 파일 단위)이 정적 초기화 시점(main() 진입 전)에
// REGISTER_CHAT_PACKET_HANDLER 매크로를 통해 스스로 Register()를 호출해
// 자신을 등록한다 — 이 클래스(중앙 파일)는 새 패킷이 추가돼도 전혀 수정할
// 필요가 없다.
//
// 스레드 안전성: 정적 초기화는 프로그램 시작 시 단일 스레드에서 완료되고,
// 그 이후(서버가 실제로 패킷을 받기 시작한 시점)엔 테이블이 읽기 전용이
// 된다고 가정한다 — 그래서 Register()/Dispatch() 어디에도 락이 없다.
// 런타임 중 동적 등록/해제(플러그인 핫로드 등)는 이 설계 범위 밖이다.
//
// [정적 라이브러리 링킹 주의] 이 핸들러 .cpp 파일들이 실행 파일에 직접
// 컴파일되지 않고 정적 라이브러리(.lib/.a)로 묶여 링크되는 구조라면, 그
// 파일의 심볼을 아무도 참조하지 않으므로(등록 부작용만 있을 뿐 호출되는
// 함수가 없음) 링커가 해당 오브젝트 전체를 통째로 버릴 수 있다(자체 등록
// 패턴의 흔한 함정). 그 경우 /WHOLEARCHIVE(MSVC) 또는 --whole-archive(GCC/
// Clang) 링크 옵션으로 강제 포함시켜야 한다. 지금처럼 .exe 프로젝트에 직접
// 컴파일해 넣는 구성이면 문제되지 않는다.
//***************************************************************************
class CChatPacketDispatcher
{
public:
	//***************************************************************************
	// @brief 패킷 핸들러를 등록합니다. 직접 호출하지 말고
	//        REGISTER_CHAT_PACKET_HANDLER 매크로를 통해 정적 초기화 시점에
	//        자동 호출되게 하십시오.
	// @param type 패킷 타입
	// @param minSize 이 타입 패킷의 최소 크기(보통 sizeof(해당 패킷 구조체))
	// @param handler 처리 함수
	//***************************************************************************
	static void Register(EChatPacketType type, uint16 minSize, ChatPacketHandler handler);

	//***************************************************************************
	// @brief 패킷을 등록된 핸들러로 디스패치합니다.
	// @return 처리 결과 (Handled/UnknownType/SizeViolation) — 실패 시 어떤
	//         조치를 취할지는 호출부(CChatSession::HandlePacket())가 결정한다.
	//***************************************************************************
	static EChatDispatchResult Dispatch(CChatSession& session, const PacketHeader* header);

private:
	struct Entry
	{
		uint16				minSize;
		ChatPacketHandler	handler;
	};

	//***************************************************************************
	// @brief 함수-로컬 static(Meyer's singleton)으로 테이블을 보관합니다.
	// @details 여러 TU에 흩어진 전역 등록 객체(ChatPacketRegistrar)들이 어떤
	//          순서로 생성되든, 이 함수가 처음 호출되는 시점에 맵이 안전하게
	//          생성된다 — "정적 초기화 순서 문제(SIOF)"를 구조적으로 회피.
	//***************************************************************************
	static std::unordered_map<uint16, Entry>& GetTable();
};

//***************************************************************************
// @brief 패킷 핸들러 모듈이 자신을 등록하기 위한 RAII 헬퍼.
//        네임스페이스(파일) 스코프에 static 인스턴스를 두면 프로그램 시작 시
//        생성자가 실행되며 자동 등록된다. 직접 쓰기보다는 아래
//        REGISTER_CHAT_PACKET_HANDLER 매크로를 통해 쓸 것.
//***************************************************************************
struct ChatPacketRegistrar
{
	ChatPacketRegistrar(EChatPacketType type, uint16 minSize, ChatPacketHandler handler)
	{
		CChatPacketDispatcher::Register(type, minSize, handler);
	}
};

//***************************************************************************
// @brief 핸들러 등록 매크로.
// @details __LINE__을 변수명에 섞어, 한 파일에 등록이 여러 개 있어도 이름
//          충돌이 나지 않게 한다.
//
// 사용 예 (파일 스코프에 배치):
//   REGISTER_CHAT_PACKET_HANDLER(LoginReq, LoginReqPacket, HandleLoginReq);
//***************************************************************************
#define UC_CHAT_PACKET_REGISTRAR_NAME_INNER(line) sChatPacketRegistrar_##line
#define UC_CHAT_PACKET_REGISTRAR_NAME(line) UC_CHAT_PACKET_REGISTRAR_NAME_INNER(line)

#define REGISTER_CHAT_PACKET_HANDLER(EnumName, PacketType, HandlerFunc) \
	static ChatPacketRegistrar UC_CHAT_PACKET_REGISTRAR_NAME(__LINE__)( \
		EChatPacketType::EnumName, sizeof(PacketType), &HandlerFunc)

#endif // ndef UC_CHATPACKETDISPATCHER_H