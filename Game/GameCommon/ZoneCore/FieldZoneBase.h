//***************************************************************************
// FieldZoneBase.h : interface for the CFieldZoneBase class.
//
//***************************************************************************

#ifndef FIELDZONEBASE_H
#define FIELDZONEBASE_H

#include <memory>
#include <unordered_map>
#include <utility>

#include <BaseRedefineDataType.h>
#include <Memory/Containers.h>

#include "GridAOI.h"
#include "Zone.h"
#include "ZoneEntity.h"

//***************************************************************************
// @brief 존 엔티티용 그리드 AOI. 엔티티 타입을 고정한 별칭입니다.
//***************************************************************************
using CZoneGridAOI = CGridAOI<CZoneEntity>;

//***************************************************************************
// @brief 이동 요청 처리 결과.
//***************************************************************************
enum class MoveResult : uint8
{
	Ok,					// 이동 시작(또는 목적지 갱신, 동일 목적지 재요청 포함)
	EntityNotFound,		// 존에 없는 엔티티
	Immobile,			// 이동 속도가 0 이하
	OutOfBounds,		// 목적지가 존 영역 밖
	PositionMismatch,	// 클라이언트 보고 위치가 서버 위치와 허용 오차 이상 차이
	Throttled,			// 요청 간격이 minMoveIntervalMs보다 짧아 무시됨 (보정 통지 없음)
};

//***************************************************************************
// @brief 필드 존 생성 설정값.
//
// - zone              : 틱 루프 설정
// - minX~maxY         : 존 영역
// - cellSize          : AOI 셀 크기
// - viewCells         : AOI 시야 반경(셀 단위). 최소 보장 시야 = (viewCells - 2 * aoiHysteresis) * cellSize
// - aoiHysteresis     : AOI 셀 변경 히스테리시스(셀 크기 대비 비율). 셀 경계 왕복 시 통지 폭주 방지
// - posToleranceBase  : 클라이언트 위치 보고 허용 오차 기본값
// - maxLatencyMs      : 허용 오차 계산에 반영할 최대 지연(ms).
//                       허용 오차 = posToleranceBase + speed * maxLatencyMs / 1000
// - minMoveIntervalMs : 엔티티별 이동 요청 최소 간격(존 논리 시간 기준). 이보다 짧은 요청은 무시한다.
//                       요청 한 건이 주변 전원에게 브로드캐스트되므로 증폭 공격을 막는 장치다. 0이면 제한 없음
//***************************************************************************
struct FieldZoneConfig
{
	ZoneConfig zone;					// 틱 루프 설정
	float minX = 0.0f;					// 영역 최소 X
	float minY = 0.0f;					// 영역 최소 Y
	float maxX = 1000.0f;				// 영역 최대 X
	float maxY = 1000.0f;				// 영역 최대 Y
	float cellSize = 50.0f;				// AOI 셀 크기
	int32 viewCells = 1;				// AOI 시야 반경(셀)
	float aoiHysteresis = 0.1f;			// AOI 셀 변경 히스테리시스
	float posToleranceBase = 1.0f;		// 위치 허용 오차 기본값
	uint32 maxLatencyMs = 300;			// 허용 오차에 반영할 최대 지연(ms)
	uint32 minMoveIntervalMs = 50;		// 이동 요청 최소 간격(ms)
};

//***************************************************************************
// @class CFieldZoneBase
// @brief 필드 전투 존의 비템플릿 구현부. CZone의 틱 루프 위에 엔티티 관리, 서버 권위 이동, 그리드 AOI를 얹는다.
// @details 엔티티를 CZoneEntity로 다루는 타입 소거 계층이다. 직접 상속하지 말고 타입 안전한 래퍼
//          CFieldZone<TEntity>(FieldZone.h)를 사용한다. 템플릿을 얇게 유지해 구현은 한 번만 컴파일된다.
//
// 이동 방식은 "이동 시작 + 목적지 + 속도"이다. 클라이언트는 자신의 현재 위치와
// 목적지만 보고하고, 속도는 서버 스탯을 사용하며 위치 적분도 서버가 틱마다 수행한다.
// 보고된 위치가 서버 위치와 허용 오차 이상 어긋나면 거부하고 보정 통지를 보낸다.
//
// 아래 public 메서드는 PostMove()를 제외하고 모두 존 스레드에서만 호출해야 한다.
// (외부 스레드는 Post()로 작업을 넘겨 그 안에서 호출한다.)
// 파생 클래스는 소멸자에서 Stop()을 먼저 호출해, 틱 스레드가 살아 있는 동안
// 파생 멤버가 파괴되지 않게 해야 한다.
//***************************************************************************
class CFieldZoneBase : public CZone
{
public:
	explicit CFieldZoneBase(const FieldZoneConfig& config);
	~CFieldZoneBase() override;

