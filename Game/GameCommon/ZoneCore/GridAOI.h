//***************************************************************************
// GridAOI.h : interface for the CGridAOI class.
//
//***************************************************************************

#ifndef GRIDAOI_H
#define GRIDAOI_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

#include <BaseRedefineDataType.h>
#include <Memory/Containers.h>

#include "ZoneMath.h"

//***************************************************************************
// @brief 엔티티가 AOI 격자 안에서의 위치를 기록해 두는 노드. AOI를 쓰는 엔티티가 멤버로 가진다.
//        엔티티 하나가 한 격자에만 속하므로 격자별 해시 맵 없이 O(1)로 제거/이동할 수 있다.
//***************************************************************************
struct SAoiNode
{
	static constexpr uint32 kInvalidSlot = static_cast<uint32>(-1);

	int32	cellIdx = -1;				// 소속 셀 인덱스 (-1: 미소속)
	uint32	slot = kInvalidSlot;		// 소속 셀 안에서의 위치
};

//***************************************************************************
// @class CGridAOI
// @brief 격자(셀) 기반 관심영역(AOI) 관리자.
// @details
// 존 영역을 cellSize 크기의 정사각 셀로 나누고, 엔티티는 자신이 속한 셀 기준
// (2*viewCells+1)^2 셀 범위를 시야로 가진다. 거리가 (viewCells - 2*hysteresis)*cellSize 이내인 엔티티끼리는
// 항상 서로 보이고(최소 보장 시야), 실제로 보이는 최대 거리는 (viewCells+1)*cellSize 근처까지 늘어난다.
// 따라서 (viewCells - 2*hysteresis)*cellSize를 클라이언트가 기대하는 시야 거리 이상으로 잡아야 한다.
//
// 엔티티가 셀 경계를 넘을 때만 (새 시야 - 옛 시야)에 진입 통지, (옛 시야 - 새 시야)에 이탈 통지를
// 양방향으로 발생시킨다. 같은 셀 안의 이동은 비용이 거의 없다(셀 인덱스 계산만 수행).
//
// 히스테리시스: 셀 경계에서 왔다 갔다 하는 엔티티가 매 틱 진입/이탈 통지를 쏟아내지 않도록,
// 엔티티는 현재 셀을 경계 바깥으로 hysteresis*cellSize 이상 벗어나야 소속 셀을 바꾼다.
// (그래서 엔티티의 실제 위치는 소속 셀에서 최대 hysteresis*cellSize 만큼 벗어나 있을 수 있다.)
//
// 요구 사항 — TEntity는 다음을 제공해야 한다:
//     - const Vec2& GetPos() const
//     - SAoiNode 멤버 _aoiNode, Vec2 멤버 _pos (이 클래스가 friend로 접근)
//     - void OnAoiEnter(TEntity& other) / void OnAoiLeave(TEntity& other)
//
// 스레드 안전하지 않다. 소유 존의 틱 스레드에서만 호출해야 한다.
// 통지 콜백 안에서 Add/Remove/Update를 호출하면 안 된다(디버그 빌드에서 assert).
//***************************************************************************
template <typename TEntity>
class CGridAOI
{
public:
	CGridAOI() = default;

	CGridAOI(const CGridAOI&) = delete;
	CGridAOI& operator=(const CGridAOI&) = delete;

	//***************************************************************************
	// @brief 영역과 셀 크기를 지정해 격자를 구성합니다.
	// @param minX       영역 최소 X
	// @param minY       영역 최소 Y
	// @param maxX       영역 최대 X
	// @param maxY       영역 최대 Y
	// @param cellSize   셀 한 변의 길이
	// @param viewCells  시야 반경(셀 단위, 1이면 3x3)
	// @param hysteresis 셀 변경 히스테리시스(셀 크기 대비 비율, 0이면 끔, 0.5 미만이어야 함)
	// @return 성공 시 true, 인자가 유효하지 않으면 false
	//***************************************************************************
	bool Init(float minX, float minY, float maxX, float maxY, float cellSize, int32 viewCells = 1, float hysteresis = 0.1f)
	{
		if( !(cellSize > 0.0f) || !(maxX > minX) || !(maxY > minY) || viewCells < 1 || !(hysteresis >= 0.0f) || !(hysteresis < 0.5f) )
			return false;

		const double cols = std::ceil(static_cast<double>(maxX - minX) / cellSize);
		const double rows = std::ceil(static_cast<double>(maxY - minY) / cellSize);
		if( cols * rows > 16.0 * 1024.0 * 1024.0 )
			return false;		// 셀 수가 비정상적으로 많은 설정(셀 크기 오설정)을 거부

		_minX = minX;
		_minY = minY;
		_cellSize = cellSize;
		_invCellSize = 1.0f / cellSize;
		_cols = (std::max)(1, static_cast<int32>(cols));
		_rows = (std::max)(1, static_cast<int32>(rows));
		_view = viewCells;
		_hysteresis = hysteresis;

		_cells.clear();
		_cells.resize(static_cast<size_t>(_cols) * static_cast<size_t>(_rows));
		_entityCount = 0;
		return true;
	}

