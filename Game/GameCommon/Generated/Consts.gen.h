
//***************************************************************************
// Consts.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// XlsxConverter가 GameData.xlsx의 Const 시트로부터 만듭니다. 문자열 상수가 있으면 TCHAR, _T가 필요하다. (pch가 제공)
//***************************************************************************

#ifndef GAMEDATACONSTS_GEN_H
#define GAMEDATACONSTS_GEN_H

#include <cstdint>

namespace GameDataConsts
{
	constexpr std::uint32_t kMaxHeroSkills = 8;	// 영웅 한 명이 가질 수 있는 스킬 수 상한 (클라이언트 슬롯 수와 맞춘다)
	constexpr std::uint32_t kMaxStatusesPerEntity = 8;	// 엔티티 한 명에게 동시에 걸릴 수 있는 상태 효과 수
	constexpr std::uint32_t kMaxBossAdds = 12;	// 보스 한 마리가 동시에 데리고 있을 수 있는 소환 몬스터 수
	constexpr std::uint32_t kMaxBossActionsPerPhase = 16;	// 보스 페이즈 하나에 넣을 수 있는 행동(BossAction) 수
}

#endif // ndef GAMEDATACONSTS_GEN_H
