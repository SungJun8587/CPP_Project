
//***************************************************************************
// BlockMapFile.cpp : Block.json 읽기 구현
//
//***************************************************************************

#include "pch.h"
#include "BlockMapFile.h"
#include <ConfigFile/JSONConfigFieldReader.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace
{
	constexpr int64 kU32Max = 0xFFFFFFFFll;

	//***************************************************************************
	// @brief Block.json의 한 행. 필드 존재/타입은 읽기 전에 SFieldSpec으로 검증하므로 FromJSON은 값만 옮긴다.
	//***************************************************************************
	struct SBlockRow
	{
		uint32		zoneId = 0;
		_tstring	type;
		double		x = 0;
		double		y = 0;
		double		w = 0;
		double		h = 0;
		double		r = 0;

		static std::initializer_list<SFieldSpec> Specs()
		{
			static const std::initializer_list<SFieldSpec> specs =
			{
				{ _T("ZoneId"), EFieldType::UInt, 0, kU32Max },
				{ _T("Type"), EFieldType::String },
				{ _T("X"), EFieldType::Number },
				{ _T("Y"), EFieldType::Number },
				{ _T("W"), EFieldType::Number },
				{ _T("H"), EFieldType::Number },
				{ _T("R"), EFieldType::Number },
			};
			return specs;
		}

		void ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const
		{
			value.SetObject();
			value.AddMember(_T("ZoneId"), zoneId, allocator);
			value.AddMember(_T("Type"), _tValue(type.c_str(), allocator), allocator);
			value.AddMember(_T("X"), x, allocator);
			value.AddMember(_T("Y"), y, allocator);
			value.AddMember(_T("W"), w, allocator);
			value.AddMember(_T("H"), h, allocator);
			value.AddMember(_T("R"), r, allocator);
		}

		void FromJSON(const _tValue& value)
		{
			zoneId = value[_T("ZoneId")].GetUint();
			type = value[_T("Type")].GetString();
			x = value[_T("X")].GetDouble();
			y = value[_T("Y")].GetDouble();
			w = value[_T("W")].GetDouble();
			h = value[_T("H")].GetDouble();
			r = value[_T("R")].GetDouble();
		}
	};
}

//***************************************************************************
// @brief Block.json에서 한 존의 장애물을 읽어 차단 맵을 만든다. 자세한 규칙은 헤더를 참고.
//***************************************************************************
bool LoadBlockMapFile(const _tstring& file, uint32 zoneId, float minX, float minY, float maxX, float maxY, float cellSize,
	std::shared_ptr<const CBlockMap>& out, float margin)
{
	out.reset();

	if( !(std::isfinite(margin) && margin >= 0.0f) )
	{
		LOG_ERROR(_T("[BlockMap] margin must be finite and >= 0. margin=%f"), static_cast<double>(margin));
		return false;
	}

	CRapidJSONUtil json;
	if( !json.LoadFromFile(file.c_str()) )
	{
		LOG_ERROR(_T("[BlockMap] load failed. path=%s"), file.c_str());
		return false;
	}
	if( !json.IsObject() )
	{
		LOG_ERROR(_T("[BlockMap] top-level value must be an object. path=%s"), file.c_str());
		return false;
	}

	const std::vector<_tstring> keys = json.GetKeys();
	if( std::find(keys.begin(), keys.end(), _T("Block")) == keys.end() )
	{
		LOG_ERROR(_T("[BlockMap] table key missing. path=%s, key=Block"), file.c_str());
		return false;
	}

	std::vector<SBlockRow> rows;
	CJSONConfigFieldReader reader(json);
	reader.ReadObjects(_T("Block"), rows, SBlockRow::Specs());
	if( !reader.Validate() )
		return false;

	for( size_t i = 0; i < rows.size(); ++i )
	{
		const SBlockRow& r = rows[i];
		const uint32 row = static_cast<uint32>(i);

		if( !std::isfinite(r.x) || !std::isfinite(r.y) )
		{
			LOG_ERROR(_T("[BlockMap] row %u: X/Y must be finite"), row);
			return false;
		}

		if( r.type == _T("Rect") )
		{
			if( !(std::isfinite(r.w) && std::isfinite(r.h) && r.w > 0.0 && r.h > 0.0) )
			{
				LOG_ERROR(_T("[BlockMap] row %u: Rect needs W > 0 and H > 0"), row);
				return false;
			}
		}
		else if( r.type == _T("Circle") )
		{
			if( !(std::isfinite(r.r) && r.r > 0.0) )
			{
				LOG_ERROR(_T("[BlockMap] row %u: Circle needs R > 0"), row);
				return false;
			}
		}
		else
		{
			LOG_ERROR(_T("[BlockMap] row %u: Type must be \"Rect\" or \"Circle\". value=%s"), row, r.type.c_str());
			return false;
		}
	}

	std::shared_ptr<CBlockMap> map = std::make_shared<CBlockMap>();
	if( !map->Init(minX, minY, maxX, maxY, cellSize) )
	{
		LOG_ERROR(_T("[BlockMap] init failed (area invalid or too many cells). zoneId=%u"), zoneId);
		return false;
	}

	uint32 used = 0;
	for( const SBlockRow& r : rows )
	{
		if( r.zoneId != zoneId )
			continue;

		if( r.type == _T("Rect") )
			map->AddRect(static_cast<float>(r.x - margin), static_cast<float>(r.y - margin),
				static_cast<float>(r.w + 2.0 * margin), static_cast<float>(r.h + 2.0 * margin));
		else
			map->AddCircle(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.r + margin));
		++used;
	}

	if( used == 0 )
		return true;		// 이 존에는 장애물이 없다

	LOG_INFO(_T("[BlockMap] loaded. zoneId=%u, shapes=%u, margin=%.2f, blockedCells=%u"),
		zoneId, used, static_cast<double>(margin), static_cast<uint32>(map->GetBlockedCellCount()));
	out = std::move(map);
	return true;
}
