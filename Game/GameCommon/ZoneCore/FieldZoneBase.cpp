
//***************************************************************************
// FieldZoneBase.cpp : implementation of the CFieldZoneBase class.
//
//***************************************************************************

#include "pch.h"
#include "FieldZoneBase.h"

#include <algorithm>
#include <cassert>
#include <cmath>

//***************************************************************************
// @brief 필드 존을 생성하고 AOI 격자를 구성합니다. 스레드는 Start() 호출 시 시작됩니다.
// @param config 필드 존 설정
//***************************************************************************
CFieldZoneBase::CFieldZoneBase(const FieldZoneConfig& config)
	: CZone(config.zone)
	, _fieldConfig(config)
	, _valid(false)
	, _updatingMovement(false)
{
	if( _fieldConfig.minDamage < 1 )
		_fieldConfig.minDamage = 1;

	_valid = _aoi.Init(config.minX, config.minY, config.maxX, config.maxY, config.cellSize, config.viewCells, config.aoiHysteresis);
	assert(_valid && "CFieldZoneBase: invalid AOI configuration");

	_moving.reserve(1024);
}

//***************************************************************************
// @brief 소멸자. 틱 스레드가 이 객체의 멤버를 사용하지 않도록 먼저 정지시킵니다.
//***************************************************************************
CFieldZoneBase::~CFieldZoneBase()
{
	Stop();
}

//***************************************************************************
// @brief AOI 초기화가 정상적으로 되었는지 확인합니다. Start() 전에 확인하세요.
// @return 유효하면 true
//***************************************************************************
bool CFieldZoneBase::IsValid() const
{
	return _valid;
}

//***************************************************************************
// @brief 엔티티를 존에 등록하고 AOI에 편입합니다(시야 안 엔티티와 양방향 진입 통지).
//        소유권은 존으로 이전됩니다. 존 스레드에서만 호출해야 합니다.
// @param entity 등록할 엔티티 (SetSpawnPos로 위치가 설정되어 있어야 함)
// @return 등록된 엔티티 포인터, 실패(ID 중복/영역 밖/무효 존) 시 nullptr
//***************************************************************************
CZoneEntity* CFieldZoneBase::AddEntity(std::unique_ptr<CZoneEntity> entity)
{
	assert(CanTouchZoneState());

	if( !_valid || !entity || !IsInBounds(entity->_pos) )
		return nullptr;

	const uint64 id = entity->GetId();
	if( _entities.find(id) != _entities.end() )
		return nullptr;

	CZoneEntity* raw = entity.get();
	raw->_dest = raw->_pos;
	raw->_moving = false;
	raw->_inZone = true;
	raw->_moveGate.Reset();
	raw->_attackGate.Reset();
	raw->_invulnerableUntilMs = 0;
	raw->_dead = false;
	raw->_hp = raw->_stats.maxHp;
	raw->_nextAttackMs = GetNowMs();
	raw->_respawnTimerId = 0;

	_entities.emplace(id, std::move(entity));
	_aoi.Add(raw);
	return raw;
}

//***************************************************************************
// @brief 엔티티를 존에서 제거합니다. 이동 중이면 중단하고, AOI에서 양방향 이탈 통지를 보낸 뒤 해제합니다.
//        엔티티를 빼기 직전에 HandleEntityRemoved()를 호출해, 엔티티를 가리키는 외부 목록을 정리할 수 있게 합니다.
//        이동 갱신/AOI 콜백 안에서는 호출할 수 없습니다. 존 스레드에서만 호출해야 합니다.
// @param id 제거할 엔티티 ID
// @return 제거했으면 true, 없으면 false
//***************************************************************************
bool CFieldZoneBase::RemoveEntity(uint64 id)
{
	assert(CanTouchZoneState());
	assert(!_updatingMovement && "CFieldZoneBase::RemoveEntity called during movement update");

	auto it = _entities.find(id);
	if( it == _entities.end() )
		return false;

	CZoneEntity* entity = it->second.get();

	if( entity->_moving )
		EndMoving(entity);

	if( entity->_respawnTimerId != 0 )
	{
		CancelTimer(entity->_respawnTimerId);
		entity->_respawnTimerId = 0;
	}

	HandleEntityRemoved(*entity);

	_aoi.Remove(entity);
	_entities.erase(it);
	return true;
}

//***************************************************************************
// @brief ID로 엔티티를 찾습니다. 존 스레드에서만 호출해야 합니다.
// @param id 엔티티 ID
// @return 엔티티 포인터, 없으면 nullptr
//***************************************************************************
CZoneEntity* CFieldZoneBase::FindEntity(uint64 id) const
{
	auto it = _entities.find(id);
	return it != _entities.end() ? it->second.get() : nullptr;
}

