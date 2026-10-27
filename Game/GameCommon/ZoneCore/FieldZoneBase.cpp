//***************************************************************************
// FieldZoneBase.cpp : implementation of the CFieldZoneBase class.
//
//***************************************************************************

#include "pch.h"
#include "FieldZoneBase.h"

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
	assert(IsZoneThread() || !IsRunning());

	if( !_valid || !entity || !IsInBounds(entity->_pos) )
		return nullptr;

	const uint64 id = entity->GetId();
	if( _entities.find(id) != _entities.end() )
		return nullptr;

	CZoneEntity* raw = entity.get();
	raw->_dest = raw->_pos;
	raw->_moving = false;
	raw->_inZone = true;
	raw->_hasMoveReq = false;

	_entities.emplace(id, std::move(entity));
	_aoi.Add(raw);
	return raw;
}

//***************************************************************************
// @brief 엔티티를 존에서 제거합니다. 이동 중이면 중단하고, AOI에서 양방향 이탈 통지를 보낸 뒤 해제합니다.
//        이동 갱신/AOI 콜백 안에서는 호출할 수 없습니다. 존 스레드에서만 호출해야 합니다.
// @param id 제거할 엔티티 ID
// @return 제거했으면 true, 없으면 false
//***************************************************************************
bool CFieldZoneBase::RemoveEntity(uint64 id)
{
	assert(IsZoneThread() || !IsRunning());
	assert(!_updatingMovement && "CFieldZoneBase::RemoveEntity called during movement update");

	auto it = _entities.find(id);
	if( it == _entities.end() )
		return false;

	CZoneEntity* entity = it->second.get();

	if( entity->_moving )
		EndMoving(entity);

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
	assert(IsZoneThread() || !IsRunning());

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
	assert(IsZoneThread() || !IsRunning());
	assert(entity._inZone);

	const uint64 nowMs = GetNowMs();
	if( _fieldConfig.minMoveIntervalMs != 0 && entity._hasMoveReq &&
		nowMs - entity._lastMoveReqMs < _fieldConfig.minMoveIntervalMs )
		return MoveResult::Throttled;

	entity._hasMoveReq = true;
	entity._lastMoveReqMs = nowMs;

	if( entity._speed <= 0.0f )
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

	// 이미 목적지에 있으면 이동하지 않고 정지 처리한다.
	if( DistanceSq(dest, entity._pos) < 1e-4f )
	{
		StopMove(entity.GetId());
		return MoveResult::Ok;
	}

	// 같은 목적지로 이미 이동 중이면 알릴 변화가 없다.
	if( entity._moving && DistanceSq(dest, entity._dest) < 1e-4f )
		return MoveResult::Ok;

	entity._dest = dest;

	if( !entity._moving )
		BeginMoving(&entity);

	HandleMoveStarted(entity);
	return MoveResult::Ok;
}

//***************************************************************************
// @brief 이동 중인 엔티티를 현재 서버 위치에서 정지시킵니다. 존 스레드에서만 호출해야 합니다.
// @param id 엔티티 ID
// @return 이동 중이어서 정지했으면 true
//***************************************************************************
bool CFieldZoneBase::StopMove(uint64 id)
{
	assert(IsZoneThread() || !IsRunning());

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
	assert(IsZoneThread() || !IsRunning());
	assert(!_updatingMovement && "CFieldZoneBase::Teleport called during movement update");

	CZoneEntity* entity = FindEntity(id);
	if( entity == nullptr || !IsInBounds(pos) )
		return false;

	const bool wasMoving = entity->_moving;
	if( wasMoving )
		EndMoving(entity);

	entity->_pos = pos;
	entity->_dest = pos;
	_aoi.Update(entity);

	if( wasMoving )
		HandleMoveStopped(*entity, false);

	HandleTeleported(*entity);
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
