
//***************************************************************************
// BlockMapFile.h : Block.json 읽기 (서버와 봇 클라이언트가 공유)
//
//***************************************************************************

#ifndef BLOCKMAPFILE_H
#define BLOCKMAPFILE_H

#include <memory>

#include "BlockMap.h"

//***************************************************************************
// @brief Block.json에서 한 존의 장애물을 읽어 차단 맵(CBlockMap)을 만듭니다.
// @details 형식: {"Block": [ {"ZoneId","Type","X","Y","W","H","R"}, ... ]}
//            Type = "Rect"  : X,Y = 왼쪽 아래 모서리, W,H = 너비/높이 (R은 0)
//            Type = "Circle": X,Y = 중심, R = 반경 (W,H는 0)
//          모든 존이 한 파일을 공유하므로 다른 존의 행도 먼저 검증한다(오타가 해당 존을 띄울 때서야 드러나지 않게).
//          이 함수 호출 전에 pch.h(_tstring, 로그, JSON 유틸)가 포함되어 있어야 한다.
// @param file   Block.json 경로
// @param zoneId 이 존의 ID (다른 존의 행은 검증만 하고 쓰지 않음)
// @param minX   존 영역 최소 X
// @param minY   존 영역 최소 Y
// @param maxX   존 영역 최대 X
// @param maxY   존 영역 최대 Y
// @param cellSize 차단 맵 셀 한 변의 길이
// @param out    [out] 차단 맵. 이 존의 행이 없으면 nullptr
// @param margin 장애물을 사방으로 키우는 여유 폭(엔티티 반경 대신, 0 이상). 엔티티를 점으로 다루므로 몸 크기를 이 값으로 반영한다.
//               서버와 봇이 같은 값을 써야 같은 판정이 되므로 기본값을 공용 상수로 둔다. Rect는 사방으로, Circle은 반경에 더한다
// @return 성공(이 존의 행이 없는 경우 포함) 시 true, 파일/형식/값 오류나 셀 수 초과 시 false (원인은 로그)
//***************************************************************************
bool LoadBlockMapFile(const _tstring& file, uint32 zoneId, float minX, float minY, float maxX, float maxY, float cellSize,
	std::shared_ptr<const CBlockMap>& out, float margin = CBlockMap::kDefaultMargin);

#endif // ndef BLOCKMAPFILE_H
