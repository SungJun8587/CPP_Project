
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
	inline const TCHAR* ToString(EFlag v)
	{
		switch( v )
		{
		case EFlag::None: return _T("None");
		case EFlag::A: return _T("A");
		case EFlag::B: return _T("B");
		case EFlag::High: return _T("High");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, EFlag& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("None")) == 0 ) { out = EFlag::None; return true; }
		if( _tcscmp(s, _T("A")) == 0 ) { out = EFlag::A; return true; }
		if( _tcscmp(s, _T("B")) == 0 ) { out = EFlag::B; return true; }
		if( _tcscmp(s, _T("High")) == 0 ) { out = EFlag::High; return true; }
		return false;
	}

	inline const TCHAR* ToString(EColor v)
	{
		switch( v )
		{
		case EColor::Red: return _T("Red");
		case EColor::Green: return _T("Green");
		case EColor::Blue: return _T("Blue");
		case EColor::Alpha: return _T("Alpha");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, EColor& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("Red")) == 0 ) { out = EColor::Red; return true; }
		if( _tcscmp(s, _T("Green")) == 0 ) { out = EColor::Green; return true; }
		if( _tcscmp(s, _T("Blue")) == 0 ) { out = EColor::Blue; return true; }
		if( _tcscmp(s, _T("Alpha")) == 0 ) { out = EColor::Alpha; return true; }
		return false;
	}

	inline const TCHAR* ToString(ESigned v)
	{
		switch( v )
		{
		case ESigned::Neg: return _T("Neg");
		case ESigned::Zero: return _T("Zero");
		}
		return _T("");
	}

	// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.
	inline bool TryParse(const TCHAR* s, ESigned& out)
	{
		if( s == nullptr )
			return false;
		if( _tcscmp(s, _T("Neg")) == 0 ) { out = ESigned::Neg; return true; }
		if( _tcscmp(s, _T("Zero")) == 0 ) { out = ESigned::Zero; return true; }
		return false;
	}
}

#endif // ndef GAMEDATAENUMSTRINGS_GEN_H