//***************************************************************************
// @brief ID로 엔티티를 찾아 이동 요청을 처리합니다. 자세한 규칙은 엔티티 버전을 참고하세요.
// @param id        이동 요청 엔티티 ID
// @param clientPos 클라이언트가 보고한 현재 위치
// @param dest      목적지
// @return 처리 결과
//***************************************************************************
MoveResult CFieldZoneBase::RequestMove(uint64 id, const Vec2& clientPos, const Vec2& dest)
{
	assert(CanTouchZoneState());

	CZoneEntity* entity = FindEntity(id);
	if( entity == nullptr )
		return MoveResult::EntityNotFound;

	return RequestMove(*entity, clientPos, dest);
}

//***************************************************************************
// @brief 클라이언트의 이동 요청을 검증하고 이동을 시작합니다. (호출자가 엔티티를 이미 알고 있는 경우의 조회 생략 버전)
//        - 최소 요청 간격(minMoveIntervalMs)보다 짧은 요청은 보정 통지 없이 무시한다(브로드캐스트 증폭 차단).
//        - 속도는 서버 스탯을 사용하므로 스피드핵은 의미가 없다.
//        - 클라이언트 보고 위치가 서버 위치와 허용 오차(기본값 + 속도*지연) 이상 어긋나면
//          거부하고 HandleMoveRejected()로 보정 통지를 보낸다. (텔레포트 핵 차단)
//        - 이미 같은 목적지로 이동 중이면 아무것도 하지 않는다(중복 브로드캐스트 방지).
//        이미 이동 중이면 목적지만 갱신한다. 존 스레드에서만 호출해야 하며, entity는 이 존에 속해야 한다.
// @param entity    이동 요청 엔티티 (이 존에 등록된 엔티티)
// @param clientPos 클라이언트가 보고한 현재 위치
// @param dest      목적지
// @return 처리 결과
//***************************************************************************
MoveResult CFieldZoneBase::RequestMove(CZoneEntity& entity, const Vec2& clientPos, const Vec2& dest)
{
	assert(CanTouchZoneState());
	assert(entity._inZone);

	// 스로틀을 먼저 적용한다. 죽은 클라이언트가 이동 요청을 쏟아내도 보정 통지가 요청 수만큼 나가지 않게 한다.
	if( !entity._moveGate.TryPass(GetNowMs(), _fieldConfig.minMoveIntervalMs) )
		return MoveResult::Throttled;

	if( entity._dead )
	{
		HandleMoveRejected(entity, MoveResult::Dead);
		return MoveResult::Dead;
	}

	if( !(entity._speed > 0.0f) )		// NaN도 이동 불가로 본다
	{
		HandleMoveRejected(entity, MoveResult::Immobile);
		return MoveResult::Immobile;
	}

	if( !IsInBounds(dest) )
	{
		HandleMoveRejected(entity, MoveResult::OutOfBounds);
		return MoveResult::OutOfBounds;
	}

	const float tolerance = _fieldConfig.posToleranceBase
		+ entity._speed * static_cast<float>(_fieldConfig.maxLatencyMs) * 0.001f;

	// NaN/Inf 좌표는 대소 비교가 항상 false가 되어 검증을 통과해 버리므로, "허용 오차 이내"를 긍정형으로 검사한다.
	if( !(DistanceSq(clientPos, entity._pos) <= tolerance * tolerance) )
	{
		HandleMoveRejected(entity, MoveResult::PositionMismatch);
		return MoveResult::PositionMismatch;
	}

	OrderMove(entity, dest);
	return MoveResult::Ok;
}

//***************************************************************************
// @brief 검증을 마친 이동 명령을 실행합니다. 클라이언트 요청(RequestMove)과 서버 주도 이동(MoveEntity)이 공유합니다.
//        - 이미 목적지에 있으면 이동하지 않고 정지 처리한다.
//        - 같은 목적지로 이미 이동 중이면 알릴 변화가 없으므로 아무것도 하지 않는다(중복 브로드캐스트 방지).
//        - 그 외에는 목적지를 갱신하고 이동 시작을 통지한다.
// @param entity 이동시킬 엔티티 (생존, 이동 속도 > 0, 목적지가 영역 안임이 검증된 상태)
// @param dest   목적지
//***************************************************************************
void CFieldZoneBase::OrderMove(CZoneEntity& entity, const Vec2& dest)
{
	if( DistanceSq(dest, entity._pos) < 1e-4f )
	{
		StopMove(entity.GetId());
		return;
	}

	if( entity._moving && DistanceSq(dest, entity._dest) < 1e-4f )
		return;

	entity._dest = dest;

	if( !entity._moving )
		BeginMoving(&entity);

	HandleMoveStarted(entity);
}

