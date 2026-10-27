//***************************************************************************
// ZoneEntity.h : interface for the CZoneEntity class.
//
//***************************************************************************

#ifndef ZONEENTITY_H
#define ZONEENTITY_H

#include <cstddef>

#include <BaseRedefineDataType.h>

#include "GridAOI.h"
#include "ZoneMath.h"

class CFieldZoneBase;

//***************************************************************************
// @brief 존에 배치되는 엔티티 종류.
//***************************************************************************
enum class EntityType : uint8
{
	Player,
	Monster,
	Npc,
	Item,
};

//***************************************************************************
// @class CZoneEntity
// @brief 존에 배치되는 모든 객체(플레이어/몬스터/NPC/아이템)의 기반 클래스.
// @details
// 위치/이동/AOI 소속 정보는 CFieldZoneBase와 CGridAOI만 변경하며(friend),
// 모든 접근은 존 스레드에서만 이루어지므로 별도의 락이 없다.
// AOI 진입/이탈 통지(OnAoiEnter/OnAoiLeave)는 파생 클래스가 재정의하여
// 스폰/디스폰 패킷 전송 등에 사용한다.
//
// 통지 콜백 안에서는 AOI 구조를 변경(Add/Remove/Update)하거나
// 엔티티를 제거해서는 안 된다.
//***************************************************************************
class CZoneEntity
{
public:
	CZoneEntity(uint64 id, EntityType type);
	virtual ~CZoneEntity();

	CZoneEntity(const CZoneEntity&) = delete;
	CZoneEntity& operator=(const CZoneEntity&) = delete;

	void SetSpawnPos(const Vec2& pos);

	//***************************************************************************
	// @brief 엔티티 ID를 반환합니다.
	// @return 존 내에서 유일한 엔티티 ID
	//***************************************************************************
	uint64 GetId() const { return _id; }

	//***************************************************************************
	// @brief 엔티티 종류를 반환합니다.
	// @return 엔티티 타입
	//***************************************************************************
	EntityType GetType() const { return _type; }

	//***************************************************************************
	// @brief 서버가 보유한 현재 위치를 반환합니다.
	// @return 현재 좌표
	//***************************************************************************
	const Vec2& GetPos() const { return _pos; }

	//***************************************************************************
	// @brief 이동 중인 경우의 목적지를 반환합니다.
	// @return 목적지 좌표
	//***************************************************************************
	const Vec2& GetDest() const { return _dest; }

	//***************************************************************************
	// @brief 이동 속도를 반환합니다.
	// @return 이동 속도(units/sec)
	//***************************************************************************
	float GetSpeed() const { return _speed; }

	//***************************************************************************
	// @brief 이동 속도를 설정합니다. 속도는 서버 스탯으로만 결정되며 클라이언트 값은 신뢰하지 않습니다.
	// @param speed 이동 속도(units/sec)
	//***************************************************************************
	void SetSpeed(float speed) { _speed = speed; }

	//***************************************************************************
	// @brief 이동 중인지 반환합니다.
	// @return 이동 중이면 true
	//***************************************************************************
	bool IsMoving() const { return _moving; }

protected:
	virtual void OnAoiEnter(CZoneEntity& other);
	virtual void OnAoiLeave(CZoneEntity& other);

private:
	template <typename T> friend class CGridAOI;
	friend class CFieldZoneBase;

	static constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

	uint64		_id;					// 엔티티 ID
	EntityType	_type;					// 엔티티 종류
	Vec2		_pos;					// 현재 위치 (서버 권위)
	Vec2		_dest;					// 이동 목적지
	float		_speed;					// 이동 속도 (units/sec)
	bool		_moving;				// 이동 중 여부
	bool		_inZone;				// 존에 등록되었는지 여부

	SAoiNode	_aoiNode;				// 소속 AOI 셀 정보 - CGridAOI 전용
	size_t		_movingIdx;				// 이동 목록 내 위치 - CFieldZoneBase 전용
	uint64		_lastMoveReqMs;			// 마지막으로 접수한 이동 요청의 존 논리 시각 - CFieldZoneBase 전용
	bool		_hasMoveReq;			// 이동 요청을 접수한 적이 있는지 - CFieldZoneBase 전용
};

#endif // ndef ZONEENTITY_H
