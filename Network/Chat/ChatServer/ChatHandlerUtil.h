
//***************************************************************************
// ChatHandlerUtil.h : 패킷 핸들러 공통 전처리 보조 함수 (header-only)
//
// @details
// 로그인이 필요한 핸들러마다 반복되던 "로그인 확인 -> 서버 포인터 확인 ->
// 세션 shared_ptr 얻기" 전처리를 모았습니다. 핸들러는 이렇게 씁니다.
//
//   CChatServerMain* server = GetLoggedInServer(session);
//   if( server == nullptr )
//       return;
//
//   auto sessionRef = GetSessionRef(session);
//   std::weak_ptr<CChatSession> sessionWeak = sessionRef;   // 비동기 콜백에 캡처
//
// 매크로가 아니라 일반 함수라서 브레이크포인트/스텝 실행이 그대로 되고,
// 핸들러가 전처리 이후 흐름(조기 반환, 응답 패킷)을 직접 통제합니다.
// ChatSession.h / ChatServerMain.h 뒤에 include 되어야 합니다.
//***************************************************************************

#ifndef UC_CHATHANDLERUTIL_H
#define UC_CHATHANDLERUTIL_H

#include <memory>

//***************************************************************************
// @brief 로그인된 세션이면 그 세션이 속한 서버를 돌려준다.
// @param session 패킷을 보낸 세션
// @return 서버 포인터. 로그인 전이거나 서버 연결이 없으면 nullptr.
//***************************************************************************
inline CChatServerMain* GetLoggedInServer(CChatSession& session)
{
	if( !session.IsLoggedIn() )
		return nullptr;

	return session.GetServer();
}

//***************************************************************************
// @brief 세션 자신의 shared_ptr을 CChatSession 타입으로 얻는다.
// @details 비동기 콜백이 세션 수명을 연장하거나(shared_ptr) 약하게 참조하려면
//          (weak_ptr로 변환) 필요하다. 세션이 shared_ptr로 관리되고 있을 때만
//          호출해야 한다(핸들러 호출 시점에는 항상 그렇다).
// @param session 대상 세션
// @return 세션의 shared_ptr
//***************************************************************************
inline std::shared_ptr<CChatSession> GetSessionRef(CChatSession& session)
{
	return std::static_pointer_cast<CChatSession>(session.shared_from_this());
}

#endif // ndef UC_CHATHANDLERUTIL_H