//***************************************************************************
// @brief 서버가 엔티티를 목적지로 걸어가게 합니다. (몬스터 AI, 스크립트 등 클라이언트 요청이 아닌 이동)
//        클라이언트 요청과 달리 위치 보고 검증과 요청 간격 제한이 없고, 호출자가 서버 코드이므로 보정 통지도 없다.
//        이동 갱신 중에는 호출할 수 없고 존 스레드에서만 호출해야 한다.
// @param entity 이동시킬 엔티티 (이 존에 등록된 엔티티)
// @param dest   목적지
// @return 이동을 시작했거나 이미 그 목적지로 이동 중이면 true, 사망/이동 불가/영역 밖이면 false
//***************************************************************************
bool CFieldZoneBase::MoveEntity(CZoneEntity& entity, const Vec2& dest)
{
	assert(CanTouchZoneState());
	assert(entity._inZone);
	assert(!_updatingMovement && "CFieldZoneBase::MoveEntity called during movement update");

	if( entity._dead || !(entity._speed > 0.0f) || !IsInBounds(dest) )
		return false;

	OrderMove(entity, dest);
	return true;
}

//***************************************************************************
// @brief 이동 중인 엔티티를 현재 서버 위치에서 정지시킵니다. 존 스레드에서만 호출해야 합니다.
// @param id 엔티티 ID
// @return 이동 중이어서 정지했으면 true
//***************************************************************************
bool CFieldZoneBase::StopMove(uint64 id)
{
	assert(CanTouchZoneState());

	CZoneEntity* entity = FindEntity(id);
	if( entity == nullptr || !entity->_moving )
		return false;

	entity->_dest = entity->_pos;
	EndMoving(entity);
	HandleMoveStopped(*entity, false);
	return true;
}

//***************************************************************************
// @brief 엔티티를 지정 위치로 즉시 이동시킵니다. (스킬, 리스폰, 서버 보정 등 서버 주도 이동)
//        이동 중이면 중단하며, AOI는 셀 변경 규칙에 따라 갱신됩니다.
//        존 스레드에서만 호출해야 합니다.
// @param id  엔티티 ID
// @param pos 이동할 좌표
// @return 성공 시 true, 엔티티가 없거나 영역 밖이면 false
//***************************************************************************
bool CFieldZoneBase::Teleport(uint64 id, const Vec2& pos)
{
	assert(CanTouchZoneState());

	CZoneEntity* entity = FindEntity(id);
	if( entity == nullptr )
		return false;

	return MoveTo(entity, pos, true);
}

//***************************************************************************
// @brief 엔티티를 지정 위치로 즉시 옮기는 내부 구현입니다. 이동 중이면 중단하고 AOI를 갱신합니다.
// @param entity 대상 엔티티
// @param pos    이동할 좌표
// @param notify true면 이동 중단/순간이동 훅을 호출한다. 리스폰은 별도 훅으로 알리므로 false를 쓴다
// @return 성공 시 true, 영역 밖이면 false
//***************************************************************************
bool CFieldZoneBase::MoveTo(CZoneEntity* entity, const Vec2& pos, bool notify)
{
	assert(!_updatingMovement && "CFieldZoneBase::MoveTo called during movement update");

	if( !IsInBounds(pos) )
		return false;

	const bool wasMoving = entity->_moving;
	if( wasMoving )
		EndMoving(entity);

	entity->_pos = pos;
	entity->_dest = pos;
	_aoi.Update(entity);

	if( notify )
	{
		if( wasMoving )
			HandleMoveStopped(*entity, false);

		HandleTeleported(*entity);
	}
	return true;
}

//***************************************************************************
// @brief 외부 스레드(IOCP 워커)에서 이동 요청을 존으로 넘깁니다. 스레드 안전.
// @param id        이동 요청 엔티티 ID
// @param clientPos 클라이언트가 보고한 현재 위치
// @param dest      목적지
// @return 입력 큐 적재 성공 시 true
//***************************************************************************
bool CFieldZoneBase::PostMove(uint64 id, const Vec2& clientPos, const Vec2& dest)
{
	return Post([id, clientPos, dest](CZone& zone)
		{
			static_cast<CFieldZoneBase&>(zone).RequestMove(id, clientPos, dest);
		});
}

//***************************************************************************
// @brief 매 틱 호출됩니다. 현재는 이동 적분만 수행하며, 이후 단계의 시뮬레이션을 이어서 호출합니다.
// @param dtMs 고정 틱 간격(ms)
//***************************************************************************
void CFieldZoneBase::OnUpdate(uint32 dtMs)
{
	UpdateMovement(dtMs);
}

//***************************************************************************
// @brief 엔티티가 존에서 제거되기 직전에 호출됩니다. 엔티티는 아직 AOI와 엔티티 테이블에 남아 있습니다.
//        재정의하여 엔티티를 가리키는 외부 목록(몬스터 AI 활성 목록 등)에서 뺍니다.
// @param entity 제거될 엔티티
//***************************************************************************
void CFieldZoneBase::HandleEntityRemoved(CZoneEntity& entity)
{
	(void)entity;
}

