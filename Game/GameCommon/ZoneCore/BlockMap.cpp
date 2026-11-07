
//***************************************************************************
// BlockMap.cpp : implementation of the CBlockMap class.
//
//***************************************************************************

#include "pch.h"
#include "BlockMap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>

//***************************************************************************
// @brief CBlockMap 클래스의 생성자. Init() 전에는 모든 좌표가 이동 가능이다.
//***************************************************************************
CBlockMap::CBlockMap()
{
}

//***************************************************************************
// @brief 존 영역과 셀 크기로 격자를 구성합니다. 기존 내용은 지워집니다.
// @param minX     영역 최소 X
// @param minY     영역 최소 Y
// @param maxX     영역 최대 X
// @param maxY     영역 최대 Y
// @param cellSize 셀 한 변의 길이 (kMinCellSize 이상)
// @return 성공 시 true, 값이 유효하지 않거나 셀 수가 kMaxCells를 넘으면 false
//***************************************************************************
bool CBlockMap::Init(float minX, float minY, float maxX, float maxY, float cellSize)
{
	_cells.clear();
	_cellsX = 0;
	_cellsY = 0;
	_blockedCount = 0;

	if( !(std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX) && std::isfinite(maxY) && std::isfinite(cellSize)) )
		return false;
	if( !(maxX > minX) || !(maxY > minY) || !(cellSize >= kMinCellSize) )
		return false;

	const double cx = std::ceil((static_cast<double>(maxX) - minX) / cellSize);
	const double cy = std::ceil((static_cast<double>(maxY) - minY) / cellSize);
	if( cx * cy > static_cast<double>(kMaxCells) )
		return false;

	_minX = minX;
	_minY = minY;
	_maxX = maxX;
	_maxY = maxY;
	_cellSize = cellSize;
	_cellsX = static_cast<int32>(cx);
	_cellsY = static_cast<int32>(cy);
	_cells.assign(static_cast<size_t>(_cellsX) * static_cast<size_t>(_cellsY), 0);
	return true;
}

//***************************************************************************
// @brief 좌표가 속한 셀의 X 인덱스를 구합니다. 영역 밖은 가장자리 셀로 보정합니다.
//***************************************************************************
int32 CBlockMap::CellX(double x) const
{
	const double v = std::floor((x - _minX) / _cellSize);
	return static_cast<int32>((std::min)((std::max)(v, 0.0), static_cast<double>(_cellsX - 1)));
}

//***************************************************************************
// @brief 좌표가 속한 셀의 Y 인덱스를 구합니다. 영역 밖은 가장자리 셀로 보정합니다.
//***************************************************************************
int32 CBlockMap::CellY(double y) const
{
	const double v = std::floor((y - _minY) / _cellSize);
	return static_cast<int32>((std::min)((std::max)(v, 0.0), static_cast<double>(_cellsY - 1)));
}

//***************************************************************************
// @brief 셀 하나를 차단 셀로 표시합니다.
//***************************************************************************
void CBlockMap::BlockCell(int32 cx, int32 cy)
{
	uint8& cell = _cells[static_cast<size_t>(cy) * static_cast<size_t>(_cellsX) + static_cast<size_t>(cx)];
	if( cell == 0 )
	{
		cell = 1;
		++_blockedCount;
	}
}

//***************************************************************************
// @brief 사각형 장애물을 등록합니다. 사각형과 조금이라도 겹치는 셀은 모두 차단됩니다.
// @param x      사각형 최소 X (왼쪽 아래 모서리)
// @param y      사각형 최소 Y
// @param width  너비 (0 이하이거나 유한하지 않으면 무시)
// @param height 높이 (0 이하이거나 유한하지 않으면 무시)
//***************************************************************************
void CBlockMap::AddRect(float x, float y, float width, float height)
{
	if( _cells.empty() || !(std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height))
		|| !(width > 0.0f) || !(height > 0.0f) )
		return;

	const double x1 = static_cast<double>(x) + width;
	const double y1 = static_cast<double>(y) + height;
	if( x1 <= _minX || y1 <= _minY || x >= _maxX || y >= _maxY )
		return;

	// 최대 경계가 셀 경계에 정확히 닿으면 다음 셀로 번지지 않게 경계를 살짝 안쪽으로 당겨 셀을 구한다.
	const double eps = static_cast<double>(_cellSize) * 1e-6;
	const int32 cx0 = CellX(x);
	const int32 cy0 = CellY(y);
	const int32 cx1 = (std::max)(cx0, CellX(x1 - eps));
	const int32 cy1 = (std::max)(cy0, CellY(y1 - eps));

	for( int32 cy = cy0; cy <= cy1; ++cy )
		for( int32 cx = cx0; cx <= cx1; ++cx )
			BlockCell(cx, cy);
}

