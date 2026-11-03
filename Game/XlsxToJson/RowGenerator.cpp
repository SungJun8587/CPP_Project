
//***************************************************************************
// RowGenerator.cpp : C++ 행 구조체 헤더 생성 구현
//
//***************************************************************************

#include "pch.h"
#include "RowGenerator.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace
{
	const char* kKeywords[] =
	{
		"alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
		"continue", "default", "delete", "do", "double", "else", "enum", "explicit", "export", "extern", "false", "float",
		"for", "friend", "goto", "if", "inline", "int", "long", "namespace", "new", "noexcept", "not", "nullptr", "operator",
		"or", "private", "protected", "public", "register", "return", "short", "signed", "sizeof", "static", "struct",
		"switch", "template", "this", "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual",
		"void", "volatile", "while", "xor",
	};

	bool IsKeyword(const std::string& s)
	{
		for( const char* k : kKeywords )
			if( s == k ) return true;
		return false;
	}

	bool IsIdentifier(const std::string& s) { return IsPlainIdentifier(s); }

	std::string CppType(const std::string& type)
	{
		return FindTypeInfo(type)->cppType;
	}

	std::string CppFieldType(const SFieldDef& f)
	{
		return f.type == "enum" ? "GameDataEnums::" + f.enumName : CppType(f.type);
	}

	std::string DefaultInit(const SFieldDef& f, const SDefinitions& defs)
	{
		const std::string& type = f.type;
		if( type == "enum" )
		{
			const SEnumDef* e = defs.FindEnum(f.enumName);
			return " = GameDataEnums::" + f.enumName + "::" + (e && !e->members.empty() ? e->members.front().name : std::string());
		}
		return FindTypeInfo(type)->cppDefault;
	}

	// int64 리터럴. INT32_MIN처럼 부호 있는 최솟값도 안전하게 쓴다.
	std::string Literal(int64_t v) { return std::to_string(v) + "ll"; }

	std::string SpecLine(const SFieldDef& f)
	{
		const std::string name = "_T(\"" + f.name + "\")";
		std::string line = "{ " + name + ", ";

		// enum은 JSON에 멤버 이름 문자열로 저장된다.
		const STypeInfo* info = f.type == "enum" ? FindTypeInfo("string") : FindTypeInfo(f.type);
		if( info->isInteger )
		{
			const int64_t lo = f.hasMin ? f.minV : info->minV;
			const int64_t hi = f.hasMax ? f.maxV : info->maxV;
			line += std::string("EFieldType::") + info->specType + ", " + Literal(lo) + ", " + Literal(hi);
		}
		else
		{
			line += std::string("EFieldType::") + info->specType;
			if( f.optional )
				line += ", (std::numeric_limits<int64>::min)(), (std::numeric_limits<int64>::max)()";
		}

		if( f.optional ) line += ", false";
		return line + " },";
	}

	std::string ToJsonLine(const SFieldDef& f, const std::string& member)
	{
		const std::string key = "_T(\"" + f.name + "\")";
		if( f.type == "string" )
			return "value.AddMember(" + key + ", _tValue(this->" + member + ".c_str(), allocator), allocator);";
		if( f.type == "enum" )
			return "value.AddMember(" + key + ", _tValue(GameDataEnums::ToString(this->" + member + "), allocator), allocator);";
		return "value.AddMember(" + key + ", this->" + member + ", allocator);";
	}

	std::string Getter(const std::string& type)
	{
		return FindTypeInfo(type)->getter;
	}

	std::string FromJsonLine(const SFieldDef& f, const std::string& member)
	{
		const std::string key = "_T(\"" + f.name + "\")";
		if( f.type == "enum" )
		{
			// 알 수 없는 이름이면 enumOk를 내린다. (JSON을 손으로 고친 경우를 로더가 잡을 수 있게)
			const std::string parse = "!GameDataEnums::TryParse(value[" + key + "].GetString(), this->" + member + ")";
			return f.optional ? "if( value.HasMember(" + key + ") && " + parse + " ) this->enumOk = false;"
				: "if( " + parse + " ) this->enumOk = false;";
		}
		if( !f.optional )
			return "this->" + member + " = value[" + key + "]." + Getter(f.type) + ";";

		// 선택 열: 없으면 멤버의 기본값을 그대로 둔다.
		return "if( value.HasMember(" + key + ") ) this->" + member + " = value[" + key + "]." + Getter(f.type) + ";";
	}
}