//***************************************************************************
// @brief 이동이 시작(또는 목적지가 갱신)되었을 때 호출됩니다.
//        재정의하여 AOI 내 플레이어에게 이동 패킷(시작 위치, 목적지, 속도, 서버 시각)을
//        ForEachInAoi()로 브로드캐스트합니다. 이동 중인 엔티티 목록 변경은 허용되지 않습니다.
// @param entity 이동을 시작한 엔티티
//***************************************************************************
void CFieldZoneBase::HandleMoveStarted(CZoneEntity& entity)
{
	(void)entity;
}

//***************************************************************************
// @brief 이동이 끝났을 때 호출됩니다. 이동 갱신 중 호출될 수 있으므로 RemoveEntity/Teleport는 금지됩니다.
// @param entity  이동을 마친 엔티티
// @param arrived 목적지에 도착해 끝났으면 true, 중단(StopMove/순간이동)이면 false
//***************************************************************************
void CFieldZoneBase::HandleMoveStopped(CZoneEntity& entity, bool arrived)
{
	(void)entity;
	(void)arrived;
}

//***************************************************************************
// @brief 순간이동이 완료되었을 때 호출됩니다. 재정의하여 위치 보정 패킷을 전송합니다.
// @param entity 순간이동한 엔티티
//***************************************************************************
void CFieldZoneBase::HandleTeleported(CZoneEntity& entity)
{
	(void)entity;
}

//***************************************************************************
// @brief 이동 요청이 거부되었을 때 호출됩니다. (Throttled는 호출되지 않음)
//        재정의하여 해당 클라이언트에 서버 위치(GetPos)로 보정하라는 패킷을 전송합니다.
// @param entity 요청한 엔티티
// @param reason 거부 사유
//***************************************************************************
void CFieldZoneBase::HandleMoveRejected(CZoneEntity& entity, MoveResult reason)
{
	(void)entity;
	(void)reason;
}

//***************************************************************************
// @brief ID로 공격자를 찾아 기본 공격을 처리합니다. 자세한 규칙은 엔티티 버전을 참고하세요.
// @param attackerId 공격자 엔티티 ID
// @param targetId   대상 엔티티 ID
// @return 처리 결과
//***************************************************************************
AttackResult CFieldZoneBase::RequestAttack(uint64 attackerId, uint64 targetId)
{
	assert(CanTouchZoneState());

	CZoneEntity* attacker = FindEntity(attackerId);
	if( attacker == nullptr )
		return AttackResult::EntityNotFound;

	return RequestAttack(*attacker, targetId);
}

//***************************************************************************
// @brief 클라이언트의 기본 공격 요청을 서버 권위로 검증하고 피해를 적용합니다.
//        검증 순서(앞 단계가 싸고 클라이언트가 위조하기 쉬운 값부터 거른다):
//        1) 같은 틱의 중복 요청은 조용히 무시한다(Throttled) - 요청 폭주 방지
//        2) 공격자 생존 -> 3) 쿨다운 -> 4) 대상 조회 -> 5) 사거리 -> 6) 대상 생존 -> 7) 공격 가능 대상(CanAttack) -> 8) 무적
//        사거리 검사를 대상 상태 검사보다 앞에 둬서, 멀리 있는 엔티티의 존재/생사 여부가 응답으로 새지 않게 한다.
//        (대상이 없는 경우와 사거리 밖은 클라이언트에 같은 사유로 통지된다.)
//        쿨다운은 틱 한 개만큼 여유를 둬서 틱 경계 지터로 정상 요청이 거절되지 않게 하며, 다음 공격 시각을
//        max(다음 시각, 지금) + 간격으로 갱신해 여유분이 누적(드리프트)되지 않게 한다.
//        사거리는 이동 중인 양쪽의 지연 보정(속도*maxLatencyMs)과 위치 허용 오차를 더해 판정한다.
//        피해량은 CalcDamage()가 정하고 적용은 DealDamage()가 맡는다.
//        거부된 요청(Cooldown/Throttled 제외)은 HandleAttackRejected()로 알리고, 쿨다운은 소모하지 않는다.
//        공격에 성공하면 공격자 본인의 무적은 해제된다(무적 상태로 일방 공격하는 것을 막는다).
//        존 스레드에서만 호출해야 하며, attacker는 이 존에 속해야 한다.
// @param attacker 공격자 (이 존에 등록된 엔티티)
// @param targetId 대상 엔티티 ID
// @return 처리 결과
//***************************************************************************
AttackResult CFieldZoneBase::RequestAttack(CZoneEntity& attacker, uint64 targetId)
{
	assert(CanTouchZoneState());
	assert(attacker._inZone);
	assert(!_updatingMovement && "CFieldZoneBase::RequestAttack called during movement update");

	const uint64 nowMs = GetNowMs();
	if( !attacker._attackGate.TryPass(nowMs, 1) )
		return AttackResult::Throttled;

	if( attacker._dead )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::AttackerDead);
		return AttackResult::AttackerDead;
	}

	if( nowMs + GetTickIntervalMs() < attacker._nextAttackMs )
		return AttackResult::Cooldown;

	CZoneEntity* target = FindEntity(targetId);
	if( target == nullptr )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::TargetNotFound);
		return AttackResult::TargetNotFound;
	}

	float moveSpeed = 0.0f;
	if( attacker._moving )
		moveSpeed += attacker._speed;
	if( target->_moving )
		moveSpeed += target->_speed;

	const float reach = attacker._stats.attackRange + _fieldConfig.posToleranceBase
		+ moveSpeed * static_cast<float>(_fieldConfig.maxLatencyMs) * 0.001f;

	// NaN 좌표가 들어와도 거부되도록 "사거리 이내"를 긍정형으로 검사한다.
	if( !(DistanceSq(attacker._pos, target->_pos) <= reach * reach) )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::OutOfRange);
		return AttackResult::OutOfRange;
	}

	if( target->_dead )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::TargetDead);
		return AttackResult::TargetDead;
	}

	if( !CanAttack(attacker, *target) )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::InvalidTarget);
		return AttackResult::InvalidTarget;
	}

	if( IsInvulnerable(*target) )
	{
		HandleAttackRejected(attacker, targetId, AttackResult::TargetInvulnerable);
		return AttackResult::TargetInvulnerable;
	}

	attacker._nextAttackMs = (attacker._nextAttackMs > nowMs ? attacker._nextAttackMs : nowMs) + attacker._stats.attackIntervalMs;
	attacker._invulnerableUntilMs = 0;

	DealDamage(*target, &attacker, CalcDamage(attacker, *target));
	return AttackResult::Ok;
}

