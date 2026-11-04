
//***************************************************************************
// EnumStrings.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.
//
// enum 멤버 이름 <-> 문자열 변환. JSON에는 멤버 이름이 그대로 저장된다. TCHAR, _T, _tcscmp가 필요하다.
//***************************************************************************

#ifndef GAMEDATAENUMSTRINGS_GEN_H
#define GAMEDATAENUMSTRINGS_GEN_H

#include "Enums.gen.h"

namespace GameDataEnums
{
	inline const TCHAR* ToString(ESkillShape v)
	{
		switch( v )
		{
		case ESkillShape::Single: return _T("Single");
		case ESkillShape::Area: return _T("Area");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, ESkillShape& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("Single")) == 0 ) { out = ESkillShape::Single; return true; }
		if( _tcscmp(s, _T("Area")) == 0 ) { out = ESkillShape::Area; return true; }
		return false;
	}

	inline const TCHAR* ToString(EStatusKind v)
	{
		switch( v )
		{
		case EStatusKind::Dot: return _T("Dot");
		case EStatusKind::Buff: return _T("Buff");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, EStatusKind& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("Dot")) == 0 ) { out = EStatusKind::Dot; return true; }
		if( _tcscmp(s, _T("Buff")) == 0 ) { out = EStatusKind::Buff; return true; }
		return false;
	}

	inline const TCHAR* ToString(EStatusTarget v)
	{
		switch( v )
		{
		case EStatusTarget::Hit: return _T("Hit");
		case EStatusTarget::Self: return _T("Self");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, EStatusTarget& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("Hit")) == 0 ) { out = EStatusTarget::Hit; return true; }
		if( _tcscmp(s, _T("Self")) == 0 ) { out = EStatusTarget::Self; return true; }
		return false;
	}
}

#endif // ndef GAMEDATAENUMSTRINGS_GEN_H