//***************************************************************************
// @brief 원형 장애물을 등록합니다. 원과 조금이라도 겹치는 셀은 모두 차단됩니다.
// @param centerX 중심 X
// @param centerY 중심 Y
// @param radius  반경 (0 이하이거나 유한하지 않으면 무시)
//***************************************************************************
void CBlockMap::AddCircle(float centerX, float centerY, float radius)
{
	if( _cells.empty() || !(std::isfinite(centerX) && std::isfinite(centerY) && std::isfinite(radius)) || !(radius > 0.0f) )
		return;

	const double cx = centerX;
	const double cy = centerY;
	const double r = radius;
	if( cx + r <= _minX || cy + r <= _minY || cx - r >= _maxX || cy - r >= _maxY )
		return;

	const int32 x0 = CellX(cx - r);
	const int32 x1 = CellX(cx + r);
	const int32 y0 = CellY(cy - r);
	const int32 y1 = CellY(cy + r);

	for( int32 iy = y0; iy <= y1; ++iy )
	{
		const double cellMinY = _minY + static_cast<double>(iy) * _cellSize;
		const double nearY = (std::min)((std::max)(cy, cellMinY), cellMinY + _cellSize);
		for( int32 ix = x0; ix <= x1; ++ix )
		{
			const double cellMinX = _minX + static_cast<double>(ix) * _cellSize;
			const double nearX = (std::min)((std::max)(cx, cellMinX), cellMinX + _cellSize);
			const double dx = nearX - cx;
			const double dy = nearY - cy;
			if( dx * dx + dy * dy < r * r )		// 접하기만 하는 셀은 막지 않는다
				BlockCell(ix, iy);
		}
	}
}

//***************************************************************************
// @brief 셀이 차단 셀인지 확인합니다. 격자 밖 인덱스는 false입니다.
//***************************************************************************
bool CBlockMap::IsCellBlocked(int32 cx, int32 cy) const
{
	if( cx < 0 || cy < 0 || cx >= _cellsX || cy >= _cellsY )
		return false;
	return _cells[static_cast<size_t>(cy) * static_cast<size_t>(_cellsX) + static_cast<size_t>(cx)] != 0;
}

//***************************************************************************
// @brief 좌표가 차단 셀 안인지 확인합니다. 영역 밖이거나 NaN이면 false입니다(영역 검사는 호출자 몫).
//***************************************************************************
bool CBlockMap::IsBlocked(const Vec2& pos) const
{
	if( _cells.empty() || !(pos.x >= _minX && pos.x <= _maxX && pos.y >= _minY && pos.y <= _maxY) )
		return false;
	return IsCellBlocked(CellX(pos.x), CellY(pos.y));
}

//***************************************************************************
// @brief 선분이 처음 차단 셀에 들어가는 지점을 찾습니다. (격자 순회: Amanatides-Woo)
//        시작 셀은 검사하지 않고, 격자 밖으로 나가면 거기서 끝낸다.
// @param from  시작점
// @param to    끝점
// @param tHit  [out] 충돌 지점의 선분 매개변수 (0..1)
// @return 차단 셀에 들어가면 true
//***************************************************************************
bool CBlockMap::FindHit(const Vec2& from, const Vec2& to, double& tHit) const
{
	if( _cells.empty() || _blockedCount == 0 )
		return false;

	const double dx = static_cast<double>(to.x) - from.x;
	const double dy = static_cast<double>(to.y) - from.y;
	if( !(std::isfinite(dx) && std::isfinite(dy)) || (dx == 0.0 && dy == 0.0) )
		return false;

	int32 cx = CellX(from.x);
	int32 cy = CellY(from.y);
	const int32 endX = CellX(to.x);
	const int32 endY = CellY(to.y);
	if( cx == endX && cy == endY )
		return false;		// 같은 셀 안의 이동

	constexpr double kInf = (std::numeric_limits<double>::infinity)();
	const int32 stepX = dx > 0.0 ? 1 : -1;
	const int32 stepY = dy > 0.0 ? 1 : -1;
	const double deltaX = dx != 0.0 ? _cellSize / std::fabs(dx) : kInf;
	const double deltaY = dy != 0.0 ? _cellSize / std::fabs(dy) : kInf;

	double maxX = kInf;
	if( dx != 0.0 )
	{
		const double edge = _minX + static_cast<double>(stepX > 0 ? cx + 1 : cx) * _cellSize;
		maxX = (edge - from.x) / dx;
	}
	double maxY = kInf;
	if( dy != 0.0 )
	{
		const double edge = _minY + static_cast<double>(stepY > 0 ? cy + 1 : cy) * _cellSize;
		maxY = (edge - from.y) / dy;
	}

	// 선분이 지나는 셀 수는 (|끝-시작| 셀 차이 합 + 1) 이하이므로 그 횟수만 돈다.
	const int32 limit = std::abs(endX - cx) + std::abs(endY - cy);
	for( int32 i = 0; i < limit; ++i )
	{
		double t;
		if( maxX < maxY )
		{
			t = maxX;
			maxX += deltaX;
			cx += stepX;
		}
		else
		{
			t = maxY;
			maxY += deltaY;
			cy += stepY;
		}

		if( t > 1.0 || cx < 0 || cy < 0 || cx >= _cellsX || cy >= _cellsY )
			return false;

		if( IsCellBlocked(cx, cy) )
		{
			tHit = (std::max)(t, 0.0);
			return true;
		}
	}

	return false;
}

