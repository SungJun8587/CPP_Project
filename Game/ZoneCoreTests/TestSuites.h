// 테스트 스위트 목록: 각 테스트 파일이 하나씩 내보내는 진입 함수. TestMain.cpp가 골라서 호출한다.
// 모든 함수는 실패한 검사 수가 0이면 0, 아니면 1을 돌려준다.
#pragma once

#include <BaseRedefineDataType.h>

int RunZoneTests();								// tests.cpp          : CMoveOnlyFunction / CTimerHeap / CJob / CZone / CGridAOI / CFieldZone
int RunCombatTests();							// combat_tests.cpp   : 전투(피해, 사거리, PvP, 사망/부활, 무적)
int RunMonsterTests();							// monster_tests.cpp  : 몬스터 스폰/어그로/추적/복귀/리스폰
int RunGameDataTests(const _tstring& dataDir);	// game_data_tests.cpp: JSON 게임 데이터 로더 (dataDir = 샘플 GameData 폴더)
int RunZoneConfigTests();						// zone_config_tests.cpp: 존 서버 설정 파일 읽기/저장/검증
int RunCombatFuzz(int seeds);					// fuzz_combat.cpp    : 전투 랜덤 연산 (시드 수만큼 반복)
int RunMonsterFuzz();							// fuzz_monster.cpp   : 몬스터 랜덤 연산 + 불변식 점검
