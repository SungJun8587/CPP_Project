
//***************************************************************************
// XlsxToJson.cpp : 기획 데이터 엑셀(.xlsx) -> 시트별 JSON 변환기 (C++17)
//
// 사용법:  XlsxToJson GameData.xlsx --out ../GameData [--sheets Monster Spawn]
//
//   - 1행은 헤더이며 각 칸은 "필드명:타입" (타입: int, uint, float, string, bool).
//   - 2행부터 데이터. 첫 칸이 '#'로 시작하는 행과 완전히 빈 행은 건너뛴다.
//   - '_'로 시작하는 시트는 변환하지 않는다. (--sheets로 직접 지정하면 변환)
//   - 시트 이름이 곧 출력 파일 이름이고 JSON 최상위 키다. Monster 시트 -> Monster.json = {"Monster":[...]}
//   - 오류가 하나라도 있으면 모두 출력하고 아무 파일도 쓰지 않는다.
//
// 종료 코드: 0 성공, 1 변환 실패(데이터 오류), 2 사용법/파일 오류.
//
// 구성:
//   XlntWorkbookReader  시트 목록/셀 값 읽기 (UtilCore\Excel, xlnt)
//   ValueFormat         값 표기 규칙            SheetConverter 시트 -> JSON 변환
//***************************************************************************

#include "pch.h"

namespace fs = std::filesystem;
using Xlnt::CXlntWorkbookReader;
using Xlnt::TRows;

namespace
{
	enum EExitCode
	{
		ExitOk = 0,
		ExitConvertFailed = 1,
		ExitUsage = 2,
	};

	//***************************************************************************
	// @struct SOptions
	// @brief 명령행 옵션.
	//***************************************************************************
	struct SOptions
	{
		_tstring					xlsxPath;
		fs::path					outDir;
		std::vector<std::string>	sheets;				// UTF-8
		bool						hasSheets = false;
	};

	//***************************************************************************
	// @brief UTF-8 문자열을 파일 시스템 경로로 변환합니다. (시트 이름으로 출력 파일 이름을 만들 때 사용)
	//***************************************************************************
	fs::path PathFromUtf8(const std::string& utf8)
	{
		return fs::path(Utf8ToTString(utf8));
	}

	//***************************************************************************
	// @brief 파일 시스템 경로를 UTF-8 문자열로 변환합니다. (오류/진행 메시지용)
	//***************************************************************************
	std::string PathToUtf8(const fs::path& path)
	{
#ifdef _UNICODE
		return TStringToUtf8(path.native());
#else
		return TStringToUtf8(path.string());
#endif
	}

	//***************************************************************************
	// @brief 명령행을 해석합니다.
	// @param argc main의 argc
	// @param argv main의 argv
	// @param opt  [out] 해석 결과
	// @return 형식이 맞으면 true. 틀리면 사용법을 출력하고 false
	//***************************************************************************
	bool ParseArgs(int argc, TCHAR* argv[], SOptions& opt)
	{
		bool hasOut = false;

		for( int i = 1; i < argc; ++i )
		{
			const _tstring arg = argv[i];

			if( arg == _T("--out") && i + 1 < argc )
			{
				opt.outDir = fs::path(argv[++i]);
				hasOut = true;
			}
			else if( arg == _T("--sheets") )
			{
				opt.hasSheets = true;
				while( i + 1 < argc && !(argv[i + 1][0] == _T('-') && argv[i + 1][1] == _T('-')) )
					opt.sheets.push_back(TStringToUtf8(argv[++i]));
			}
			else if( opt.xlsxPath.empty() && !(arg.size() >= 2 && arg[0] == _T('-') && arg[1] == _T('-')) )
			{
				opt.xlsxPath = argv[i];
			}
			else
			{
				std::fprintf(stderr, "알 수 없는 인자: %s\n", TStringToUtf8(argv[i]).c_str());
				return false;
			}
		}

		if( opt.xlsxPath.empty() || !hasOut )
		{
			std::fprintf(stderr, "usage: XlsxToJson <file.xlsx> --out <dir> [--sheets Name ...]\n");
			return false;
		}
		return true;
	}

	//***************************************************************************
	// @brief 변환 결과를 임시 파일에 쓴 뒤 같은 폴더에서 교체합니다. (읽는 쪽이 반쯤 쓰인 파일을 보지 않게 한다)
	// @param path 최종 파일 경로
	// @param json 파일 내용 (UTF-8, BOM 없음)
	// @exception std::runtime_error 쓰기에 실패했을 때
	//***************************************************************************
	void WriteAtomically(const fs::path& path, const std::string& json)
	{
		fs::path tmp = path;
		tmp += ".tmp";

		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if( !f )
				throw std::runtime_error("cannot write: " + PathToUtf8(tmp));

			f << json;
			f.flush();
			if( !f )
				throw std::runtime_error("write failed: " + PathToUtf8(tmp));
		}

		fs::rename(tmp, path);
	}
}

//***************************************************************************
// @brief 진입점.
// @return 종료 코드 (EExitCode)
//***************************************************************************
int _tmain(int argc, TCHAR* argv[])
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	InitUtf8Console();

	SOptions opt;
	if( !ParseArgs(argc, argv, opt) )
		return ExitUsage;

	try
	{
		CXlntWorkbookReader workbook(opt.xlsxPath);

		std::vector<std::string> names;
		if( opt.hasSheets )
			names = opt.sheets;
		else
			for( const std::string& name : workbook.GetSheetNames() )
				if( name.empty() || name[0] != '_' )
					names.push_back(name);

		struct SResult { std::string name, json; size_t rowCount; };
		std::vector<SResult> results;
		std::vector<std::string> errors;

		for( const std::string& name : names )
		{
			TRows rows;
			std::string error;
			if( !workbook.ReadSheet(name, rows, error) )
			{
				errors.push_back(error);
				continue;
			}

			SResult result;
			result.name = name;
			result.rowCount = 0;
			if( SheetConverter::Convert(name, rows, errors, result.json, result.rowCount) )
				results.push_back(std::move(result));
		}

		if( !errors.empty() )
		{
			std::fprintf(stderr, "변환 실패 (%zu건):\n", errors.size());
			for( const std::string& e : errors )
				std::fprintf(stderr, "  %s\n", e.c_str());
			return ExitConvertFailed;
		}

		fs::create_directories(opt.outDir);
		for( const SResult& r : results )
		{
			const fs::path path = opt.outDir / PathFromUtf8(r.name + ".json");
			WriteAtomically(path, r.json);
			std::printf("%s: %zu rows\n", PathToUtf8(path).c_str(), r.rowCount);
		}
		return ExitOk;
	}
	catch( const std::exception& e )
	{
		std::fprintf(stderr, "error: %s\n", e.what());
		return ExitUsage;
	}
}
