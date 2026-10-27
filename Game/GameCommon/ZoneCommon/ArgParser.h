
//***************************************************************************
// ArgParser.h : interface for the CArgParser class.
//
//***************************************************************************

#ifndef ARGPARSER_H
#define ARGPARSER_H

#include <map>
#include <tchar.h>

//***************************************************************************
// @class CArgParser
// @brief "--key=value" 형식의 명령행 인자를 파싱하는 단순 파서.
// @details 값 없이 "--key"만 주면 "1"로 취급한다. 서버 설정 파일(JSON) 연동 전의 임시 설정 경로이며,
//          나중에 CServerConfig 계열로 교체해도 호출부(main)만 바꾸면 된다.
//***************************************************************************
class CArgParser
{
public:
	//***************************************************************************
	// @brief 명령행 인자를 파싱합니다.
	// @param argc main의 argc
	// @param argv main의 argv (argv[0]은 무시)
	//***************************************************************************
	CArgParser(int argc, TCHAR* argv[])
	{
		for( int i = 1; i < argc; ++i )
		{
			const _tstring arg = argv[i];
			if( arg.size() < 3 || arg[0] != _T('-') || arg[1] != _T('-') )
				continue;

			const size_t eq = arg.find(_T('='));
			if( eq == _tstring::npos )
				_values[arg.substr(2)] = _T("1");
			else
				_values[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
		}
	}

	//***************************************************************************
	// @brief 문자열 인자를 반환합니다.
	// @param key 인자 이름("--" 제외)
	// @param def 없을 때의 기본값
	//***************************************************************************
	_tstring GetString(const TCHAR* key, const TCHAR* def) const
	{
		auto it = _values.find(key);
		return it != _values.end() ? it->second : _tstring(def);
	}

	//***************************************************************************
	// @brief 부호 없는 정수 인자를 반환합니다.
	//***************************************************************************
	uint32 GetUInt(const TCHAR* key, uint32 def) const
	{
		auto it = _values.find(key);
		return it != _values.end() ? static_cast<uint32>(_tcstoul(it->second.c_str(), nullptr, 10)) : def;
	}

	//***************************************************************************
	// @brief 실수 인자를 반환합니다.
	//***************************************************************************
	float GetFloat(const TCHAR* key, float def) const
	{
		auto it = _values.find(key);
		return it != _values.end() ? static_cast<float>(_tcstod(it->second.c_str(), nullptr)) : def;
	}

private:
	std::map<_tstring, _tstring>	_values;	// 인자 이름 -> 값
};

#endif // ndef ARGPARSER_H