//***************************************************************************
// @brief 기본 공격의 피해량 공식입니다. max(minDamage, 공격력 - 방어력)이며 64비트로 계산해 오버플로를 막습니다.
//        재정의하여 관통/치명타/속성 보정 등을 추가합니다.
// @param attacker 공격자
// @param victim   피해자
// @return 피해량 (minDamage 이상)
//***************************************************************************
int32 CFieldZoneBase::CalcDamage(const CZoneEntity& attacker, const CZoneEntity& victim) const
{
	int64 damage = static_cast<int64>(attacker._stats.attack) - static_cast<int64>(victim._stats.defense);
	if( damage < _fieldConfig.minDamage )
		damage = _fieldConfig.minDamage;
	if( damage > INT32_MAX )
		damage = INT32_MAX;

	return static_cast<int32>(damage);
}

//***************************************************************************
// @brief 체력 차감, 피해 훅, 사망 처리를 하는 공용 피해 적용 경로입니다. 기본 공격, 스킬, DoT, 환경 피해가 모두 이 함수를 지납니다.
//        죽었거나 무적인 대상, 0 이하의 피해는 무시합니다. 체력은 0 아래로 내려가지 않습니다.
//        이동 갱신 중에는 호출할 수 없고 존 스레드에서만 호출해야 합니다.
// @param victim 피해자 (이 존에 등록된 엔티티)
// @param source 피해를 준 엔티티. 공격자가 없거나 이미 퇴장했으면 nullptr (DoT는 틱마다 FindEntity로 다시 조회해 넘긴다)
// @param amount 피해량
// @return 피해를 적용했으면 true
//***************************************************************************
bool CFieldZoneBase::DealDamage(CZoneEntity& victim, CZoneEntity* source, int32 amount)
{
	assert(CanTouchZoneState());
	assert(victim._inZone);
	assert(!_updatingMovement && "CFieldZoneBase::DealDamage called during movement update");

	if( amount <= 0 || victim._dead || IsInvulnerable(victim) )
		return false;

	victim._hp = amount >= victim._hp ? 0 : victim._hp - amount;

	HandleDamaged(source, victim, amount);

	if( victim._hp == 0 )
		Kill(victim, source);

	return true;
}

//***************************************************************************
// @brief 체력을 회복합니다. 최대 체력을 넘지 않으며, 죽은 엔티티나 이미 가득 찬 엔티티는 무시합니다.
//        이동 갱신 중에도 호출할 수 있고(위치/이동 목록을 건드리지 않음) 존 스레드에서만 호출해야 합니다.
// @param entity 회복할 엔티티 (이 존에 등록된 엔티티)
// @param amount 회복량
// @return 실제로 회복했으면 true
//***************************************************************************
bool CFieldZoneBase::Heal(CZoneEntity& entity, int32 amount)
{
	assert(CanTouchZoneState());
	assert(entity._inZone);

	if( amount <= 0 || entity._dead || entity._hp >= entity._stats.maxHp )
		return false;

	const int64 healed = (std::min)(static_cast<int64>(amount), static_cast<int64>(entity._stats.maxHp) - entity._hp);
	entity._hp += static_cast<int32>(healed);

	HandleHealed(entity, static_cast<int32>(healed));
	return true;
}