	//***************************************************************************
	// @brief 엔티티를 AOI에 등록하고, 시야 안의 엔티티와 양방향 진입 통지를 발생시킵니다.
	// @param entity 등록할 엔티티 (위치가 설정되어 있어야 함)
	// @return 등록 성공 시 true, 이미 등록되었거나 Init 전이면 false
	//***************************************************************************
	bool Add(TEntity* entity)
	{
		assert(!_dispatching && "CGridAOI::Add called inside AOI callback");

		if( entity == nullptr || _cols == 0 || entity->_aoiNode.cellIdx >= 0 )
			return false;

		const int32 cellIdx = CellIndexOf(entity->_pos);
		InsertToCell(entity, cellIdx);
		++_entityCount;

		NotifyCells(entity, cellIdx, -1, true);
		return true;
	}

	//***************************************************************************
	// @brief 엔티티를 AOI에서 제거하고, 시야 안의 엔티티와 양방향 이탈 통지를 발생시킵니다.
	// @param entity 제거할 엔티티
	//***************************************************************************
	void Remove(TEntity* entity)
	{
		assert(!_dispatching && "CGridAOI::Remove called inside AOI callback");

		if( entity == nullptr || entity->_aoiNode.cellIdx < 0 )
			return;

		NotifyCells(entity, entity->_aoiNode.cellIdx, -1, false);

		EraseFromCell(entity);
		--_entityCount;
	}

	//***************************************************************************
	// @brief 엔티티의 위치가 바뀐 뒤 호출합니다. 소속 셀이 바뀐 경우에만
	//        (옛 시야 - 새 시야)에 이탈 통지, (새 시야 - 옛 시야)에 진입 통지를 보냅니다.
	//        순간이동처럼 두 시야가 겹치지 않는 경우도 같은 규칙으로 처리됩니다.
	// @param entity 위치가 갱신된 엔티티 (_pos가 이미 새 값이어야 함)
	// @return 소속 셀이 바뀌었으면 true
	//***************************************************************************
	bool Update(TEntity* entity)
	{
		assert(!_dispatching && "CGridAOI::Update called inside AOI callback");

		if( entity == nullptr || entity->_aoiNode.cellIdx < 0 )
			return false;

		const int32 oldCell = entity->_aoiNode.cellIdx;

		// 현재 셀을 히스테리시스만큼 확장한 영역 안이면 셀을 바꾸지 않는다.
		if( _hysteresis > 0.0f )
		{
			const float fx = (entity->_pos.x - _minX) * _invCellSize;
			const float fy = (entity->_pos.y - _minY) * _invCellSize;
			const float cx = static_cast<float>(oldCell % _cols);
			const float cy = static_cast<float>(oldCell / _cols);

			if( fx >= cx - _hysteresis && fx < cx + 1.0f + _hysteresis &&
				fy >= cy - _hysteresis && fy < cy + 1.0f + _hysteresis )
				return false;
		}

		const int32 newCell = CellIndexOf(entity->_pos);
		if( newCell == oldCell )
			return false;

		EraseFromCell(entity);
		InsertToCell(entity, newCell);

		NotifyCells(entity, oldCell, newCell, false);
		NotifyCells(entity, newCell, oldCell, true);
		return true;
	}

