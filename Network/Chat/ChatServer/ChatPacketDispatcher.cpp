
//***************************************************************************
// ChatPacketDispatcher.cpp: implementation of the CChatPacketDispatcher class.
//
//***************************************************************************

#include "pch.h"
#include "ChatPacketDispatcher.h"

//***************************************************************************
// @brief 등록 테이블을 보관하는 함수-로컬 static (Meyer's singleton).
//***************************************************************************
std::unordered_map<uint16, CChatPacketDispatcher::Entry>& CChatPacketDispatcher::GetTable()
{
	static std::unordered_map<uint16, Entry> table;
	return table;
}

//***************************************************************************
// @brief 패킷 핸들러를 등록합니다.
//***************************************************************************
void CChatPacketDispatcher::Register(EChatPacketType type, uint16 minSize, ChatPacketHandler handler)
{
	const uint16 key = static_cast<uint16>(type);

	// 같은 타입이 중복 등록되면(복붙 실수 등) 조용히 덮어쓰지 않고 바로
	// 알아챌 수 있게 크래시시킨다 — 정적 초기화 시점(프로그램 시작 전)이라
	// 여기서 멈추는 게 런타임에 엉뚱한 핸들러가 호출되는 것보다 안전하다.
	ASSERT_CRASH(GetTable().find(key) == GetTable().end());

	GetTable().emplace(key, Entry{ minSize, handler });
}

//***************************************************************************
// @brief 패킷을 등록된 핸들러로 디스패치합니다.
//***************************************************************************
EChatDispatchResult CChatPacketDispatcher::Dispatch(CChatSession& session, const PacketHeader* header)
{
	auto& table = GetTable();

	auto it = table.find(header->type);
	if( it == table.end() )
		return EChatDispatchResult::UnknownType;

	if( header->size < it->second.minSize )
		return EChatDispatchResult::SizeViolation;

	it->second.handler(session, header);
	return EChatDispatchResult::Handled;
}
