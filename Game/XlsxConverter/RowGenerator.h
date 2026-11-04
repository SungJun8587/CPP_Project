
//***************************************************************************
// RowGenerator.h : 시트 스키마 -> C++ 행 구조체 헤더(GameDataRows.gen.h) 생성
//
//***************************************************************************

#ifndef ROWGENERATOR_H
#define ROWGENERATOR_H

#include "RowSchema.h"

#include <string>
#include <vector>

//***************************************************************************
// @struct SGeneratedFile
// @brief 생성된 파일 하나. name은 출력 폴더 기준 파일 이름이다.
//***************************************************************************
struct SGeneratedFile
{
	std::string	name;
	std::string	content;
};

//***************************************************************************
// @brief 시트마다 <시트>.gen.h 파일 하나를 만들고, 그 안에 namespace GameDataRows { struct S<시트>Row } 가 있다.
// @details 구조체는 kFile, kTable, Specs(), ToJSON(), FromJSON()을 가지며
//          GameDataLoader의 ReadTable<TRow>가 그대로 읽을 수 있다.
//          Specs()는 헤더의 타입/범위/선택 열 표기와 항상 같다. 생성 결과는 같은 입력이면 항상 같은 바이트다.
//***************************************************************************
namespace RowGenerator
{
	//***************************************************************************
	// @brief 필드명을 C++ 멤버 이름으로 바꿉니다. (앞쪽 대문자 묶음을 소문자로: MaxHp -> maxHp, HPMax -> hpMax, Id -> id)
	//***************************************************************************
	std::string MemberName(const std::string& fieldName);

	//***************************************************************************
	// @brief 시트별 헤더 파일들을 만듭니다.
	// @param schemas 시트 스키마 (출력할 순서)
	// @param defs    Enum, Const 시트 정의. 있으면 Enums.gen.h, EnumStrings.gen.h, Consts.gen.h도 만든다
	// @param out     [out] 시트마다 <시트>.gen.h 한 파일과, 그 헤더들을 모두 포함하는 GeneratedCommon.h (UTF-8, BOM 없음, LF)
	// @param errors  [in/out] C++ 식별자로 쓸 수 없는 시트/필드 이름 등의 오류
	// @return 오류가 없으면 true
	//***************************************************************************
	bool Generate(const std::vector<SSheetSchema>& schemas, const SDefinitions& defs, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors);

	//***************************************************************************
	// @brief 시트별 C# 파일들을 만듭니다.
	// @details 시트마다 <시트>Row(속성 = 필드명 그대로, 범위 검사 Validate)와 <시트>Table({"시트": [...]} 최상위 객체)을 만든다.
	//          속성 이름이 JSON 키와 같아 System.Text.Json, Newtonsoft.Json 모두 특성 없이 읽는다.
	// @param schemas   시트 스키마
	// @param defs      Enum, Const 시트 정의. 있으면 Enums.gen.cs, Consts.gen.cs(GameConsts 클래스)도 만든다
	// @param ns        C# 네임스페이스
	// @param out       [out] 시트마다 <시트>.gen.cs 한 파일 (UTF-8, BOM 없음, LF)
	// @param errors    [in/out] C# 식별자로 쓸 수 없는 이름 등의 오류
	// @return 오류가 없으면 true
	//***************************************************************************
	bool GenerateCs(const std::vector<SSheetSchema>& schemas, const SDefinitions& defs, const std::string& ns, std::vector<SGeneratedFile>& out, std::vector<std::string>& errors);
}

#endif // ndef ROWGENERATOR_H
