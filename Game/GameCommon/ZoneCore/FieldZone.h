
//***************************************************************************
// FieldZone.h : interface for the CFieldZone<TEntity> class template.
//
//***************************************************************************

#ifndef FIELDZONE_H
#define FIELDZONE_H

#include <memory>
#include <type_traits>
#include <utility>

#include "FieldZoneBase.h"

//***************************************************************************
// @class CFieldZone
// @brief 엔티티 타입을 지정할 수 있는 필드 존. CFieldZoneBase 위에 타입 안전 인터페이스를 얹은 얇은 템플릿이다.
// @tparam TEntity CZoneEntity에서 파생된 엔티티 타입 (기본값: CZoneEntity)
// @details
// 이 존에는 TEntity(또는 그 파생형)만 등록할 수 있으므로 FindEntity/ForEachInAoi/ForEachInRadius와
// 이벤트 훅(OnEntity*/OnMoveRejected)이 TEntity&를 그대로 전달한다. 호출자가 static_cast를 쓸 필요가 없다.
// 이동/AOI/틱 로직은 모두 CFieldZoneBase에 있으며 한 번만 컴파일된다.
//
// 사용 예:
//   class CMonster : public CZoneEntity { ... };
//   class CDungeonZone : public CFieldZone<CMonster> { void OnEntityMoveStarted(CMonster& m) override; };
//
// 스레드 규칙은 CFieldZoneBase와 같다. PostMove()를 제외한 모든 메서드는 존 스레드 전용이다.
//***************************************************************************
template <typename TEntity = CZoneEntity>
class CFieldZone : public CFieldZoneBase
{
	static_assert(std::is_base_of<CZoneEntity, TEntity>::value, "CFieldZone: TEntity must derive from CZoneEntity");

public:
	explicit CFieldZone(const FieldZoneConfig& config) : CFieldZoneBase(config) {}

	//***************************************************************************
	// @brief 엔티티를 존에 등록하고 AOI에 편입합니다. 소유권은 존으로 이전됩니다.
	// @param entity 등록할 엔티티 (SetSpawnPos로 위치가 설정되어 있어야 함)
	// @return 등록된 엔티티 포인터, 실패(ID 중복/영역 밖/무효 존) 시 nullptr
	//***************************************************************************
	TEntity* AddEntity(std::unique_ptr<TEntity> entity)
	{
		return static_cast<TEntity*>(CFieldZoneBase::AddEntity(std::unique_ptr<CZoneEntity>(std::move(entity))));
	}

	//***************************************************************************
	// @brief ID로 엔티티를 찾습니다. 존 스레드에서만 호출해야 합니다.
	// @param id 엔티티 ID
	// @return 엔티티 포인터, 없으면 nullptr
	//***************************************************************************
	TEntity* FindEntity(uint64 id) const
	{
		return static_cast<TEntity*>(CFieldZoneBase::FindEntity(id));
	}

	//***************************************************************************
	// @brief 엔티티의 시야 안에 있는 다른 엔티티를 순회합니다. 순회 중 추가/제거/이동 호출 금지.
	// @tparam F void(TEntity&) 형태의 호출 가능 객체
	//***************************************************************************
	template <typename F>
	void ForEachInAoi(const TEntity& entity, F&& fn) const
	{
		CFieldZoneBase::ForEachInAoi(entity, [&fn](CZoneEntity& other) { fn(static_cast<TEntity&>(other)); });
	}

	//***************************************************************************
	// @brief 원형 범위 안의 엔티티를 순회합니다. 범위 스킬/어그로 탐색용.
	// @tparam F void(TEntity&) 형태의 호출 가능 객체
	//***************************************************************************
	template <typename F>
	void ForEachInRadius(const Vec2& center, float radius, F&& fn) const
	{
		CFieldZoneBase::ForEachInRadius(center, radius, [&fn](CZoneEntity& other) { fn(static_cast<TEntity&>(other)); });
	}

protected:
	// ── 타입 안전 이벤트 훅 (CFieldZoneBase 문서 참고) ───────
	virtual void OnEntityMoveStarted(TEntity& entity) { (void)entity; }
	virtual void OnEntityMoveStopped(TEntity& entity, bool arrived) { (void)entity; (void)arrived; }
	virtual void OnEntityTeleported(TEntity& entity) { (void)entity; }
	virtual void OnMoveRejected(TEntity& entity, MoveResult reason) { (void)entity; (void)reason; }
	virtual void OnEntityDamaged(TEntity* attacker, TEntity& victim, int32 damage) { (void)attacker; (void)victim; (void)damage; }
	virtual void OnEntityDied(TEntity& victim, TEntity* killer) { (void)victim; (void)killer; }
	virtual void OnEntityRespawned(TEntity& entity) { (void)entity; }
	virtual void OnAttackRejected(TEntity& attacker, uint64 targetId, AttackResult reason) { (void)attacker; (void)targetId; (void)reason; }

private:
	void HandleMoveStarted(CZoneEntity& entity) final { OnEntityMoveStarted(static_cast<TEntity&>(entity)); }
	void HandleMoveStopped(CZoneEntity& entity, bool arrived) final { OnEntityMoveStopped(static_cast<TEntity&>(entity), arrived); }
	void HandleTeleported(CZoneEntity& entity) final { OnEntityTeleported(static_cast<TEntity&>(entity)); }
	void HandleMoveRejected(CZoneEntity& entity, MoveResult reason) final { OnMoveRejected(static_cast<TEntity&>(entity), reason); }
	void HandleDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage) final { OnEntityDamaged(static_cast<TEntity*>(attacker), static_cast<TEntity&>(victim), damage); }
	void HandleDied(CZoneEntity& victim, CZoneEntity* killer) final { OnEntityDied(static_cast<TEntity&>(victim), static_cast<TEntity*>(killer)); }
	void HandleRespawned(CZoneEntity& entity) final { OnEntityRespawned(static_cast<TEntity&>(entity)); }
	void HandleAttackRejected(CZoneEntity& attacker, uint64 targetId, AttackResult reason) final { OnAttackRejected(static_cast<TEntity&>(attacker), targetId, reason); }
};

#endif // ndef FIELDZONE_H