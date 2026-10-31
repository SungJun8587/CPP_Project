
//***************************************************************************
// SheetConverter.h : 시트 -> JSON 변환 (헤더 해석, 셀 타입 변환, 오류 수집)
//
//***************************************************************************

#ifndef UC_SHEETCONVERTER_H
#define UC_SHEETCONVERTER_H

#include <Excel/XlntDefine.h>

#include <string>
#include <vector>

//***************************************************************************
// @brief 시트 하나를 {"시트이름": [ {...}, ... ]} 형태의 JSON 문자열로 변환한다.
// @details 1행은 "필드명:타입" 헤더(int, uint, float, string, bool)이며 2행부터 데이터다.
//          첫 칸이 '#'로 시작하는 행과 완전히 빈 행은 건너뛴다. 오류는 멈추지 않고 모두 수집한다.
//***************************************************************************
namespace SheetConverter
{
	//***************************************************************************
	// @brief 시트를 변환합니다.
	// @param sheet    시트 이름 (UTF-8, JSON 최상위 키가 된다)
	// @param rows     시트 셀 값
	// @param errors   [in/out] 오류 메시지 목록. 이 시트의 오류를 뒤에 덧붙인다
	// @param jsonOut  [out] 변환 결과 JSON (성공 시)
	// @param rowCount [out] 변환된 데이터 행 수 (성공 시)
	// @return 오류가 하나도 없으면 true
	//***************************************************************************
	bool Convert(const std::string& sheet, const Xlnt::TRows& rows, std::vector<std::string>& errors, std::string& jsonOut, size_t& rowCount);
}

#endif // ndef UC_SHEETCONVERTER_H