//***************************************************************************
// @brief 엔티티가 무적 상태인지 확인합니다.
// @param entity 확인할 엔티티
// @return 무적이면 true
//***************************************************************************
bool CFieldZoneBase::IsInvulnerable(const CZoneEntity& entity) const
{
	return GetNowMs() < entity._invulnerableUntilMs;
}

//***************************************************************************
// @brief AOI가 모든 방향에서 보장하는 최소 시야 거리를 반환합니다. (viewCells - 2 * aoiHysteresis) * cellSize
//        AOI 설정이 유효하지 않은 존은 아무 거리도 보장하지 못하므로 0입니다.
// @return 최소 보장 시야 거리 (units)
//***************************************************************************
float CFieldZoneBase::GetGuaranteedViewRange() const
{
	return _valid ? _aoi.GetGuaranteedViewRadius() : 0.0f;
}

//***************************************************************************
// @brief 기본 공격 사거리가 AOI 시야 안에 들어오는지 확인합니다.
//        RequestAttack()의 실제 판정 거리의 최대치(사거리 + 위치 허용 오차 + 양쪽이 모두 최고 속도로 이동 중일 때의
//        지연 보정 2 * 속도 * maxLatencyMs)가 최소 보장 시야 이하여야 합니다.
//        시야보다 긴 판정 거리는 보이지 않는 대상을 공격하게 하고 피해 통지가 시야 밖으로 나가므로 구동 시 거부해야 합니다.
// @param attackRange  확인할 사거리
// @param maxMoveSpeed 이 사거리를 쓰는 엔티티와 그 대상이 가질 수 있는 최대 이동 속도 (units/sec)
// @return 지원하면 true
//***************************************************************************
bool CFieldZoneBase::IsAttackRangeSupported(float attackRange, float maxMoveSpeed) const
{
	const float worstReach = attackRange + _fieldConfig.posToleranceBase
		+ 2.0f * maxMoveSpeed * static_cast<float>(_fieldConfig.maxLatencyMs) * 0.001f;

	return worstReach <= GetGuaranteedViewRange();
}

//***************************************************************************
// @brief 엔티티를 사망 처리합니다. 이동 중이면 멈추고, 사망 훅 호출 후 자동 리스폰을 예약합니다.
// @param victim 사망하는 엔티티
// @param killer 마지막 일격을 가한 엔티티 (없으면 nullptr)
//***************************************************************************
void CFieldZoneBase::Kill(CZoneEntity& victim, CZoneEntity* killer)
{
	victim._dead = true;
	victim._hp = 0;

	if( victim._moving )
	{
		victim._dest = victim._pos;
		EndMoving(&victim);
		HandleMoveStopped(victim, false);
	}

	HandleDied(victim, killer);
	ScheduleRespawn(victim);
}

//***************************************************************************
// @brief 정책(GetRespawnDelayMs)에 따라 자동 리스폰 타이머를 예약합니다.
//        타이머는 엔티티 제거 시 취소되므로, 같은 ID로 재입장한 엔티티를 잘못 되살리지 않는다.
// @param victim 사망한 엔티티
//***************************************************************************
void CFieldZoneBase::ScheduleRespawn(CZoneEntity& victim)
{
	const uint32 delayMs = GetRespawnDelayMs(victim);
	if( delayMs == kNoRespawn )
		return;

	const uint64 id = victim.GetId();
	victim._respawnTimerId = ScheduleTimer(delayMs, [id](CZone& zone)
		{
			CFieldZoneBase& self = static_cast<CFieldZoneBase&>(zone);
			CZoneEntity* entity = self.FindEntity(id);
			if( entity == nullptr || !entity->_dead )
				return;

			entity->_respawnTimerId = 0;
			self.Respawn(id);
		});

	// 타이머를 걸 수 없으면 영구 사망 상태로 남지 않도록 즉시 부활시킨다.
	if( victim._respawnTimerId == 0 )
	{
		LOG_ERROR(_T("[CFieldZoneBase] respawn timer failed; respawning immediately. zoneId=%u, entityId=%llu"),
			GetZoneId(), static_cast<unsigned long long>(id));
		Respawn(id);
	}
}

