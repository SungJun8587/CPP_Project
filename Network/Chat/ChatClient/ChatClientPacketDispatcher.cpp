
//***************************************************************************
// ChatClientPacketDispatcher.cpp: implementation of the CChatClientPacketDispatcher class.
//
//***************************************************************************

#include "pch.h"
#include "ChatClientPacketDispatcher.h"

std::unordered_map<uint16, CChatClientPacketDispatcher::Entry>& CChatClientPacketDispatcher::GetTable()
{
	static std::unordered_map<uint16, Entry> table;
	return table;
}

void CChatClientPacketDispatcher::Register(EChatPacketType type, uint16 minSize, ChatClientPacketHandler handler)
{
	const uint16 key = static_cast<uint16>(type);

	ASSERT_CRASH(GetTable().find(key) == GetTable().end());

	GetTable().emplace(key, Entry{ minSize, handler });
}

EChatDispatchResult CChatClientPacketDispatcher::Dispatch(CChatClientSession& session, const PacketHeader* header)
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