std::string RowGenerator::MemberName(const std::string& fieldName)
{
	std::string s = fieldName;
	size_t run = 0;
	while( run < s.size() && std::isupper(static_cast<unsigned char>(s[run])) ) ++run;

	// 대문자 묶음 뒤에 소문자가 이어지면 마지막 대문자는 다음 단어의 시작이므로 남긴다. (HPMax -> hpMax)
	size_t lower = run;
	if( run > 1 && run < s.size() && std::islower(static_cast<unsigned char>(s[run])) ) lower = run - 1;
	for( size_t i = 0; i < lower; ++i )
		s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));

	if( IsKeyword(s) ) s += '_';
	return s;
}

namespace
{
	const char* kCsKeywords[] =
	{
		"abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const", "continue",
		"decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern", "false", "finally",
		"fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface", "internal", "is", "lock",
		"long", "namespace", "new", "null", "object", "operator", "out", "override", "params", "private", "protected",
		"public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc", "static", "string",
		"struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
		"using", "virtual", "void", "volatile", "while",
	};

	std::string CsIdent(const std::string& s)
	{
		for( const char* k : kCsKeywords )
			if( s == k ) return "@" + s;
		return s;
	}

	std::string CsType(const std::string& type)
	{
		return FindTypeInfo(type)->csType;
	}

	std::string CsFieldType(const SFieldDef& f)
	{
		return f.type == "enum" ? f.enumName : CsType(f.type);
	}

	std::string CsBase(const std::string& base)
	{
		return FindBaseInfo(base)->csType;
	}

	std::string CppBase(const std::string& base)
	{
		return FindBaseInfo(base)->cppType;
	}

	std::string CppConstType(const std::string& type)
	{
		return FindTypeInfo(type)->cppConstType;
	}

	// 값의 JSON 표기를 C++ 리터럴로. 문자열만 _T()로 감싼다.
	std::string CppConstLiteral(const SConstDef& c)
	{
		return c.type == "string" ? "_T(" + c.literal + ")" : c.literal;
	}

	std::string CommentSuffix(const std::string& comment)
	{
		return comment.empty() ? std::string() : "\t// " + comment;
	}

	//***************************************************************************
	// @brief Enum/Const 시트 이름이 C++ 규칙에 맞는지 확인합니다.
	//***************************************************************************
	bool ValidateDefinitionsForCpp(const SDefinitions& defs, std::vector<std::string>& errors)
	{
		const size_t before = errors.size();
		for( const SEnumDef& e : defs.enums )
		{
			if( IsKeyword(e.name) ) errors.push_back("Enum " + e.name + U8(": C++ 키워드는 enum 이름으로 쓸 수 없습니다"));
			for( const SEnumMember& m : e.members )
				if( IsKeyword(m.name) ) errors.push_back("Enum " + e.name + "." + m.name + U8(": C++ 키워드는 멤버 이름으로 쓸 수 없습니다"));
		}
		return errors.size() == before;
	}