	bool IsValid() const;

	// ── 엔티티 관리 (존 스레드 전용) ──────────────────────
	bool RemoveEntity(uint64 id);
	CZoneEntity* FindEntity(uint64 id) const;

	// ── 이동 (존 스레드 전용) ─────────────────────────────
	MoveResult RequestMove(uint64 id, const Vec2& clientPos, const Vec2& dest);
	MoveResult RequestMove(CZoneEntity& entity, const Vec2& clientPos, const Vec2& dest);
	bool StopMove(uint64 id);
	bool Teleport(uint64 id, const Vec2& pos);

	// ── 외부 스레드용 ─────────────────────────────────────
	bool PostMove(uint64 id, const Vec2& clientPos, const Vec2& dest);

	//***************************************************************************
	// @brief 엔티티의 시야 안에 있는 다른 엔티티를 순회합니다. 패킷 브로드캐스트용.
	//        순회 중 엔티티 추가/제거/이동을 호출하면 안 됩니다.
	// @tparam F void(CZoneEntity&) 형태의 호출 가능 객체
	// @param entity 기준 엔티티
	// @param fn     각 이웃에 대해 호출할 함수
	//***************************************************************************
	template <typename F>
	void ForEachInAoi(const CZoneEntity& entity, F&& fn) const
	{
		_aoi.ForEachNeighbor(entity, std::forward<F>(fn));
	}

	//***************************************************************************
	// @brief 원형 범위 안의 엔티티를 순회합니다. 범위 스킬/어그로 탐색용.
	// @tparam F void(CZoneEntity&) 형태의 호출 가능 객체
	// @param center 중심 좌표
	// @param radius 반경
	// @param fn     각 엔티티에 대해 호출할 함수
	//***************************************************************************
	template <typename F>
	void ForEachInRadius(const Vec2& center, float radius, F&& fn) const
	{
		_aoi.ForEachInRadius(center, radius, std::forward<F>(fn));
	}

	//***************************************************************************
	// @brief 존에 등록된 엔티티 수를 반환합니다.
	// @return 엔티티 수
	//***************************************************************************
	size_t GetEntityCount() const { return _entities.size(); }

protected:
	void OnUpdate(uint32 dtMs) override;

	// 엔티티 등록. 파생 템플릿(CFieldZone<TEntity>)이 타입 검사를 거쳐 호출한다.
	CZoneEntity* AddEntity(std::unique_ptr<CZoneEntity> entity);

	// 이벤트 훅의 타입 소거 버전. CFieldZone<TEntity>가 TEntity&로 변환해 OnEntity*/OnMoveRejected 훅으로 전달한다.
	virtual void HandleMoveStarted(CZoneEntity& entity);
	virtual void HandleMoveStopped(CZoneEntity& entity, bool arrived);
	virtual void HandleTeleported(CZoneEntity& entity);
	virtual void HandleMoveRejected(CZoneEntity& entity, MoveResult reason);

private:
	bool IsInBounds(const Vec2& pos) const;
	void UpdateMovement(uint32 dtMs);
	void BeginMoving(CZoneEntity* entity);
	void EndMoving(CZoneEntity* entity);

	FieldZoneConfig _fieldConfig;												// 필드 존 설정
	bool _valid;																// AOI 초기화 성공 여부
	bool _updatingMovement;														// UpdateMovement 실행 중 여부 (제거 재진입 방지)

	CZoneGridAOI _aoi;																// 그리드 AOI
	std::unordered_map<uint64, std::unique_ptr<CZoneEntity>> _entities;			// 엔티티 소유 테이블 (ID -> 엔티티)
	CVector<CZoneEntity*> _moving;												// 이동 중인 엔티티 목록 (매 틱 이 목록만 순회)
};

#endif // ndef FIELDZONEBASE_H
