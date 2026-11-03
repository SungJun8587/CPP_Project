
//***************************************************************************
// DefinitionSheets.h : Enum, Const 시트 해석
//
//***************************************************************************

#ifndef DEFINITIONSHEETS_H
#define DEFINITIONSHEETS_H

#include <Excel/XlntDefine.h>

#include "RowSchema.h"

#include <string>
#include <vector>

//***************************************************************************
// @brief 데이터 시트가 아니라 형식을 정의하는 예약 시트.
// @details
//   Enum 시트   헤더: Enum:string  Name:string  Value:int?  Base:string?  Comment:string?
//               같은 Enum 이름의 행들이 한 enum이다. Value를 비우면 앞 멤버 + 1(첫 멤버는 0).
//               Base는 기반 정수형(uint8, uint16, uint32, int32), 생략하면 uint8. enum의 첫 행에만 써도 된다.
//   Const 시트  헤더: Name:string  Type:string  Value  Comment:string?
//               Type은 int, uint, float, string, bool. Value 열 헤더는 "Value" 또는 "Value:..."
//   첫 칸이 '#'로 시작하는 행과 빈 행은 건너뛴다. 이 시트들은 JSON 파일로 변환하지 않는다.
//***************************************************************************
namespace DefinitionSheets
{
	const char* const kEnumSheet = "Enum";
	const char* const kConstSheet = "Const";

	//***************************************************************************
	// @brief 예약 시트 이름인지 확인합니다. (Enum, Const, 그리고 생성 파일 이름과 겹치는 Enums, Consts, EnumStrings)
	//***************************************************************************
	bool IsReservedSheet(const std::string& name);

	//***************************************************************************
	// @brief Enum 시트를 읽어 defs.enums를 채웁니다. 오류는 멈추지 않고 모두 수집합니다.
	// @return 오류가 하나도 없으면 true
	//***************************************************************************
	bool ParseEnumSheet(const Xlnt::TRows& rows, SDefinitions& defs, std::vector<std::string>& errors);

	//***************************************************************************
	// @brief Const 시트를 읽어 defs.consts를 채웁니다. 오류는 멈추지 않고 모두 수집합니다.
	// @return 오류가 하나도 없으면 true
	//***************************************************************************
	bool ParseConstSheet(const Xlnt::TRows& rows, SDefinitions& defs, std::vector<std::string>& errors);
}

#endif // ndef DEFINITIONSHEETS_H