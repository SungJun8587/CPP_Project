
//***************************************************************************
// SheetConverter.h : 시트 -> JSON 변환 (헤더 해석, 셀 타입 변환, 오류 수집)
//
//***************************************************************************

#ifndef SHEETCONVERTER_H
#define SHEETCONVERTER_H

#include <Excel/XlntDefine.h>

#include "RowSchema.h"

#include <string>
#include <vector>

//***************************************************************************
// @brief 시트 하나를 {"시트이름": [ {...}, ... ]} 형태의 JSON 문자열로 변환한다.
// @details 1행은 "필드명:타입[min..max]?" 헤더이며 2행부터 데이터다.
//          타입: int, uint, float, string, bool
//          [min..max]: int/uint 값의 허용 범위. 한쪽은 생략할 수 있다. 예) uint[1..], int[-1000..1000], uint[..100]
//          enum: 타입 자리에 Enum 시트에 정의한 enum 이름을 쓴다. 예) Shape:ESkillShape  (JSON에는 멤버 이름 문자열이 들어간다)
//          ?: 선택 열. 빈 칸이면 오류 대신 그 행의 JSON 필드를 생략한다.
//          첫 칸이 '#'로 시작하는 행과 완전히 빈 행은 건너뛴다. 오류는 멈추지 않고 모두 수집한다.
//          '#' 행에 다른 칸 값도 있으면 주석이 아니라 데이터일 수 있어(예: 이름이 "#1 보스"인 행) warnings에 알린다. 주석은 첫 칸에만 쓰는 것이 관례다.
//***************************************************************************
namespace SheetConverter
{
	//***************************************************************************
	// @brief 시트를 변환합니다.
	// @param sheet     시트 이름 (UTF-8, JSON 최상위 키가 된다)
	// @param rows      시트 셀 값
	// @param errors    [in/out] 오류 메시지 목록. 이 시트의 오류를 뒤에 덧붙인다
	// @param jsonOut   [out] 변환 결과 JSON (성공 시)
	// @param rowCount  [out] 변환된 데이터 행 수 (성공 시)
	// @param schemaOut [out] 헤더에서 읽은 스키마 (성공 시). 필요 없으면 nullptr
	// @param defs      enum 열의 멤버 이름 검사에 쓰는 정의. enum 열이 없으면 nullptr 가능
	// @param warnings  [in/out] 변환은 되지만 알려야 할 내용(주석으로 보이지 않는 '#' 행)을 덧붙인다. 필요 없으면 nullptr
	// @return 오류가 하나도 없으면 true
	//***************************************************************************
	bool Convert(const std::string& sheet, const Xlnt::TRows& rows, std::vector<std::string>& errors, std::string& jsonOut, size_t& rowCount, SSheetSchema* schemaOut = nullptr, const SDefinitions* defs = nullptr, std::vector<std::string>* warnings = nullptr);

	//***************************************************************************
	// @brief 셀 하나를 필드 정의의 타입(범위 포함)으로 변환해 JSON 값 문자열로 만듭니다. (Const 시트도 같은 규칙을 쓴다)
	// @param cell 셀
	// @param def  필드 정의
	// @param defs enum 필드의 멤버 이름 검사에 쓰는 정의. enum이 아니면 nullptr 가능
	// @param json [out] JSON 값 (이미 따옴표/이스케이프 포함)
	// @param why  [out] 실패 사유
	// @return 변환했으면 true
	//***************************************************************************
	bool ConvertCellValue(const Xlnt::SCell& cell, const SFieldDef& def, const SDefinitions* defs, std::string& json, std::string& why);

	//***************************************************************************
	// @brief 비어 있는 셀인지 확인합니다. (값이 없거나 공백뿐인 문자열)
	//***************************************************************************
	bool IsBlank(const Xlnt::SCell& cell);

	//***************************************************************************
	// @brief 변환하지 않는 행인지 확인합니다. (첫 칸이 '#'로 시작하는 주석 행, 완전히 빈 행)
	//***************************************************************************
	bool IsSkipRow(const Xlnt::TRow& row);

	//***************************************************************************
	// @brief 주석 행인지 확인합니다. (첫 칸이 문자열이고 '#'로 시작)
	//***************************************************************************
	bool IsCommentRow(const Xlnt::TRow& row);

	//***************************************************************************
	// @brief 오류 메시지의 행 위치 접두사 "시트!행번호행 "을 만듭니다. (행 번호는 엑셀의 1-base 번호)
	//***************************************************************************
	std::string RowLabel(const std::string& sheet, size_t row);
}

#endif // ndef SHEETCONVERTER_H