//***************************************************************************
// @brief 사망한 엔티티를 부활시킵니다. 체력을 가득 채우고 리스폰 위치로 옮깁니다.
//        예약된 자동 리스폰이 있으면 취소합니다. 존 스레드에서만 호출해야 합니다.
// @param id 엔티티 ID
// @return 부활했으면 true, 엔티티가 없거나 살아 있으면 false
//***************************************************************************
bool CFieldZoneBase::Respawn(uint64 id)
{
	assert(CanTouchZoneState());

	CZoneEntity* entity = FindEntity(id);
	if( entity == nullptr || !entity->_dead )
		return false;

	if( entity->_respawnTimerId != 0 )
	{
		CancelTimer(entity->_respawnTimerId);
		entity->_respawnTimerId = 0;
	}

	entity->_dead = false;
	entity->_hp = entity->_stats.maxHp;
	entity->_nextAttackMs = GetNowMs();

	const uint32 invulnMs = GetRespawnInvulnMs(*entity);
	entity->_invulnerableUntilMs = invulnMs != 0 ? GetNowMs() + invulnMs : 0;

	// 리스폰 위치가 영역 밖이면(설정 오류) 제자리에서 부활한다.
	MoveTo(entity, GetRespawnPos(*entity), false);

	HandleRespawned(*entity);
	return true;
}

//***************************************************************************
// @brief 공격 가능한 대상인지 판정하는 기본 정책입니다.
//        자기 자신은 불가, 전투 참여 종류(플레이어/몬스터)끼리만 가능하며,
//        플레이어끼리는 allowPvp일 때만, 몬스터끼리는 불가합니다. 재정의하여 파티/길드 등을 추가합니다.
// @param attacker 공격자
// @param target   대상
// @return 공격 가능하면 true
//***************************************************************************
bool CFieldZoneBase::CanAttack(const CZoneEntity& attacker, const CZoneEntity& target) const
{
	if( &attacker == &target || !attacker.IsCombatant() || !target.IsCombatant() )
		return false;

	const bool attackerIsPlayer = attacker.GetType() == EntityType::Player;
	const bool targetIsPlayer = target.GetType() == EntityType::Player;

	if( attackerIsPlayer && targetIsPlayer )
		return _fieldConfig.allowPvp;

	return attackerIsPlayer || targetIsPlayer;
}

//***************************************************************************
// @brief 사망 후 자동 리스폰까지의 지연을 반환하는 기본 정책입니다. 플레이어만 설정값으로 부활합니다.
//        몬스터 등 그 외는 kNoRespawn(리스폰 스폰 시스템이 별도로 관리)입니다.
// @param victim 사망한 엔티티
// @return 지연(ms) 또는 kNoRespawn
//***************************************************************************
uint32 CFieldZoneBase::GetRespawnDelayMs(const CZoneEntity& victim) const
{
	return victim.GetType() == EntityType::Player ? _fieldConfig.respawnDelayMs : kNoRespawn;
}

//***************************************************************************
// @brief 피해가 적용된 직후 호출됩니다. 재정의하여 주변에 피해 패킷을 브로드캐스트합니다.
//        피해자의 현재 체력은 victim.GetHp()입니다. 치명타라면 이어서 HandleDied()가 호출됩니다.
// @param attacker 공격자 (공격자가 없는 피해면 nullptr)
// @param victim   피해자
// @param damage   적용된 피해량
//***************************************************************************
void CFieldZoneBase::HandleDamaged(CZoneEntity* attacker, CZoneEntity& victim, int32 damage)
{
	(void)attacker;
	(void)victim;
	(void)damage;
}

//***************************************************************************
// @brief 엔티티가 사망했을 때 호출됩니다. 이미 이동은 중단된 상태입니다.
// @param victim 사망한 엔티티
// @param killer 처치한 엔티티 (없으면 nullptr)
//***************************************************************************
void CFieldZoneBase::HandleDied(CZoneEntity& victim, CZoneEntity* killer)
{
	(void)victim;
	(void)killer;
}

//***************************************************************************
// @brief 엔티티가 부활하여 리스폰 위치에 배치된 직후 호출됩니다.
// @param entity 부활한 엔티티
//***************************************************************************
void CFieldZoneBase::HandleRespawned(CZoneEntity& entity)
{
	(void)entity;
}

//***************************************************************************
// @brief 공격 요청이 거부되었을 때 호출됩니다. (Cooldown/Throttled는 호출되지 않음)
//        재정의하여 요청한 클라이언트에 거부 사유를 보냅니다.
// @param attacker 공격 요청 엔티티
// @param targetId 요청이 지목한 대상 ID
// @param reason   거부 사유
//***************************************************************************
void CFieldZoneBase::HandleAttackRejected(CZoneEntity& attacker, uint64 targetId, AttackResult reason)
{
	(void)attacker;
	(void)targetId;
	(void)reason;
}

