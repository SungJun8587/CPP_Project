
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
#include <tuple>
#include <utility>

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

//***************************************************************************
// @brief 요청자 스냅샷 — 요청자가 응답 전에 끊겨도 다른 사용자에게 보내는 푸시에 쓸 수 있도록
//        세션은 weak_ptr로, 식별자는 값으로 보관한다. 비동기 콜백에 값으로 캡처한다.
//***************************************************************************
struct SActor
{
	std::weak_ptr<CChatSession>	session;
	PublicId					publicId{};
};

//***************************************************************************
// @brief 로그인된 세션만 허용하고 서버와 요청자 스냅샷을 만든다.
// @param session 패킷을 보낸 세션
// @param server 서버 포인터를 돌려받는다
// @param actor 요청자 스냅샷을 돌려받는다
// @return 로그인 전이거나 서버 연결이 없으면 false.
//***************************************************************************
inline bool BeginActorOp(CChatSession& session, CChatServerMain*& server, SActor& actor)
{
	server = GetLoggedInServer(session);
	if( server == nullptr )
		return false;

	actor.session = GetSessionRef(session);
	actor.publicId = session.GetPublicId();
	return true;
}

//***************************************************************************
// @brief target이 접속 중이면 그 세션에 data를 전송한다(오프라인이면 아무것도 하지 않는다).
//***************************************************************************
inline void SendToOnline(CChatServerMain* server, const PublicId& target, const void* data, uint16 size)
{
	if( auto s = server->FindOnlineSession(target) )
		s->Send(data, size);
}

//***************************************************************************
// @brief ids 중 접속 중인 모든 세션에 data를 전송한다(온라인 맵 락 1회).
// @param skip nullptr이 아니면 이 id는 건너뛴다(예: 행위자 본인 제외).
//***************************************************************************
inline void SendToOnlineMany(CChatServerMain* server, const std::vector<PublicId>& ids,
	const void* data, uint16 size, const PublicId* skip = nullptr)
{
	const auto sessions = server->FindOnlineSessions(ids);
	for( size_t i = 0; i < ids.size(); ++i )
	{
		if( sessions[i] == nullptr || (skip != nullptr && ids[i] == *skip) )
			continue;
		sessions[i]->Send(data, size);
	}
}

//***************************************************************************
// @brief 패킷 구조체 하나를 세션에 전송한다. Send(&packet, sizeof(packet))의 축약형.
// @param session 받는 세션
// @param packet 전송할 패킷(size/type이 채워져 있어야 한다)
// @return Send()의 결과
//***************************************************************************
template<typename TPacket>
bool SendPacket(CChatSession& session, const TPacket& packet)
{
	static_assert(sizeof(TPacket) <= 0xFFFF, "SendPacket: packet size exceeds uint16 range");
	return session.Send(&packet, static_cast<uint16>(sizeof(TPacket)));
}

template<typename TPacket>
bool SendPacket(const std::shared_ptr<CChatSession>& session, const TPacket& packet)
{
	return SendPacket(*session, packet);
}

template<typename TPacket>
bool SendPacket(CChatSession* session, const TPacket& packet)
{
	return SendPacket(*session, packet);
}

//***************************************************************************
// @brief 비동기 콜백을 "세션이 아직 살아 있을 때만" 실행되는 콜백으로 감싼다.
// @details DB/잡 큐 응답이 돌아오기 전에 연결이 끊길 수 있어서, 콜백마다
//          weak_ptr::lock() -> null 검사를 하던 것을 한 곳으로 모은 것이다.
//          세션이 이미 사라졌으면 handler는 호출되지 않는다(조용히 무시).
//          handler의 첫 인자로 잠근 세션(shared_ptr)이 전달되고, 나머지 인자는
//          원래 콜백의 인자가 그대로 이어진다.
//
//   server->RequestX(sessionRef, ...,
//       BindSession(sessionWeak, [](const std::shared_ptr<CChatSession>& session, ERoomResult result)
//       {
//           ...
//       }));
// @param weak 대상 세션의 weak_ptr
// @param handler (const std::shared_ptr<CChatSession>&, 콜백 인자...)를 받는 함수 객체
// @return 원래 콜백 시그니처로 호출할 수 있는 함수 객체
//***************************************************************************
template<typename THandler>
auto BindSession(std::weak_ptr<CChatSession> weak, THandler handler)
{
	return [weak = std::move(weak), handler = std::move(handler)](auto&&... args)
		{
			std::shared_ptr<CChatSession> session = weak.lock();
			if( session == nullptr )
				return;

			handler(session, std::forward<decltype(args)>(args)...);
		};
}

//***************************************************************************
// @brief DB/Redis 워커 스레드의 결과를 JobQueue 스레드에서 onComplete로 전달한다.
// @details 인자는 값으로 복사해 잡에 실어 보내므로, 호출한 스코프의 지역 변수를
//          참조하지 않는다. jobQueue가 null이거나 onComplete가 비어 있으면
//          아무것도 하지 않는다.
// @param jobQueue 결과를 넘겨받아 실행할 JobQueue
// @param onComplete JobQueue 스레드에서 호출할 완료 콜백
// @param args onComplete에 그대로 전달할 인자
//***************************************************************************
template<typename TCallback, typename... TArgs>
void PostToJobQueue(const CJobQueueRef& jobQueue, const TCallback& onComplete, TArgs&&... args)
{
	if( jobQueue == nullptr || !onComplete )
		return;

	jobQueue->DoAsync([onComplete, packed = std::make_tuple(std::forward<TArgs>(args)...)]()
		{
			std::apply(onComplete, packed);
		});
}

//***************************************************************************
// @brief 받은 인자를 가공 없이 JobQueue로 넘기는 DB 완료 콜백을 만든다.
// @details req->onComplete = MakeJobQueueDispatcher(jobQueue, onComplete); 처럼
//          쓴다. 인자를 가공해야 하면(표시용 URL 복원 등) 람다 안에서 가공한 뒤
//          PostToJobQueue()를 직접 호출한다.
// @param jobQueue 결과를 넘겨받아 실행할 JobQueue
// @param onComplete JobQueue 스레드에서 호출할 완료 콜백
// @return DB 워커 스레드에서 호출할 함수 객체
//***************************************************************************
template<typename TCallback>
auto MakeJobQueueDispatcher(CJobQueueRef jobQueue, TCallback onComplete)
{
	return [jobQueue = std::move(jobQueue), onComplete = std::move(onComplete)](auto&&... args)
		{
			PostToJobQueue(jobQueue, onComplete, std::forward<decltype(args)>(args)...);
		};
}

#endif // ndef UC_CHATHANDLERUTIL_H