	//***************************************************************************
	// @brief 엔티티의 시야 셀 안에 있는 다른 엔티티를 순회합니다. (자기 자신 제외)
	//        순회 중 AOI 구조를 변경하면 안 됩니다.
	// @tparam F void(TEntity&) 형태의 호출 가능 객체
	// @param entity 기준 엔티티 (AOI에 등록되어 있어야 함)
	// @param fn     각 이웃 엔티티에 대해 호출할 함수
	//***************************************************************************
	template <typename F>
	void ForEachNeighbor(const TEntity& entity, F&& fn) const
	{
		if( entity._aoiNode.cellIdx < 0 )
			return;

		const int32 cx = entity._aoiNode.cellIdx % _cols;
		const int32 cy = entity._aoiNode.cellIdx / _cols;

		for( int32 y = (std::max)(cy - _view, 0); y <= (std::min)(cy + _view, _rows - 1); ++y )
		{
			for( int32 x = (std::max)(cx - _view, 0); x <= (std::min)(cx + _view, _cols - 1); ++x )
			{
				const CVector<TEntity*>& cell = _cells[static_cast<size_t>(y) * _cols + x];
				for( size_t i = 0; i < cell.size(); ++i )
				{
					if( cell[i] != &entity )
						fn(*cell[i]);
				}
			}
		}
	}

	//***************************************************************************
	// @brief 임의 좌표 기준 원형 범위 안의 엔티티를 순회합니다. 범위 스킬/어그로 탐색용.
	//        셀로 후보를 줄인 뒤 정확한 거리 비교를 수행합니다.
	//        히스테리시스로 엔티티가 소속 셀 밖에 있을 수 있어 후보 셀 범위를 그만큼 넓혀 탐색합니다.
	// @tparam F void(TEntity&) 형태의 호출 가능 객체
	// @param center 중심 좌표
	// @param radius 반경 (NaN/음수면 아무것도 순회하지 않음)
	// @param fn     범위 안의 각 엔티티에 대해 호출할 함수
	//***************************************************************************
	template <typename F>
	void ForEachInRadius(const Vec2& center, float radius, F&& fn) const
	{
		if( _cols == 0 || !(radius >= 0.0f) || !std::isfinite(center.x) || !std::isfinite(center.y) )
			return;

		const float radiusSq = radius * radius;
		const float reach = radius + _hysteresis * _cellSize;
		const int32 x0 = CellX(center.x - reach);
		const int32 x1 = CellX(center.x + reach);
		const int32 y0 = CellY(center.y - reach);
		const int32 y1 = CellY(center.y + reach);

		for( int32 y = y0; y <= y1; ++y )
		{
			for( int32 x = x0; x <= x1; ++x )
			{
				const CVector<TEntity*>& cell = _cells[static_cast<size_t>(y) * _cols + x];
				for( size_t i = 0; i < cell.size(); ++i )
				{
					if( DistanceSq(cell[i]->GetPos(), center) <= radiusSq )
						fn(*cell[i]);
				}
			}
		}
	}

	//***************************************************************************
	// @brief 등록된 엔티티 수를 반환합니다.
	// @return 엔티티 수
	//***************************************************************************
	size_t GetEntityCount() const { return _entityCount; }

	//***************************************************************************
	// @brief 시야 반경(셀 단위)을 반환합니다.
	// @return 시야 반경
	//***************************************************************************
	int32 GetViewCells() const { return _view; }

	//***************************************************************************
	// @brief 최소 보장 시야 반경(월드 단위)을 반환합니다. 이 거리 안의 두 엔티티는 항상 서로의 시야에 있습니다.
	//        클라이언트와 합의된 시야 거리 이상이어야 합니다.
	// @return (viewCells - 2 * hysteresis) * cellSize
	//***************************************************************************
	float GetGuaranteedViewRadius() const { return (static_cast<float>(_view) - 2.0f * _hysteresis) * _cellSize; }

private:
	//***************************************************************************
	// @brief 셀 공간 실수 좌표를 [0, count-1] 범위의 셀 인덱스로 변환합니다.
	//        NaN/Inf/거대한 값도 안전하게 가장자리 셀로 보정합니다. (float->int 캐스트의 UB 방지)
	//***************************************************************************
	static int32 ClampCell(float f, int32 count)
	{
		if( !(f > 0.0f) )
			return 0;
		if( f >= static_cast<float>(count) )
			return count - 1;
		return static_cast<int32>(f);
	}

