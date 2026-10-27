
//***************************************************************************
// RoomValidation.h : 방 이름 형식 검증
//
//***************************************************************************

#ifndef ROOMVALIDATION_H
#define ROOMVALIDATION_H

#include <string>

namespace RoomValidation
{
	//***************************************************************************
	// @brief 방 이름 형식 검증 — 빈 문자열이거나 필드 크기(NUL 포함)를 넘는 값만
	//        걸러낸다.
	// @details 닉네임처럼 문자 종류를 세밀하게 제한하지는 않는다 — 방 이름은
	//          자유도가 더 커도 무방하다. 방 생성과 방 이름 변경이 같은 규칙을 쓴다.
	// @param name UTF-8 방 이름
	// @return 사용할 수 있는 이름이면 true
	//***************************************************************************
	inline bool IsValidRoomName(const std::string& name)
	{
		return !name.empty() && name.size() < kRoomNameBytes;
	}
}

#endif // ndef ROOMVALIDATION_H