//***************************************************************************
// @brief 선분 전체가 차단 셀을 지나지 않는지 확인합니다.
// @param from 시작점
// @param to   끝점
// @return 막힘이 없으면 true
//***************************************************************************
bool CBlockMap::IsSegmentClear(const Vec2& from, const Vec2& to) const
{
	double t;
	return !FindHit(from, to, t);
}

//***************************************************************************
// @brief from에서 to로 직선 이동할 때 막히기 전까지 갈 수 있는 가장 먼 지점을 구합니다.
//        막히면 충돌 지점에서 kBackoff만큼 물러선 위치를 돌려줘, 부동소수점 오차로 차단 셀에 들어가지 않게 합니다.
// @param from 시작점
// @param to   목적지
// @param hit  [out] 선택. 막혀서 목적지가 바뀌었으면 true
// @return 도달 가능한 지점 (막힘이 없으면 to 그대로)
//***************************************************************************
Vec2 CBlockMap::ClampMove(const Vec2& from, const Vec2& to, bool* hit) const
{
	double t;
	const bool blocked = FindHit(from, to, t);
	if( hit != nullptr )
		*hit = blocked;
	if( !blocked )
		return to;

	const double dx = static_cast<double>(to.x) - from.x;
	const double dy = static_cast<double>(to.y) - from.y;
	const double len = std::sqrt(dx * dx + dy * dy);
	double tt = (std::max)(0.0, t - static_cast<double>(kBackoff) / len);

	// float로 줄이면서 좌표가 큰 곳(ulp > kBackoff)에서는 차단 셀 안으로 반올림될 수 있으므로 확인하고 더 물러선다.
	Vec2 out = from;
	for( int i = 0; i < 8; ++i )
	{
		out.x = static_cast<float>(from.x + dx * tt);
		out.y = static_cast<float>(from.y + dy * tt);
		if( tt <= 0.0 || !IsBlocked(out) )
			return out;
		tt = (std::max)(0.0, tt - static_cast<double>(kBackoff) * (2 << i) / len);
	}
	return from;
}

