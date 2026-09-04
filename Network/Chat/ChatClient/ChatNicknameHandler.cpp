//***************************************************************************
// ChatNicknameHandler.cpp : NicknameGenerateReq 패킷 핸들러 (자체 등록)
//
//***************************************************************************

#include "pch.h"
#include "ChatSession.h"
#include "ChatPacketDispatcher.h"

#include <random>
#include <string>
#include <cstring>
#include <algorithm>
#include <iterator>

namespace
{
	const char* const kAdjectives[] =
	{
		"Brave", "Clever", "Swift", "Silent", "Happy", "Lucky", "Mighty", "Gentle"
	};

	const char* const kNouns[] =
	{
		"Tiger", "Falcon", "Wolf", "Panda", "Dragon", "Otter", "Fox", "Owl"
	};

	//***************************************************************************
	// @brief "형용사+명사+4자리 숫자" 조합으로 랜덤 닉네임을 생성합니다.
	// @details thread_local RNG — IOCP 워커 스레드마다 독립된 엔진을 가져서
	//          락 경합이나 시드 공유 문제 없이 여러 스레드가 동시에 호출해도
	//          안전하다.
	// @note [알려진 한계] 서버 전체에 걸친 유일성(uniqueness)을 보장하지
	//       않는다 — 조합 가짓수(8*8*9000 ≈ 57.6만)상 실무적으로 드물지만,
	//       두 유저가 같은 닉네임을 받을 수 있다. 진짜 유일성이 필요하면
	//       Redis SET(NX)로 생성된 닉네임을 등록/충돌 시 재생성하는 절차를
	//       추가해야 한다(기본 뼈대 범위 밖).
	//***************************************************************************
	std::string GenerateRandomNickname()
	{
		thread_local std::mt19937 rng(std::random_device{}());

		std::uniform_int_distribution<size_t> adjDist(0, std::size(kAdjectives) - 1);
		std::uniform_int_distribution<size_t> nounDist(0, std::size(kNouns) - 1);
		std::uniform_int_distribution<int> numDist(1000, 9999);

		std::string nickname = kAdjectives[adjDist(rng)];
		nickname += kNouns[nounDist(rng)];
		nickname += std::to_string(numDist(rng));
		return nickname;
	}

	//***************************************************************************
	// @brief 랜덤 닉네임을 생성해 응답합니다. 로그인 여부와 무관하게 처리됩니다.
	//***************************************************************************
	void HandleNicknameGenerateReq(CChatSession& session, const PacketHeader* /*header*/)
	{
		const std::string nickname = GenerateRandomNickname();

		NicknameGenerateResPacket res{};
		res.type = static_cast<uint16>(EChatPacketType::NicknameGenerateRes);
		res.size = sizeof(res);

		const size_t copyLen = (std::min)(nickname.size(), sizeof(res.nickname) - 1);
		::memcpy(res.nickname, nickname.data(), copyLen);
		// 나머지는 {} 초기화로 이미 0-채움 → NUL 종단 보장

		session.Send(&res, sizeof(res));
	}
}

REGISTER_CHAT_PACKET_HANDLER(NicknameGenerateReq, NicknameGenerateReqPacket, HandleNicknameGenerateReq);
