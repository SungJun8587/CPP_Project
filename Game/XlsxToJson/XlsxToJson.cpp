
//***************************************************************************
// XlsxToJson.cpp : 기획 데이터 엑셀(.xlsx) -> 시트별 JSON 변환기 (C++17)
//
// 사용법:  XlsxToJson GameData.xlsx --out ../GameData [--cpp-out ../Server/Generated] [--cs-out ../Client/Generated] [--sheets Monster Spawn]
//
//   - 1행은 헤더이며 각 칸은 "필드명:타입[min..max]?" (타입: int, uint, float, string, bool).
//       [min..max]  int/uint 허용 범위, 한쪽 생략 가능.  예) uint[1..]  int[-1000..1000]  uint[..100]
//       ?           선택 열. 빈 칸이면 오류 없이 그 행의 JSON 필드를 생략한다.
//   - 2행부터 데이터. 첫 칸이 '#'로 시작하는 행과 완전히 빈 행은 건너뛴다.
//   - '_'로 시작하는 시트는 변환하지 않는다. (--sheets로 직접 지정하면 변환)
//   - 시트 이름이 곧 출력 파일 이름이고 JSON 최상위 키다. Monster 시트 -> Monster.json = {"Monster":[...]}
//   - 오류가 하나라도 있으면 모두 출력하고 아무 파일도 쓰지 않는다.
//   - 코드 생성은 출력 폴더를 준 언어만 만든다. (C++만 / C#만 / 둘 다 = 해당 옵션을 하나만 / 둘 다 지정)
//       --cpp-out <dir>        <시트>.gen.h   시트마다 한 파일 (S<시트>Row 구조체) + GeneratedCommon.h (전부 포함하는 대표 헤더)
//       --cs-out <dir>         <시트>.gen.cs  시트마다 한 파일 (<시트>Row, <시트>Table 클래스)
//       --cs-namespace <name>  C# 네임스페이스 (기본 GameDataRows)
//     생성 코드는 BOM 있는 UTF-8로 쓴다. JSON은 BOM 없는 UTF-8이다.
//   - Enum, Const 시트는 JSON이 아니라 형식 정의다. (DefinitionSheets.h 참고)
//       다른 시트 헤더의 타입으로 enum 이름을 쓴다. 예) Shape:ESkillShape  (JSON에는 멤버 이름 문자열)
//   - --incremental: 엑셀의 수정 시각/크기가 마지막 실행 때와 같고 출력 파일이 모두 있으면 엑셀을 열지 않고 끝낸다. (Pre-Build 이벤트용)
//       엑셀이 다른 프로그램에 열려 읽을 수 없으면 경고만 내고 기존 출력을 그대로 쓴다. (이전 실행과 같은 인자일 때만)
//       상태는 <--out>/.XlsxToJson.stamp 에 저장한다. (버전 관리에서 제외할 것)
//     내용이 바뀌지 않은 파일은 다시 쓰지 않아 재빌드를 일으키지 않는다. 시트가 없어지면 그 시트의 낡은 *.gen.* 파일은 지운다. --sheets와는 함께 쓸 수 없다.
//
// 종료 코드: 0 성공, 1 변환 실패(데이터 오류), 2 사용법/파일 오류.
//
// 구성:
//   XlntWorkbookReader  시트 목록/셀 값 읽기 (UtilCore\Excel, xlnt)
//   ValueFormat         값 표기 규칙            SheetConverter 시트 -> JSON 변환
//   RowGenerator        시트 스키마 -> C++/C# 코드    DefinitionSheets Enum, Const 시트
//***************************************************************************

#include "pch.h"