//***************************************************************************
// @brief from에서 to까지 장애물을 피하는 경로를 찾습니다. (8방향 A*, 대각선은 양쪽 이웃 셀이 비어 있을 때만)
//        직선이 비어 있으면 [to] 한 점만 돌려준다. 그렇지 않으면 셀 경로를 구한 뒤, 서로 직선으로 갈 수 있는
//        경유점을 건너뛰어 합친다. 반환값의 각 구간은 IsSegmentClear()를 통과하도록 만들어지므로
//        서버가 경유점을 차례로 이동 명령으로 내려도 ClampMove()에 걸리지 않는다.
//        펼친 셀 수가 maxNodes를 넘으면 실패한다(도달 불가 목적지에서 맵 전체를 뒤지지 않도록 비용을 묶는다).
// @param from     시작점
// @param to       목적지 (차단 셀 안이면 실패)
// @param out      [out] 경유점. from은 포함하지 않고 마지막은 to
// @param maxNodes 펼칠 수 있는 셀 수 상한
// @return 경로를 찾으면 true
//***************************************************************************
bool CBlockMap::FindPath(const Vec2& from, const Vec2& to, std::vector<Vec2>& out, uint32 maxNodes) const
{
	out.clear();
	if( !(std::isfinite(from.x) && std::isfinite(from.y) && std::isfinite(to.x) && std::isfinite(to.y)) )
		return false;

	if( _cells.empty() || _blockedCount == 0 || IsSegmentClear(from, to) )
	{
		out.push_back(to);
		return true;
	}

	const int32 gx = CellX(to.x);
	const int32 gy = CellY(to.y);
	if( IsCellBlocked(gx, gy) )
		return false;

	struct Node
	{
		float	g;
		int32	x;
		int32	y;
		int32	parent;
	};
	struct Entry
	{
		float	f;
		int32	node;
		bool operator<(const Entry& other) const { return f > other.f; }		// 최소 힙
	};

	// 호출마다 할당하지 않도록 스레드별 작업 공간을 재사용한다.
	thread_local std::vector<Node> nodes;
	thread_local std::unordered_map<int64, int32> best;
	thread_local std::vector<uint8> closed;
	nodes.clear();
	best.clear();
	closed.clear();

	constexpr float kDiag = 1.41421356f;
	constexpr float kHeuristicWeight = 1.5f;
	auto Heuristic = [&](int32 x, int32 y)
	{
		const float dx = static_cast<float>(std::abs(x - gx));
		const float dy = static_cast<float>(std::abs(y - gy));
		// 가중치 1.5: 최단은 아니어도 펼치는 셀 수를 크게 줄인다. 결과는 경유점 병합으로 다듬어진다.
		return kHeuristicWeight * ((dx > dy ? dx : dy) + (kDiag - 1.0f) * (dx > dy ? dy : dx));
	};
	auto Key = [&](int32 x, int32 y) { return static_cast<int64>(y) * _cellsX + x; };

	std::priority_queue<Entry> open;
	const int32 sx = CellX(from.x);
	const int32 sy = CellY(from.y);
	nodes.push_back({ 0.0f, sx, sy, -1 });
	closed.push_back(0);
	best[Key(sx, sy)] = 0;
	open.push({ Heuristic(sx, sy), 0 });

	int32 goalNode = -1;
	uint32 expanded = 0;
	while( !open.empty() )
	{
		const Entry top = open.top();
		open.pop();
		const int32 idx = top.node;
		if( closed[idx] )
			continue;
		closed[idx] = 1;

		const Node cur = nodes[idx];
		if( best[Key(cur.x, cur.y)] != idx )
			continue;		// 더 좋은 경로로 갱신되어 뒤처진 중복 노드 (다시 펼치지 않는다)
		if( cur.x == gx && cur.y == gy )
		{
			goalNode = idx;
			break;
		}
		if( ++expanded > maxNodes )
			return false;

		for( int32 dy = -1; dy <= 1; ++dy )
		{
			for( int32 dx = -1; dx <= 1; ++dx )
			{
				if( dx == 0 && dy == 0 )
					continue;

				const int32 nx = cur.x + dx;
				const int32 ny = cur.y + dy;
				if( nx < 0 || ny < 0 || nx >= _cellsX || ny >= _cellsY || IsCellBlocked(nx, ny) )
					continue;
				if( dx != 0 && dy != 0 && (IsCellBlocked(cur.x + dx, cur.y) || IsCellBlocked(cur.x, cur.y + dy)) )
					continue;		// 모서리 가로지르기 금지

				const float ng = cur.g + (dx != 0 && dy != 0 ? kDiag : 1.0f);
				const int64 key = Key(nx, ny);
				const auto it = best.find(key);
				if( it != best.end() && ng >= nodes[it->second].g )
					continue;

				nodes.push_back({ ng, nx, ny, idx });
				closed.push_back(0);
				const int32 ni = static_cast<int32>(nodes.size()) - 1;
				best[key] = ni;
				open.push({ ng + Heuristic(nx, ny), ni });
			}
		}
	}

	if( goalNode < 0 )
		return false;

	// 셀 경로를 거꾸로 따라가 점 목록(from, 셀 중심들, to)을 만든다.
	thread_local std::vector<Vec2> pts;
	pts.clear();
	for( int32 n = goalNode; n >= 0 && nodes[n].parent >= 0; n = nodes[n].parent )
	{
		Vec2 c;
		c.x = static_cast<float>(_minX + (nodes[n].x + 0.5) * _cellSize);
		c.y = static_cast<float>(_minY + (nodes[n].y + 0.5) * _cellSize);
		pts.push_back(c);
	}
	if( pts.empty() )
		pts.push_back(to);
	pts.front() = to;		// 마지막 셀의 중심 대신 실제 목적지
	std::reverse(pts.begin(), pts.end());
	pts.insert(pts.begin(), from);

	// 직선으로 갈 수 있는 가장 먼 점으로 건너뛴다.
	size_t i = 0;
	while( i + 1 < pts.size() )
	{
		size_t next = i + 1;
		for( size_t j = pts.size() - 1; j > i + 1; --j )
		{
			if( IsSegmentClear(pts[i], pts[j]) )
			{
				next = j;
				break;
			}
		}
		out.push_back(pts[next]);
		i = next;
	}
	return true;
}