	//***************************************************************************
	// @brief 의존성 없는 enum 정의 헤더(Enums.gen.h). SkillData.h처럼 JSON과 무관한 곳에서도 쓴다.
	//***************************************************************************
	std::string CppEnumsHeader(const SDefinitions& defs)
	{
		std::string t;
		t += "\n//***************************************************************************\n";
		t += U8("// Enums.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		t += "//\n";
		t += U8("// XlsxToJson이 GameData.xlsx의 Enum 시트로부터 만듭니다. 다른 헤더에 의존하지 않습니다.\n");
		t += "//***************************************************************************\n\n";
		t += "#ifndef GAMEDATAENUMS_GEN_H\n#define GAMEDATAENUMS_GEN_H\n\n#include <cstdint>\n\n";
		t += "namespace GameDataEnums\n{\n";
		bool first = true;
		for( const SEnumDef& e : defs.enums )
		{
			if( !first ) t += "\n";
			first = false;
			t += "\tenum class " + e.name + " : " + CppBase(e.base) + "\n\t{\n";
			for( const SEnumMember& m : e.members )
				t += "\t\t" + m.name + " = " + std::to_string(m.value) + "," + CommentSuffix(m.comment) + "\n";
			t += "\t};\n";
		}
		t += "}\n\n#endif // ndef GAMEDATAENUMS_GEN_H\n";
		return t;
	}

	//***************************************************************************
	// @brief enum <-> 문자열 변환 헤더(EnumStrings.gen.h). TCHAR, _T, _tcscmp가 필요하다. (pch가 제공)
	//***************************************************************************
	std::string CppEnumStringsHeader(const SDefinitions& defs)
	{
		std::string t;
		t += "\n//***************************************************************************\n";
		t += U8("// EnumStrings.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		t += "//\n";
		t += U8("// enum 멤버 이름 <-> 문자열 변환. JSON에는 멤버 이름이 그대로 저장된다. TCHAR, _T, _tcscmp가 필요하다.\n");
		t += "//***************************************************************************\n\n";
		t += "#ifndef GAMEDATAENUMSTRINGS_GEN_H\n#define GAMEDATAENUMSTRINGS_GEN_H\n\n#include \"Enums.gen.h\"\n\n";
		t += "namespace GameDataEnums\n{\n";
		bool first = true;
		for( const SEnumDef& e : defs.enums )
		{
			if( !first ) t += "\n";
			first = false;
			t += "\tinline const TCHAR* ToString(" + e.name + " v)\n\t{\n\t\tswitch( v )\n\t\t{\n";
			for( const SEnumMember& m : e.members )
				t += "\t\tcase " + e.name + "::" + m.name + ": return _T(\"" + m.name + "\");\n";
			t += "\t\t}\n\t\treturn _T(\"\");\n\t}\n\n";
			t += U8("\t// 이름이 정확히 일치하면 out을 채우고 true. 아니면 out은 그대로 두고 false.\n");
			t += "\tinline bool TryParse(const TCHAR* s, " + e.name + "& out)\n\t{\n\t\tif( s == nullptr )\n\t\t\treturn false;\n";
			for( const SEnumMember& m : e.members )
				t += "\t\tif( _tcscmp(s, _T(\"" + m.name + "\")) == 0 ) { out = " + e.name + "::" + m.name + "; return true; }\n";
			t += "\t\treturn false;\n\t}\n";
		}
		t += "}\n\n#endif // ndef GAMEDATAENUMSTRINGS_GEN_H\n";
		return t;
	}

	std::string CppConstsHeader(const SDefinitions& defs)
	{
		std::string t;
		t += "\n//***************************************************************************\n";
		t += U8("// Consts.gen.h : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		t += "//\n";
		t += U8("// XlsxToJson이 GameData.xlsx의 Const 시트로부터 만듭니다. 문자열 상수가 있으면 TCHAR, _T가 필요하다. (pch가 제공)\n");
		t += "//***************************************************************************\n\n";
		t += "#ifndef GAMEDATACONSTS_GEN_H\n#define GAMEDATACONSTS_GEN_H\n\n#include <cstdint>\n\n";
		t += "namespace GameDataConsts\n{\n";
		for( const SConstDef& c : defs.consts )
			t += "\tconstexpr " + CppConstType(c.type) + " k" + c.name + " = " + CppConstLiteral(c) + ";" + CommentSuffix(c.comment) + "\n";
		t += "}\n\n#endif // ndef GAMEDATACONSTS_GEN_H\n";
		return t;
	}

	std::string CsLiteral(const std::string& type, int64_t v)
	{
		return std::to_string(v) + (type == "uint" ? "u" : "");
	}

	// 범위 검사식. 헤더에 명시한 쪽만 만든다. (명시하지 않은 쪽은 int/uint 자체가 보장)
	std::string CsCheck(const SFieldDef& f, const std::string& prop)
	{
		std::string cond;
		if( f.hasMin ) cond += "this." + prop + " < " + CsLiteral(f.type, f.minV);
		if( f.hasMax ) cond += std::string(cond.empty() ? "" : " || ") + "this." + prop + " > " + CsLiteral(f.type, f.maxV);
		if( cond.empty() ) return "";

		const std::string range = (f.hasMin ? std::to_string(f.minV) : std::string("")) + ".." + (f.hasMax ? std::to_string(f.maxV) : std::string(""));
		return "if( " + cond + " ) errors.Add(where + \"." + f.name + "=\" + this." + prop + U8(" + \" (허용 범위 ") + range + ")\");";
	}
}

bool RowGenerator::Generate(const std::vector<SSheetSchema>& schemas, const SDefinitions& defs, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors)
{
	const size_t errBefore = errors.size();
	std::vector<SGeneratedFile> files;

	// 형식 정의(Enum, Const)가 먼저 와야 대표 헤더가 이 순서로 포함한다.
	if( !ValidateDefinitionsForCpp(defs, errors) ) return false;
	if( !defs.enums.empty() )
	{
		files.push_back({ "Enums.gen.h", CppEnumsHeader(defs) });
		files.push_back({ "EnumStrings.gen.h", CppEnumStringsHeader(defs) });
	}
	if( !defs.consts.empty() )
		files.push_back({ "Consts.gen.h", CppConstsHeader(defs) });

	std::set<std::string> sheetNames;
	for( const SSheetSchema& s : schemas )
	{
		if( !IsIdentifier(s.name) || !sheetNames.insert(s.name).second )
		{
			errors.push_back(s.name + U8(": 시트 이름은 C++ 식별자(영문/숫자/_, 숫자로 시작 불가)여야 행 구조체를 만들 수 있습니다"));
			continue;
		}

		std::vector<std::string> members;
		std::set<std::string> used = { "kFile", "kTable", "Specs", "ToJSON", "FromJSON", "enumOk" };	// 정적 멤버와 겹치면 안 된다
		bool ok = true;
		for( const SFieldDef& f : s.fields )
		{
			std::string m = IsIdentifier(f.name) ? MemberName(f.name) : std::string();
			if( m.empty() || !used.insert(m).second )
			{
				errors.push_back(s.name + "." + f.name + U8(": 필드명이 C++ 식별자가 아니거나 다른 필드와 멤버 이름이 겹칩니다 (") + (m.empty() ? f.name : m) + ")");
				ok = false;
				continue;
			}
			members.push_back(m);
		}
		if( !ok ) continue;

		std::string guard = "GAMEDATAROWS_";
		for( unsigned char c : s.name )
			guard += static_cast<char>(std::toupper(c));
		guard += "_GEN_H";

		const std::string st = "S" + s.name + "Row";
		std::string text;
		text += "\n";
		text += "//***************************************************************************\n";
		text += "// " + s.name + U8(".gen.h : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		text += "//\n";
		text += U8("// XlsxToJson이 GameData.xlsx의 ") + s.name + U8(" 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxToJson을 다시 실행하세요.\n");
		text += "// " + st + U8("는 kFile/kTable/Specs/ToJSON/FromJSON을 갖습니다.\n");
		text += "//***************************************************************************\n";
		text += "\n";
		text += "#ifndef " + guard + "\n";
		text += "#define " + guard + "\n";
		text += "\n";
		text += "#include <initializer_list>\n";
		text += "#include <limits>\n";
		if( std::any_of(s.fields.begin(), s.fields.end(), [](const SFieldDef& f) { return f.type == "enum"; }) )
		{
			text += "\n";
			text += "#include \"Enums.gen.h\"\n";
			text += "#include \"EnumStrings.gen.h\"\n";
		}
		text += "\n";
		text += "namespace GameDataRows\n";
		text += "{\n";
		text += "\t// " + s.name + U8(".json의 한 행.\n");
		text += "\tstruct " + st + "\n";
		text += "\t{\n";
		for( size_t i = 0; i < s.fields.size(); ++i )
		{
			const SFieldDef& f = s.fields[i];
			text += "\t\t" + CppFieldType(f) + "\t" + members[i] + DefaultInit(f, defs) + ";\n";
		}
		const bool hasEnum = std::any_of(s.fields.begin(), s.fields.end(), [](const SFieldDef& f) { return f.type == "enum"; });
		if( hasEnum )
			text += U8("\t\tbool\tenumOk = true;\t\t// FromJSON에서 enum 이름을 모르는 값을 만나면 false\n");
		text += "\n";
		text += "\t\tstatic constexpr const TCHAR* kFile = _T(\"" + s.name + ".json\");\n";
		text += "\t\tstatic constexpr const TCHAR* kTable = _T(\"" + s.name + "\");\n";
		text += "\n";
		text += "\t\tstatic std::initializer_list<SFieldSpec> Specs()\n";
		text += "\t\t{\n";
		text += "\t\t\tstatic const std::initializer_list<SFieldSpec> specs =\n";
		text += "\t\t\t{\n";
		for( const SFieldDef& f : s.fields )
			text += "\t\t\t\t" + SpecLine(f) + "\n";
		text += "\t\t\t};\n";
		text += "\t\t\treturn specs;\n";
		text += "\t\t}\n";
		text += "\n";
		text += "\t\tvoid ToJSON(_tValue& value, _tDocument::AllocatorType& allocator) const\n";
		text += "\t\t{\n";
		text += "\t\t\tvalue.SetObject();\n";
		for( size_t i = 0; i < s.fields.size(); ++i )
			text += "\t\t\t" + ToJsonLine(s.fields[i], members[i]) + "\n";
		text += "\t\t}\n";
		text += "\n";
		text += "\t\tvoid FromJSON(const _tValue& value)\n";
		text += "\t\t{\n";
		for( size_t i = 0; i < s.fields.size(); ++i )
			text += "\t\t\t" + FromJsonLine(s.fields[i], members[i]) + "\n";
		text += "\t\t}\n";
		text += "\t};\n";
		text += "}\n";
		text += "\n";
		text += "#endif // ndef " + guard + "\n";

		files.push_back({ s.name + ".gen.h", std::move(text) });
	}

	// 시트별 헤더를 한 번에 포함하는 대표 헤더. 사용하는 쪽은 이 파일만 include하면 되고, 시트가 늘거나 줄어도 include를 고치지 않는다.
	if( errors.size() == errBefore )
	{
		std::string text;
		text += "\n";
		text += "//***************************************************************************\n";
		text += U8("// GeneratedCommon.h : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		text += "//\n";
		text += U8("// XlsxToJson이 만든 시트별 행 구조체 헤더(<시트>.gen.h)를 한곳에서 포함합니다.\n");
		text += "//***************************************************************************\n";
		text += "\n";
		text += "#ifndef GAMEDATAROWS_GENERATEDCOMMON_H\n";
		text += "#define GAMEDATAROWS_GENERATEDCOMMON_H\n";
		text += "\n";
		for( const SGeneratedFile& f : files )
			text += "#include \"" + f.name + "\"\n";
		text += "\n";
		text += "#endif // ndef GAMEDATAROWS_GENERATEDCOMMON_H\n";
		files.push_back({ "GeneratedCommon.h", std::move(text) });
	}

	if( errors.size() != errBefore ) return false;
	out = std::move(files);
	return true;
}

bool RowGenerator::GenerateCs(const std::vector<SSheetSchema>& schemas, const SDefinitions& defs, const std::string& ns, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors)
{
	const size_t errBefore = errors.size();

	// 네임스페이스: 점으로 구분된 식별자들
	bool nsOk = !ns.empty();
	size_t b = 0;
	while( nsOk && b <= ns.size() )
	{
		size_t e = ns.find('.', b);
		if( e == std::string::npos ) e = ns.size();
		if( !IsIdentifier(ns.substr(b, e - b)) ) nsOk = false;
		b = e + 1;
	}
	if( !nsOk ) { errors.push_back("--cs-namespace '" + ns + U8("': C# 네임스페이스 형식이 아닙니다")); return false; }

	// 같은 네임스페이스에 만들어지는 형식 이름끼리 겹치면 C# 컴파일이 깨지므로 미리 막는다.
	{
		std::set<std::string> typeNames = { "GameConsts" };
		for( const SSheetSchema& s : schemas )
		{
			typeNames.insert(s.name + "Row");
			typeNames.insert(s.name + "Table");
		}
		for( const SEnumDef& e : defs.enums )
			if( !typeNames.insert(e.name).second )
				errors.push_back("Enum " + e.name + U8(": C# 형식 이름이 시트 클래스(<시트>Row, <시트>Table)나 GameConsts와 겹칩니다"));
		for( const SConstDef& c : defs.consts )
			if( c.name == "GameConsts" )
				errors.push_back("Const " + c.name + U8(": 상수 이름이 GameConsts 클래스 이름과 같을 수 없습니다"));
		if( errors.size() != errBefore ) return false;
	}

	std::vector<SGeneratedFile> files;

	const auto header = [&](const std::string& file, const std::string& what)
		{
			std::string t;
			t += "// <auto-generated>\n";
			t += "// " + file + U8(" : 자동 생성 파일 - 직접 수정하지 마세요.\n");
			t += U8("// XlsxToJson이 GameData.xlsx의 ") + what + U8(" 시트로부터 만듭니다.\n");
			t += "// </auto-generated>\n\nnamespace " + ns + "\n{\n";
			return t;
		};
	if( !defs.enums.empty() )
	{
		std::string t = header("Enums.gen.cs", "Enum");
		bool first = true;
		for( const SEnumDef& e : defs.enums )
		{
			if( !first ) t += "\n";
			first = false;
			t += "\tpublic enum " + CsIdent(e.name) + " : " + CsBase(e.base) + "\n\t{\n";
			for( const SEnumMember& m : e.members )
				t += "\t\t" + CsIdent(m.name) + " = " + std::to_string(m.value) + "," + CommentSuffix(m.comment) + "\n";
			t += "\t}\n";
		}
		t += "}\n";
		files.push_back({ "Enums.gen.cs", std::move(t) });
	}
	if( !defs.consts.empty() )
	{
		std::string t = header("Consts.gen.cs", "Const");
		t += "\tpublic static class GameConsts\n\t{\n";
		for( const SConstDef& c : defs.consts )
			t += "\t\tpublic const " + CsType(c.type) + " " + CsIdent(c.name) + " = " + c.literal + ";" + CommentSuffix(c.comment) + "\n";
		t += "\t}\n}\n";
		files.push_back({ "Consts.gen.cs", std::move(t) });
	}

	std::set<std::string> sheetNames;
	for( const SSheetSchema& s : schemas )
	{
		if( !IsIdentifier(s.name) || !sheetNames.insert(s.name).second )
		{
			errors.push_back(s.name + U8(": 시트 이름은 C# 식별자(영문/숫자/_, 숫자로 시작 불가)여야 클래스를 만들 수 있습니다"));
			continue;
		}

		const std::string row = s.name + "Row", table = s.name + "Table";
		std::set<std::string> reserved = { "FileName", "TableKey", "Validate", row, table };
		bool ok = true;
		for( const SFieldDef& f : s.fields )
		{
			if( !IsIdentifier(f.name) || !reserved.insert(f.name).second )
			{
				errors.push_back(s.name + "." + f.name + U8(": 필드명이 C# 식별자가 아니거나 예약 이름(FileName, TableKey, Validate, ") + row + U8(")과 겹칩니다"));
				ok = false;
			}
		}
		if( !ok ) continue;

		std::string text;
		text += "// <auto-generated>\n";
		text += "// " + s.name + U8(".gen.cs : 자동 생성 파일 - 직접 수정하지 마세요.\n");
		text += U8("// XlsxToJson이 GameData.xlsx의 ") + s.name + U8(" 시트 헤더로부터 만듭니다. 시트를 고치려면 엑셀을 고치고 XlsxToJson을 다시 실행하세요.\n");
		text += "// " + row + U8("(행)와 ") + table + U8("(JSON 최상위 객체)가 있습니다. 속성 이름이 JSON 키와 같아 별도 특성이 필요 없습니다.\n");
		text += "// </auto-generated>\n";
		text += "\n";
		text += "using System.Collections.Generic;\n";
		text += "\n";
		text += "namespace " + ns + "\n";
		text += "{\n";
		text += "\t/// <summary>" + s.name + U8(".json의 한 행.</summary>\n");
		text += "\tpublic sealed class " + row + "\n";
		text += "\t{\n";
		text += "\t\tpublic const string FileName = \"" + s.name + ".json\";\n";
		text += "\t\tpublic const string TableKey = \"" + s.name + "\";\n";
		text += "\n";
		for( const SFieldDef& f : s.fields )
		{
			text += "\t\tpublic " + CsFieldType(f) + " " + CsIdent(f.name) + " { get; set; }";
			if( f.type == "string" ) text += " = string.Empty;";
			if( f.type == "enum" )
			{
				const SEnumDef* e = defs.FindEnum(f.enumName);
				if( e && !e->members.empty() ) text += " = " + f.enumName + "." + CsIdent(e->members.front().name) + ";";
			}
			text += "\n";
		}
		text += "\n";
		text += U8("\t\t/// <summary>헤더의 [min..max]를 벗어난 값을 errors에 덧붙인다.</summary>\n");
		text += "\t\tpublic void Validate(string where, List<string> errors)\n";
		text += "\t\t{\n";
		for( const SFieldDef& f : s.fields )
		{
			if( f.type == "enum" || !FindTypeInfo(f.type)->isInteger ) continue;
			const std::string c = CsCheck(f, CsIdent(f.name));
			if( !c.empty() ) text += "\t\t\t" + c + "\n";
		}
		text += "\t\t}\n";
		text += "\t}\n";
		text += "\n";
		text += "\t/// <summary>" + s.name + U8(".json의 최상위 객체. {\"") + s.name + "\": [ ... ]}</summary>\n";
		text += "\tpublic sealed class " + table + "\n";
		text += "\t{\n";
		text += "\t\tpublic List<" + row + "> " + CsIdent(s.name) + " { get; set; } = new List<" + row + ">();\n";
		text += "\n";
		text += "\t\tpublic void Validate(List<string> errors)\n";
		text += "\t\t{\n";
		text += "\t\t\tfor( int i = 0; i < this." + CsIdent(s.name) + ".Count; ++i )\n";
		text += "\t\t\t\tthis." + CsIdent(s.name) + "[i].Validate(\"" + s.name + "[\" + i + \"]\", errors);\n";
		text += "\t\t}\n";
		text += "\t}\n";
		text += "}\n";

		files.push_back({ s.name + ".gen.cs", std::move(text) });
	}

	if( errors.size() != errBefore ) return false;
	out = std::move(files);
	return true;
}