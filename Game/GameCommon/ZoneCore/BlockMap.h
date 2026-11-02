
//***************************************************************************
// BlockMap.h : interface for the CBlockMap class.
//
//***************************************************************************

#ifndef BLOCKMAP_H
#define BLOCKMAP_H

#include <vector>

#include <BaseRedefineDataType.h>

#include "ZoneMath.h"

//***************************************************************************
// @class CBlockMap
// @brief 존 평면을 균일 격자로 나누고 셀마다 이동 가능/불가를 기록한 차단 맵입니다.
// @details 나무, 바위, 절벽 같은 장애물을 사각형/원으로 등록하면 격자에 래스터화한다(겹치는 셀은 모두 차단).
//          서버는 이 맵으로 이동 목적지까지의 직선 경로를 검사한다. 엔티티는 점으로 취급하므로
//          몸 크기를 반영하려면 장애물을 엔티티 반경만큼 키워서 등록한다.
//
//          Init/AddRect/AddCircle로 구성을 마친 뒤에는 읽기 전용이며, 읽기 메서드는 여러 스레드에서 동시에 호출해도 안전하다.
//          (구성 중에는 한 스레드에서만 사용한다.) 존에는 std::shared_ptr<const CBlockMap>으로 공유한다.
//
//          FindPath()는 8방향 A*로 우회 경로를 찾아 직선으로 이어지는 구간을 합친 경유점 목록을 돌려준다.
//          각 구간은 IsSegmentClear()를 통과하므로 서버의 ClampMove()가 구간을 줄이지 않는다.
//
//          경로 검사는 시작 셀을 제외한 셀만 본다. 시작 셀이 차단이어도(경계 오차로 가장자리에 걸친 경우 등)
//          이웃 셀이 비어 있으면 빠져나갈 수 있다. 차단 영역 한가운데에 있는 엔티티는 이동할 수 없으며, 스폰/순간이동 검증으로 그런 위치에 놓이지 않게 한다.
//***************************************************************************
class CBlockMap
{
public:
	// 셀 수 상한. 이보다 크면 Init이 실패한다(셀당 1바이트).
	static constexpr size_t kMaxCells = 64u * 1024u * 1024u;
	// 셀 크기 하한. 경로 검사가 충돌 지점에서 물러서는 거리(kBackoff)보다 충분히 커야 한다.
	static constexpr float kMinCellSize = 0.5f;
	// 서버와 클라이언트(봇)가 같은 판정을 하려면 같은 셀 크기를 써야 하므로 공용 기본값을 둔다.
	static constexpr float kDefaultCellSize = 1.0f;
	// 충돌 지점에서 물러서는 거리. 부동소수점 오차로 차단 셀 경계를 넘어가지 않게 한다.
	static constexpr float kBackoff = 0.01f;
	// 경로탐색 한 번에 펼칠 수 있는 셀 수 기본 상한. 막힌 목적지에서 맵 전체를 뒤지지 않게 비용을 묶는다.
	static constexpr uint32 kDefaultPathNodes = 8192;
	// 장애물을 키우는 기본 여유 폭(엔티티 반경 대신). 서버와 봇이 같은 값을 써야 같은 판정이 된다.
	static constexpr float kDefaultMargin = 0.5f;

	CBlockMap();

	bool Init(float minX, float minY, float maxX, float maxY, float cellSize);

	// ── 구성 ──────────────────────────────────────────────
	void AddRect(float x, float y, float width, float height);
	void AddCircle(float centerX, float centerY, float radius);

	// ── 조회 (구성 후 읽기 전용) ──────────────────────────
	bool IsBlocked(const Vec2& pos) const;
	bool IsSegmentClear(const Vec2& from, const Vec2& to) const;
	Vec2 ClampMove(const Vec2& from, const Vec2& to, bool* hit = nullptr) const;
	bool FindPath(const Vec2& from, const Vec2& to, std::vector<Vec2>& out, uint32 maxNodes = kDefaultPathNodes) const;

	size_t GetBlockedCellCount() const { return _blockedCount; }
	int32 GetCellsX() const { return _cellsX; }
	int32 GetCellsY() const { return _cellsY; }
	float GetCellSize() const { return _cellSize; }
	bool IsCellBlocked(int32 cx, int32 cy) const;

private:
	int32 CellX(double x) const;
	int32 CellY(double y) const;
	bool FindHit(const Vec2& from, const Vec2& to, double& tHit) const;
	void BlockCell(int32 cx, int32 cy);

	float _minX = 0.0f;
	float _minY = 0.0f;
	float _maxX = 0.0f;
	float _maxY = 0.0f;
	float _cellSize = 1.0f;
	int32 _cellsX = 0;
	int32 _cellsY = 0;
	size_t _blockedCount = 0;
	std::vector<uint8> _cells;		// 0 = 이동 가능, 1 = 차단
};

#endif // ndef BLOCKMAP_H