#include <algorithm>
#include <set>

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
		fs::path					cppOutDir;
		bool						hasCppOut = false;
		fs::path					csOutDir;
		bool						hasCsOut = false;
		std::string					csNamespace = "GameDataRows";
		std::vector<std::string>	sheets;				// UTF-8
		bool						hasSheets = false;
		bool						incremental = false;
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
			else if( arg == _T("--cpp-out") && i + 1 < argc )
			{
				opt.cppOutDir = fs::path(argv[++i]);
				opt.hasCppOut = true;
			}
			else if( arg == _T("--cs-out") && i + 1 < argc )
			{
				opt.csOutDir = fs::path(argv[++i]);
				opt.hasCsOut = true;
			}
			else if( arg == _T("--cs-namespace") && i + 1 < argc )
			{
				opt.csNamespace = TStringToUtf8(argv[++i]);
			}
			else if( arg == _T("--incremental") )
			{
				opt.incremental = true;
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
				std::fprintf(stderr, U8("알 수 없는 인자: %s\n"), TStringToUtf8(argv[i]).c_str());
				return false;
			}
		}

		if( opt.xlsxPath.empty() || !hasOut )
		{
			std::fprintf(stderr, "usage: XlsxToJson <file.xlsx> --out <dir> [--cpp-out <dir>] [--cs-out <dir> [--cs-namespace <name>]] [--sheets Name ...] [--incremental]\n");
			return false;
		}
		if( (opt.hasCppOut || opt.hasCsOut) && opt.hasSheets )
		{
			// 일부 시트만으로 헤더를 만들면 나머지 시트의 구조체가 사라져 C++ 빌드가 깨진다.
			std::fprintf(stderr, U8("--cpp-out/--cs-out은 --sheets와 함께 쓸 수 없습니다 (생성 코드는 모든 시트로 만들어야 합니다)\n"));
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

	//***************************************************************************
	// @brief 파일 내용이 text와 같은지 확인합니다. (없거나 다르면 false)
	//***************************************************************************
	bool SameContent(const fs::path& path, const std::string& text)
	{
		std::ifstream f(path, std::ios::binary);
		if( !f ) return false;
		const std::string current((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		// 저장소 설정(autocrlf 등)으로 줄바꿈만 바뀐 파일은 같은 것으로 본다. (그렇지 않으면 빌드마다 다시 써 재빌드를 일으킨다)
		const auto strip = [](std::string s) { s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); return s; };
		return strip(current) == strip(text);
	}

	//***************************************************************************
	// @brief 증분 실행 상태 파일. 첫 부분은 실행 조건(인자와 도구 빌드), "--" 줄 뒤는 만들어진 출력 파일 목록이다.
	//***************************************************************************
	const char* kStampName = ".XlsxToJson.stamp";

	std::string AbsoluteUtf8(const fs::path& p)
	{
		std::error_code ec;
		const fs::path a = fs::absolute(p, ec);
		return PathToUtf8(ec ? p : a.lexically_normal());
	}

	// 실행 파일의 수정 시각. 도구가 다시 빌드되면 같은 엑셀에서도 생성 결과가 달라질 수 있어 상태 파일에 포함한다.
	// (__TIME__ 은 XlsxToJson.cpp 를 다시 컴파일할 때만 바뀌므로 RowGenerator.cpp 같은 다른 파일만 고친 빌드를 놓친다)
	std::string ExeStamp()
	{
		std::error_code ec;
#ifdef _WIN32
		wchar_t buf[MAX_PATH * 4] = {};
		const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
		const fs::path exe = n > 0 ? fs::path(buf) : fs::path();
#else
		const fs::path exe = "/proc/self/exe";
#endif
		const auto t = exe.empty() ? fs::file_time_type() : fs::last_write_time(exe, ec);
		return ec ? std::string("unknown") : std::to_string(t.time_since_epoch().count());
	}

	// 같은 인자, 같은 도구 빌드로 만든 출력인지 가리는 문자열.
	std::string MakeSignature(const SOptions& opt)
	{
		std::string s = "XlsxToJson stamp v2 exe=" + ExeStamp() + "\n";
		s += "xlsx=" + AbsoluteUtf8(fs::path(opt.xlsxPath)) + "\n";
		s += "out=" + AbsoluteUtf8(opt.outDir) + "\n";
		if( opt.hasCppOut ) s += "cpp=" + AbsoluteUtf8(opt.cppOutDir) + "\n";
		if( opt.hasCsOut ) s += "cs=" + AbsoluteUtf8(opt.csOutDir) + "\nns=" + opt.csNamespace + "\n";
		if( opt.hasSheets ) for( const std::string& n : opt.sheets ) s += "sheet=" + n + "\n";
		return s + "--\n";
	}

	// 엑셀 파일의 수정 시각과 크기. 시각을 읽을 수 없으면 "?" (이때는 상태 파일의 값과도 다르다고 보지 않으므로 파일이 없는 경우는 읽기 단계의 오류로 드러난다)
	std::string SourceStamp(const SOptions& opt)
	{
		std::error_code ec;
		const fs::path p(opt.xlsxPath);
		const auto t = fs::last_write_time(p, ec);
		if( ec ) return "?";
		const auto size = fs::file_size(p, ec);
		return std::to_string(t.time_since_epoch().count()) + "/" + std::to_string(ec ? 0 : size);
	}

	//***************************************************************************
	// @brief 상태 파일이 현재 인자와 맞고 출력 파일이 모두 남아 있는지 확인합니다.
	// @param checkTime true면 엑셀의 수정 시각/크기가 지난 실행 때와 같을 때만 true
	//***************************************************************************
	bool StampMatches(const SOptions& opt, const std::string& signature, bool checkTime)
	{
		const fs::path stamp = opt.outDir / kStampName;
		std::ifstream f(stamp, std::ios::binary);
		if( !f ) return false;
		const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		if( text.compare(0, signature.size(), signature) != 0 ) return false;

		// 엑셀의 수정 시각/크기가 지난 실행 때와 같아야 한다. (이전 버전을 시각 보존 복사로 되돌린 경우도 다시 변환하려고 '새로움'이 아니라 '같음'으로 비교)
		size_t pos = text.find('\n', signature.size());
		if( pos == std::string::npos ) return false;
		if( checkTime && text.compare(signature.size(), pos - signature.size(), "src=" + SourceStamp(opt)) != 0 ) return false;
		++pos;

		std::error_code ec;
		while( pos < text.size() )
		{
			size_t end = text.find('\n', pos);
			if( end == std::string::npos ) end = text.size();
			if( end > pos && !fs::exists(PathFromUtf8(text.substr(pos, end - pos)), ec) ) return false;
			pos = end + 1;
		}
		return true;
	}

	//***************************************************************************
	// @brief 생성 파일 묶음을 폴더에 쓴다. 내용이 같은 파일은 쓰지 않고, 이번에 만들지 않은 낡은 생성 파일(*.gen.<ext>)은 지운다.
	// @param dir   출력 폴더 (없으면 만든다)
	// @param files 생성된 파일들 (파일마다 UTF-8 BOM을 붙여 쓴다)
	// @param ext   ".h" 또는 ".cs"
	//***************************************************************************
	void WriteGeneratedSet(const fs::path& dir, const std::vector<SGeneratedFile>& files, const std::string& ext)
	{
		fs::create_directories(dir);

		size_t written = 0, unchanged = 0;
		std::set<std::string> keep;
		for( const SGeneratedFile& f : files )
		{
			keep.insert(f.name);
			const fs::path path = dir / PathFromUtf8(f.name);
			// 생성 코드는 한글 주석이 있어 BOM 있는 UTF-8로 쓴다. (MSVC가 BOM 없는 파일을 시스템 코드 페이지로 읽는 것을 막는다)
			const std::string content = std::string("\xEF\xBB\xBF") + f.content;
			if( SameContent(path, content) ) { ++unchanged; continue; }
			WriteAtomically(path, content);
			++written;
		}

		// 지울 파일은 이름이 *.gen<ext>이고 생성기 표식이 있는 것만으로 한정한다. (손으로 만든 파일은 건드리지 않는다)
		size_t removed = 0;
		for( const fs::directory_entry& e : fs::directory_iterator(dir) )
		{
			const std::string name = PathToUtf8(e.path().filename());
			const std::string suffix = ".gen" + ext;
			if( !e.is_regular_file() || name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0 || keep.count(name) )
				continue;

			std::ifstream f(e.path(), std::ios::binary);
			char head[512] = {};
			f.read(head, sizeof(head) - 1);
			if( std::string(head).find(U8("자동 생성 파일")) == std::string::npos ) continue;

			f.close();
			fs::remove(e.path());
			++removed;
		}

		std::printf("%s: %zu written, %zu unchanged, %zu removed\n", PathToUtf8(dir).c_str(), written, unchanged, removed);
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

	const std::string signature = MakeSignature(opt);
	const std::string sourceStamp = SourceStamp(opt);	// 읽기 시작하기 전에 잡는다. (실행 중 엑셀이 바뀌면 다음 실행에서 다시 변환된다)
	if( opt.incremental && StampMatches(opt, signature, true) )
	{
		std::printf("up to date: %s\n", PathToUtf8(fs::path(opt.xlsxPath)).c_str());
		return ExitOk;
	}

	bool opened = false;
	try
	{
		CXlntWorkbookReader workbook(opt.xlsxPath);
		opened = true;

		std::vector<std::string> names;
		if( opt.hasSheets )
			names = opt.sheets;
		else
			for( const std::string& name : workbook.GetSheetNames() )
				if( name.empty() || name[0] != '_' )
					names.push_back(name);

		struct SResult { std::string name, json; size_t rowCount; SSheetSchema schema; };
		std::vector<SResult> results;
		std::vector<std::string> errors;

		// Enum, Const 시트는 형식 정의라 JSON으로 변환하지 않는다.
		// Enum은 --sheets를 줘도 항상 읽어 enum 열의 멤버 이름을 검증한다. Const는 코드 생성에만 쓴다.
		SDefinitions defs;
		{
			const std::vector<std::string>& all = workbook.GetSheetNames();
			const auto has = [&](const char* n) { return std::find(all.begin(), all.end(), n) != all.end(); };
			const auto readDef = [&](const char* sheet, bool (*parse)(const TRows&, SDefinitions&, std::vector<std::string>&))
				{
					TRows rows;
					std::string error;
					if( !workbook.ReadSheet(sheet, rows, error) ) errors.push_back(error);
					else parse(rows, defs, errors);
				};
			if( has(DefinitionSheets::kEnumSheet) )
				readDef(DefinitionSheets::kEnumSheet, &DefinitionSheets::ParseEnumSheet);
			if( has(DefinitionSheets::kConstSheet) && (opt.hasCppOut || opt.hasCsOut) )
				readDef(DefinitionSheets::kConstSheet, &DefinitionSheets::ParseConstSheet);

			std::vector<std::string> data;
			for( const std::string& name : names )
			{
				if( name == DefinitionSheets::kEnumSheet || name == DefinitionSheets::kConstSheet ) continue;
				if( DefinitionSheets::IsReservedSheet(name) ) { errors.push_back(name + U8(": 생성 파일 이름과 겹치는 예약 시트 이름입니다")); continue; }
				data.push_back(name);
			}
			names = std::move(data);
		}

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
			if( SheetConverter::Convert(name, rows, errors, result.json, result.rowCount, &result.schema, &defs) )
				results.push_back(std::move(result));
		}

		std::vector<SGeneratedFile> cppFiles, csFiles;
		if( (opt.hasCppOut || opt.hasCsOut) && errors.empty() )
		{
			std::vector<SSheetSchema> schemas;
			for( const SResult& r : results )
				schemas.push_back(r.schema);
			if( opt.hasCppOut )
				RowGenerator::Generate(schemas, defs, cppFiles, errors);
			if( opt.hasCsOut )
				RowGenerator::GenerateCs(schemas, defs, opt.csNamespace, csFiles, errors);
		}

		if( !errors.empty() )
		{
			std::fprintf(stderr, U8("변환 실패 (%zu건):\n"), errors.size());
			for( const std::string& e : errors )
				std::fprintf(stderr, "  %s\n", e.c_str());
			return ExitConvertFailed;
		}

		fs::create_directories(opt.outDir);
		std::string produced;
		for( const SResult& r : results )
		{
			const fs::path path = opt.outDir / PathFromUtf8(r.name + ".json");
			produced += AbsoluteUtf8(path) + "\n";
			WriteAtomically(path, r.json);
			std::printf("%s: %zu rows\n", PathToUtf8(path).c_str(), r.rowCount);
		}

		if( opt.hasCppOut )
			WriteGeneratedSet(opt.cppOutDir, cppFiles, ".h");
		if( opt.hasCsOut )
			WriteGeneratedSet(opt.csOutDir, csFiles, ".cs");

		if( opt.incremental )
		{
			for( const SGeneratedFile& f : cppFiles ) produced += AbsoluteUtf8(opt.cppOutDir / PathFromUtf8(f.name)) + "\n";
			for( const SGeneratedFile& f : csFiles ) produced += AbsoluteUtf8(opt.csOutDir / PathFromUtf8(f.name)) + "\n";
			WriteAtomically(opt.outDir / kStampName, signature + "src=" + sourceStamp + "\n" + produced);	// 모든 출력이 끝난 뒤 마지막에 쓴다
		}
		return ExitOk;
	}
	catch( const std::exception& e )
	{
		// 엑셀이 다른 프로그램(Excel 등)에 열려 있어 읽지 못하는 경우: 같은 인자로 만든 출력이 남아 있으면 그것을 쓴다.
		std::error_code ec;
		if( !opened && opt.incremental && fs::exists(fs::path(opt.xlsxPath), ec) && StampMatches(opt, signature, false) )
		{
			std::fprintf(stderr, U8("warning: 엑셀을 읽지 못해 이전에 생성한 파일을 그대로 사용합니다 (%s)\n"), e.what());
			return ExitOk;
		}

		std::fprintf(stderr, "error: %s\n", e.what());
		return ExitUsage;
	}
}