	int32 CellX(float x) const { return ClampCell((x - _minX) * _invCellSize, _cols); }
	int32 CellY(float y) const { return ClampCell((y - _minY) * _invCellSize, _rows); }
	int32 CellIndexOf(const Vec2& pos) const { return CellY(pos.y) * _cols + CellX(pos.x); }

	//***************************************************************************
	// @brief 엔티티를 셀 목록 끝에 삽입하고 소속 정보를 기록합니다.
	//***************************************************************************
	void InsertToCell(TEntity* entity, int32 cellIdx)
	{
		CVector<TEntity*>& cell = _cells[cellIdx];

		entity->_aoiNode.cellIdx = cellIdx;
		entity->_aoiNode.slot = static_cast<uint32>(cell.size());
		cell.push_back(entity);
	}

	//***************************************************************************
	// @brief 엔티티를 소속 셀에서 O(1)로 제거합니다. (마지막 원소와 교체 후 pop)
	//***************************************************************************
	void EraseFromCell(TEntity* entity)
	{
		CVector<TEntity*>& cell = _cells[entity->_aoiNode.cellIdx];

		TEntity* last = cell.back();
		cell[entity->_aoiNode.slot] = last;
		last->_aoiNode.slot = entity->_aoiNode.slot;
		cell.pop_back();

		entity->_aoiNode.cellIdx = -1;
		entity->_aoiNode.slot = SAoiNode::kInvalidSlot;
	}

	//***************************************************************************
	// @brief centerCell 기준 시야 셀 중 excludeCell 기준 시야에 속하지 않는 셀의
	//        엔티티들과 양방향으로 진입/이탈 통지를 주고받습니다.
	// @param entity      통지 대상 엔티티 (자기 자신은 건너뜀)
	// @param centerCell  순회할 시야의 중심 셀
	// @param excludeCell 제외할 시야의 중심 셀 (-1이면 제외 없음)
	// @param enter       true면 진입 통지, false면 이탈 통지
	//***************************************************************************
	void NotifyCells(TEntity* entity, int32 centerCell, int32 excludeCell, bool enter)
	{
		const int32 cx = centerCell % _cols;
		const int32 cy = centerCell / _cols;
		const int32 ex = excludeCell >= 0 ? excludeCell % _cols : 0;
		const int32 ey = excludeCell >= 0 ? excludeCell / _cols : 0;

		_dispatching = true;

		for( int32 y = (std::max)(cy - _view, 0); y <= (std::min)(cy + _view, _rows - 1); ++y )
		{
			for( int32 x = (std::max)(cx - _view, 0); x <= (std::min)(cx + _view, _cols - 1); ++x )
			{
				if( excludeCell >= 0 && std::abs(x - ex) <= _view && std::abs(y - ey) <= _view )
					continue;

				CVector<TEntity*>& cell = _cells[static_cast<size_t>(y) * _cols + x];
				for( size_t i = 0; i < cell.size(); ++i )
				{
					TEntity* other = cell[i];
					if( other == entity )
						continue;

					if( enter )
					{
						entity->OnAoiEnter(*other);
						other->OnAoiEnter(*entity);
					}
					else
					{
						entity->OnAoiLeave(*other);
						other->OnAoiLeave(*entity);
					}
				}
			}
		}

		_dispatching = false;
	}

	float	_minX = 0.0f;				// 영역 최소 X
	float	_minY = 0.0f;				// 영역 최소 Y
	float	_cellSize = 1.0f;			// 셀 한 변의 길이
	float	_invCellSize = 0.0f;		// 셀 크기의 역수 (나눗셈 회피)
	float	_hysteresis = 0.0f;			// 셀 변경 히스테리시스 (셀 크기 대비 비율)
	int32	_cols = 0;					// 가로 셀 수
	int32	_rows = 0;					// 세로 셀 수
	int32	_view = 1;					// 시야 반경 (셀 단위)
	size_t	_entityCount = 0;			// 등록된 엔티티 수
	bool	_dispatching = false;		// 통지 콜백 실행 중 여부 (재진입 방지)

	CVector<CVector<TEntity*>> _cells;	// 셀별 엔티티 목록
};

#endif // ndef GRIDAOI_H