//***************************************************************************
// @brief 부활 위치를 정하는 기본 정책입니다. SetSpawnPos()로 정한 위치를 그대로 씁니다.
//        재정의하여 스폰 반경 안의 무작위 지점 등을 돌려줍니다. 영역 밖 좌표를 돌려주면 제자리에서 부활합니다.
// @param entity 부활하는 엔티티
// @return 부활 좌표
//***************************************************************************
Vec2 CFieldZoneBase::GetRespawnPos(const CZoneEntity& entity) const
{
	return entity._spawnPos;
}

//***************************************************************************
// @brief 부활 직후 무적 시간을 정하는 기본 정책입니다. 플레이어만 설정값(respawnInvulnMs)을 쓰고 나머지는 없습니다.
// @param entity 부활하는 엔티티
// @return 무적 시간(ms), 0이면 없음
//***************************************************************************
uint32 CFieldZoneBase::GetRespawnInvulnMs(const CZoneEntity& entity) const
{
	return entity.GetType() == EntityType::Player ? _fieldConfig.respawnInvulnMs : 0;
}

//***************************************************************************
// @brief 체력이 회복된 직후 호출됩니다. 재정의하여 주변에 회복 패킷을 브로드캐스트합니다.
// @param entity 회복한 엔티티 (현재 체력은 entity.GetHp())
// @param amount 실제로 회복된 양
//***************************************************************************
void CFieldZoneBase::HandleHealed(CZoneEntity& entity, int32 amount)
{
	(void)entity;
	(void)amount;
}

//***************************************************************************
// @brief 좌표가 존 영역 안인지 확인합니다. NaN은 false입니다.
// @param pos 확인할 좌표
// @return 영역 안이면 true
//***************************************************************************
bool CFieldZoneBase::IsInBounds(const Vec2& pos) const
{
	return pos.x >= _fieldConfig.minX && pos.x <= _fieldConfig.maxX
		&& pos.y >= _fieldConfig.minY && pos.y <= _fieldConfig.maxY;
}

//***************************************************************************
// @brief 이동 중인 엔티티만 순회하며 한 틱만큼 위치를 적분합니다.
//        목적지까지 남은 거리가 한 틱 이동량 이하이면 목적지에 정확히 도착시켜 오버슈트를 막습니다.
//        위치 갱신 후 AOI를 갱신하며, 셀이 바뀐 경우에만 진입/이탈 통지가 발생합니다.
// @param dtMs 고정 틱 간격(ms)
//***************************************************************************
void CFieldZoneBase::UpdateMovement(uint32 dtMs)
{
	const float dtSec = static_cast<float>(dtMs) * 0.001f;

	_updatingMovement = true;

	for( size_t i = 0; i < _moving.size(); )
	{
		CZoneEntity* entity = _moving[i];

		// 이동 도중 속도가 0 이하(또는 NaN)가 되면(속박 등) 목적지에 영원히 도착하지 못하거나 뒤로 가므로 그 자리에 멈춘다.
		if( !(entity->_speed > 0.0f) )
		{
			entity->_dest = entity->_pos;
			EndMoving(entity);
			HandleMoveStopped(*entity, false);
			continue;
		}

		const float stepLen = entity->_speed * dtSec;
		const float dx = entity->_dest.x - entity->_pos.x;
		const float dy = entity->_dest.y - entity->_pos.y;
		const float distSq = dx * dx + dy * dy;

		bool arrived = false;
		if( distSq <= stepLen * stepLen )
		{
			entity->_pos = entity->_dest;
			arrived = true;
		}
		else
		{
			const float ratio = stepLen / std::sqrt(distSq);
			entity->_pos.x += dx * ratio;
			entity->_pos.y += dy * ratio;
		}

		_aoi.Update(entity);

		if( arrived )
		{
			// swap-remove로 i번 자리에 마지막 원소가 들어오므로 i를 증가시키지 않는다.
			EndMoving(entity);
			HandleMoveStopped(*entity, true);
		}
		else
		{
			++i;
		}
	}

	_updatingMovement = false;
}

//***************************************************************************
// @brief 엔티티를 이동 목록에 등록합니다.
// @param entity 이동을 시작할 엔티티
//***************************************************************************
void CFieldZoneBase::BeginMoving(CZoneEntity* entity)
{
	entity->_moving = true;
	entity->_movingIdx = _moving.size();
	_moving.push_back(entity);
}

//***************************************************************************
// @brief 엔티티를 이동 목록에서 O(1)로 제거합니다. (마지막 원소와 교체 후 pop)
// @param entity 이동을 끝낼 엔티티
//***************************************************************************
void CFieldZoneBase::EndMoving(CZoneEntity* entity)
{
	const size_t idx = entity->_movingIdx;

	CZoneEntity* last = _moving.back();
	_moving[idx] = last;
	last->_movingIdx = idx;
	_moving.pop_back();

	entity->_moving = false;
	entity->_movingIdx = CZoneEntity::kInvalidIndex;
}