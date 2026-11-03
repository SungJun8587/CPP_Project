
//***************************************************************************
// BotBrain.h : 테스트 봇 한 마리의 프로토콜 상태 머신 + 전투 의사결정
//
// 네트워크 계층(IOCP)에 의존하지 않는다. 입력은 "수신 패킷 + 현재 시각", 출력은 SendFn 콜백이므로
// Linux 하네스에서 CBattleZone에 직접 물려 검증할 수 있다. 스레드 안전하지 않다 — 호출자가 봇 단위로
// 직렬화한다(BotClient.cpp는 봇마다 뮤텍스를 둔다).
//***************************************************************************

#ifndef BOTBRAIN_H
#define BOTBRAIN_H

#include "../GameCommon/ZoneCommon/ZonePacket.h"
#include "../GameCommon/ZoneCore/BlockMap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

//***************************************************************************
// @brief 봇 동작 설정.
//***************************************************************************
struct SBotConfig
{
	uint64	charId = 0;					// 로그인할 캐릭터 ID (= 존 내 엔티티 ID)
	uint32	zoneId = 1;					// 입장할 존
	uint32	heroId = 0;					// 선택 영웅 (0 = 서버 기본 영웅)
	uint32	loginDelayMs = 0;			// 연결 후 로그인 요청까지 대기 (접속 램프업용)
	uint32	moveIntervalMs = 300;		// 이동 요청 최소 간격
	uint32	attackIntervalMs = 400;		// 공격 요청 간격 (서버 쿨다운보다 짧으면 서버가 조용히 무시한다)
	uint32	skillId = 0;				// 0이면 스킬을 쓰지 않는다. 이 영웅이 보유한 스킬 ID여야 한다(없으면 서버가 거부하고 봇은 스킬을 끈다)
	uint32	skillIntervalMs = 1500;		// 스킬 요청 간격 (서버 쿨다운보다 짧으면 Cooldown 거부가 늘 뿐이다)
	float	approachDist = 1.5f;		// 이 거리 이내로 붙으면 공격 (사거리 밖 거부를 받으면 자동으로 줄어든다)
	float	wanderRadius = 30.0f;		// 대상이 없을 때 스폰 위치 기준 배회 반경
	uint32	wanderIntervalMs = 2000;	// 배회 목적지 갱신 간격
	uint32	moveAckTimeoutMs = 1000;	// 이동 요청 후 서버 통지를 기다리는 최대 시간
	uint32	maxPacketsPerSec = 60;		// 송신 상한 (Gateway 세션 제한 120/s보다 낮게)
	uint32	seed = 0;					// 0이면 charId로 시드
	std::shared_ptr<const CBlockMap> blockMap;	// 장애물 맵(nullptr이면 장애물 없음). 서버와 같은 Block.json으로 만들어 모든 봇이 공유한다
};

//***************************************************************************
// @brief 봇 진행 상태.
//***************************************************************************
enum class EBotState : uint8
{
	Disconnected,
	Connected,		// 연결됨, 로그인 요청 전
	LoginSent,
	EnterSent,
	InZone,
	Failed,			// 로그인/입장 거부 — 재시도하지 않는다
};

//***************************************************************************
// @brief 봇 통계. 집계는 호출자가 합산한다.
//***************************************************************************
struct SBotStats
{
	uint64	recvPackets = 0;
	uint64	recvBytes = 0;
	uint64	sentPackets = 0;
	uint64	sentBytes = 0;
	uint64	throttled = 0;			// 송신 상한에 걸려 보내지 않은 요청
	uint64	sendFailed = 0;
	uint64	unknownPackets = 0;
	uint64	moveReqs = 0;
	uint64	attackReqs = 0;
	uint64	skillReqs = 0;
	uint64	skillCasts = 0;			// 내 스킬 시전 통지 수
	uint64	skillHits = 0;			// 내 스킬 시전 통지의 hitCount 합
	uint64	skillRejects[8] = {};	// ESkillRejectReason 별
	uint64	castStarts = 0;			// 내 시전 시작 통지 수 (시전 시간이 있는 스킬)
	uint64	castCancels[8] = {};	// 내 시전 취소 통지 수, ESkillCancelReason 별
	uint64	projectiles = 0;		// 내 투사체 발사 통지 수
	uint64	impacts = 0;			// 내 투사체 착탄 통지 수
	uint64	impactHits = 0;			// 착탄 통지의 hitCount 합
	uint64	statusApplied = 0;		// 나에게 걸린(또는 갱신된) 상태 효과 통지 수
	uint64	statusEnded = 0;		// 나에게서 풀린 상태 효과 통지 수
	uint64	attackRejects[8] = {};	// EAttackRejectReason 별
	uint64	posCorrects[8] = {};	// ECorrectPosReason 별
	uint64	damageDealt = 0;		// 내가 준 피해 합
	uint64	damageTaken = 0;
	uint64	hits = 0;				// 내 공격으로 발생한 DamageNotify 수
	uint64	kills = 0;
	uint64	deaths = 0;
	uint64	respawns = 0;
	uint64	zoneLost = 0;
	uint64	loginFailReason = 0;	// 마지막 실패 사유 (EGatewayResult)
	std::array<uint64, 32> zcRecv{};	// type - 2000 별 수신 수
	std::array<uint64, 16> gwRecv{};	// 1~15 별 수신 수
	std::vector<uint32> latencyMs;		// 공격 요청 -> 내 DamageNotify 지연 샘플 (최근 kMaxLatencySamples개)
};

class CBotBrain
{
public:
	using SendFn = std::function<bool(const void*, uint16)>;

	static constexpr uint8	kWireEntityPlayer = 0;		// ZcSpawnNotify::entityType (EntityType::Player)
	static constexpr size_t	kMaxLatencySamples = 4096;
	static constexpr uint32	kCastSlackMs = 500;			// 시전 완료/취소 통지가 오지 않아도 이 시간이 지나면 시전이 끝난 것으로 본다

	CBotBrain(const SBotConfig& cfg, SendFn send)
		: _cfg(cfg), _send(std::move(send)), _rng(cfg.seed != 0 ? cfg.seed : static_cast<uint32>(cfg.charId * 2654435761u))
	{
		_approachDist = cfg.approachDist;
	}

	EBotState State() const { return _state; }
	const SBotStats& Stats() const { return _stats; }
	void ClearLatencySamples() { _stats.latencyMs.clear(); }
	uint64 CharId() const { return _cfg.charId; }
	bool IsDead() const { return _dead; }
	size_t KnownEntities() const { return _ents.size(); }
	void SelfPos(uint64 nowMs, float& x, float& y) const { PosAt(_self, nowMs, x, y); }

	//***************************************************************************
	// @brief TCP 연결이 완료됐을 때 호출합니다.
	//***************************************************************************
	void OnConnected(uint64 nowMs)
	{
		_state = EBotState::Connected;
		_loginAtMs = nowMs + _cfg.loginDelayMs;
	}

	void OnDisconnected()
	{
		if( _state != EBotState::Failed )
			_state = EBotState::Disconnected;
	}

	//***************************************************************************
	// @brief 완전한 패킷 하나를 처리합니다.
	// @return false면 프로토콜 위반(크기 부족 등) — 호출자가 연결을 끊는다.
	//***************************************************************************
	bool OnPacket(const PacketHeader* h, uint64 nowMs)
	{
		++_stats.recvPackets;
		_stats.recvBytes += h->size;

		switch( h->type )
		{
		case (uint16)EGatewayPacketType::GwLoginRes:
			Count(_stats.gwRecv, h->type);
			return With<GwLoginResPacket>(h, [&](auto& p) { OnLoginRes(p, nowMs); });
		case (uint16)EGatewayPacketType::GwEnterZoneRes:
			Count(_stats.gwRecv, h->type);
			return With<GwEnterZoneResPacket>(h, [&](auto& p) { OnEnterRes(p, nowMs); });
		case (uint16)EGatewayPacketType::GwLeaveZoneRes:
			Count(_stats.gwRecv, h->type);
			return true;
		case (uint16)EGatewayPacketType::GwZoneLostNotify:
			Count(_stats.gwRecv, h->type);
			return With<GwZoneLostNotifyPacket>(h, [&](auto&) { OnZoneLost(nowMs); });

		case (uint16)EZoneClientPacketType::ZcMoveNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcMoveNotifyPacket>(h, [&](auto& p) { OnMove(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcStopNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcStopNotifyPacket>(h, [&](auto& p) { OnStop(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcSpawnNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSpawnNotifyPacket>(h, [&](auto& p) { OnSpawn(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcDespawnNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcDespawnNotifyPacket>(h, [&](auto& p) { OnDespawn(p); });
		case (uint16)EZoneClientPacketType::ZcCorrectPosNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcCorrectPosNotifyPacket>(h, [&](auto& p) { OnCorrect(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcDamageNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcDamageNotifyPacket>(h, [&](auto& p) { OnDamage(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcDeathNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcDeathNotifyPacket>(h, [&](auto& p) { OnDeath(p); });
		case (uint16)EZoneClientPacketType::ZcRespawnNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcRespawnNotifyPacket>(h, [&](auto& p) { OnRespawn(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcAttackRejectNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcAttackRejectNotifyPacket>(h, [&](auto& p) { OnAttackReject(p); });
		case (uint16)EZoneClientPacketType::ZcHealNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcHealNotifyPacket>(h, [&](auto& p) { OnHeal(p); });
		case (uint16)EZoneClientPacketType::ZcSkillNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSkillNotifyPacket>(h, [&](auto& p) { OnSkill(p); });
		case (uint16)EZoneClientPacketType::ZcSkillRejectNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSkillRejectNotifyPacket>(h, [&](auto& p) { OnSkillReject(p); });
		case (uint16)EZoneClientPacketType::ZcSkillCastNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSkillCastNotifyPacket>(h, [&](auto& p) { OnSkillCastStart(p, nowMs); });
		case (uint16)EZoneClientPacketType::ZcSkillCancelNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSkillCancelNotifyPacket>(h, [&](auto& p) { OnSkillCancel(p); });
		case (uint16)EZoneClientPacketType::ZcProjectileNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcProjectileNotifyPacket>(h, [&](auto& p) { OnProjectile(p); });
		case (uint16)EZoneClientPacketType::ZcSkillImpactNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcSkillImpactNotifyPacket>(h, [&](auto& p) { OnImpact(p); });
		case (uint16)EZoneClientPacketType::ZcStatusNotify:
			Count(_stats.zcRecv, h->type, kClientBase);
			return With<ZcStatusNotifyPacket>(h, [&](auto& p) { OnStatus(p); });
		default:
			++_stats.unknownPackets;
			return true;
		}
	}

	//***************************************************************************
	// @brief 주기 호출(약 50~100ms). 로그인 지연 만료 처리와 전투 행동 결정.
	//***************************************************************************
	void Tick(uint64 nowMs)
	{
		if( _state == EBotState::Connected && nowMs >= _loginAtMs )
		{
			GwLoginReqPacket req{};
			InitZonePacket(req, EGatewayPacketType::GwLoginReq);
			req.charId = _cfg.charId;
			if( Send(&req, req.size, nowMs, true) )
				_state = EBotState::LoginSent;
			return;
		}

		if( _state == EBotState::EnterSent && _retryEnterAtMs != 0 && nowMs >= _retryEnterAtMs )
		{
			_retryEnterAtMs = 0;
			SendEnter(nowMs);
			return;
		}

		if( _state != EBotState::InZone || _dead )
			return;

		DropStaleTarget();
		if( _targetId == 0 )
			PickTarget(nowMs);

		if( _targetId != 0 )
			Fight(nowMs);
		else
			Wander(nowMs);
	}

private:
	static constexpr uint16 kClientBase = 2000;

	// 위치 + 이동 상태. 서버가 준 시작점/목적지/속도로 로컬 시계 기준 선형 보간한다.
	struct SEnt
	{
		uint8	type = 0;
		float	x = 0, y = 0, destX = 0, destY = 0, speed = 0;
		bool	moving = false;
		bool	dead = false;
		int32	hp = 0, maxHp = 0;
		uint64	atMs = 0;		// x,y가 유효했던 로컬 시각
	};

	template <typename T, typename F>
	bool With(const PacketHeader* h, F&& fn)
	{
		if( h->size < sizeof(T) )
			return false;
		fn(*static_cast<const T*>(h));
		return true;
	}

	template <typename Arr>
	static void Count(Arr& arr, uint16 type, uint16 base = 0)
	{
		const size_t i = static_cast<size_t>(type - base);
		if( i < arr.size() )
			++arr[i];
	}

	static void PosAt(const SEnt& e, uint64 nowMs, float& x, float& y)
	{
		x = e.x; y = e.y;
		if( !e.moving || nowMs <= e.atMs )
			return;

		const float dx = e.destX - e.x, dy = e.destY - e.y;
		const float len = std::sqrt(dx * dx + dy * dy);
		const float travel = e.speed * static_cast<float>(nowMs - e.atMs) / 1000.0f;
		if( len <= 0.0001f || travel >= len )
		{
			x = e.destX; y = e.destY;
			return;
		}
		x = e.x + dx / len * travel;
		y = e.y + dy / len * travel;
	}

	static void SetStopped(SEnt& e, float x, float y, uint64 nowMs)
	{
		e.x = x; e.y = y; e.moving = false; e.atMs = nowMs;
	}

	// --- 송신 ---------------------------------------------------------------

	bool Send(const void* data, uint16 size, uint64 nowMs, bool /*essential*/ = false)
	{
		// 1초 고정 창 송신 상한. Gateway 세션 상한(120/s)에 걸려 끊기지 않게 봇이 먼저 거른다.
		if( nowMs - _winStartMs >= 1000 )
		{
			_winStartMs = nowMs;
			_winCount = 0;
		}
		if( _winCount >= _cfg.maxPacketsPerSec )
		{
			++_stats.throttled;
			return false;
		}

		if( !_send(data, size) )
		{
			++_stats.sendFailed;
			return false;
		}
		++_winCount;
		++_stats.sentPackets;
		_stats.sentBytes += size;
		return true;
	}

	void SendEnter(uint64 nowMs)
	{
		GwEnterZoneReqPacket req{};
		InitZonePacket(req, EGatewayPacketType::GwEnterZoneReq);
		req.zoneId = _cfg.zoneId;
		req.heroId = _cfg.heroId;
		Send(&req, req.size, nowMs, true);
	}

	// --- 로그인/입장 -----------------------------------------------------------

	void OnLoginRes(const GwLoginResPacket& p, uint64 nowMs)
	{
		if( _state != EBotState::LoginSent )
			return;
		if( !p.success )
		{
			_stats.loginFailReason = p.reason;
			_state = EBotState::Failed;
			return;
		}
		_state = EBotState::EnterSent;
		SendEnter(nowMs);
	}

	void OnEnterRes(const GwEnterZoneResPacket& p, uint64 nowMs)
	{
		if( _state != EBotState::EnterSent )
			return;
		if( !p.success )
		{
			_stats.loginFailReason = p.reason;
			_state = EBotState::Failed;
			return;
		}

		ResetWorld();
		_self = SEnt{};
		SetStopped(_self, p.spawnX, p.spawnY, nowMs);
		_homeX = p.spawnX; _homeY = p.spawnY;
		_state = EBotState::InZone;
	}

	void OnZoneLost(uint64 nowMs)
	{
		++_stats.zoneLost;
		ResetWorld();
		if( _state == EBotState::InZone )
		{
			// 존이 사라졌다. 1초 뒤 같은 존에 다시 입장을 시도한다.
			_state = EBotState::EnterSent;
			_retryEnterAtMs = nowMs + 1000;
		}
	}

	void ResetWorld()
	{
		_ents.clear();
		_targetId = 0;
		_dead = false;
		_awaitMoveAck = false;
		_attackPending = false;
		_approachDist = _cfg.approachDist;
	}

	// --- 월드 상태 -------------------------------------------------------------

	void OnSpawn(const ZcSpawnNotifyPacket& p, uint64 nowMs)
	{
		SEnt e;
		e.type = p.entityType; e.x = p.posX; e.y = p.posY; e.destX = p.destX; e.destY = p.destY;
		e.speed = p.speed; e.moving = p.moving != 0; e.hp = p.hp; e.maxHp = p.maxHp; e.dead = p.dead != 0; e.atMs = nowMs;

		if( p.entityId == _cfg.charId )
		{
			_self = e;
			_dead = e.dead;
			return;
		}
		const uint64 id = p.entityId;
		_ents[id] = e;
	}

	void OnDespawn(const ZcDespawnNotifyPacket& p)
	{
		const uint64 id = p.entityId;		// 패킹된 필드는 참조로 바인딩하지 않고 값으로 복사한다
		_ents.erase(id);
		if( id == _targetId )
			_targetId = 0;
	}

	void OnMove(const ZcMoveNotifyPacket& p, uint64 nowMs)
	{
		SEnt* e = Find(p.entityId);
		if( e == nullptr )
			return;
		e->x = p.startX; e->y = p.startY; e->destX = p.destX; e->destY = p.destY;
		e->speed = p.speed; e->moving = true; e->atMs = nowMs;
		if( p.entityId == _cfg.charId )
			_awaitMoveAck = false;
	}

	void OnStop(const ZcStopNotifyPacket& p, uint64 nowMs)
	{
		SEnt* e = Find(p.entityId);
		if( e == nullptr )
			return;
		SetStopped(*e, p.posX, p.posY, nowMs);
		if( p.entityId == _cfg.charId )
			_awaitMoveAck = false;
	}

	void OnCorrect(const ZcCorrectPosNotifyPacket& p, uint64 nowMs)
	{
		if( p.reason < 8 )
			++_stats.posCorrects[p.reason];
		SetStopped(_self, p.posX, p.posY, nowMs);
		_awaitMoveAck = false;
	}

	void OnDamage(const ZcDamageNotifyPacket& p, uint64 nowMs)
	{
		if( SEnt* t = Find(p.targetId) )
		{
			t->hp = p.hp; t->maxHp = p.maxHp;
		}

		const int32 damage = p.damage < 0 ? 0 : p.damage;
		if( p.targetId == _cfg.charId )
			_stats.damageTaken += static_cast<uint64>(damage);

		if( p.attackerId == _cfg.charId )
		{
			++_stats.hits;
			_stats.damageDealt += static_cast<uint64>(damage);
			if( _attackPending && p.targetId == _attackTarget )
			{
				_attackPending = false;
				if( _stats.latencyMs.size() >= kMaxLatencySamples )
					_stats.latencyMs.erase(_stats.latencyMs.begin());
				_stats.latencyMs.push_back(static_cast<uint32>(nowMs - _attackSentMs));
			}
		}
	}

	void OnDeath(const ZcDeathNotifyPacket& p)
	{
		if( p.entityId == _cfg.charId )
		{
			++_stats.deaths;
			_dead = true;
			_self.moving = false;
			_targetId = 0;
			_awaitMoveAck = false;
			_castUntilMs = 0;
			return;
		}

		if( SEnt* e = Find(p.entityId) )
		{
			e->dead = true; e->moving = false; e->hp = 0;
		}
		if( p.killerId == _cfg.charId )
			++_stats.kills;
		if( p.entityId == _targetId )
			_targetId = 0;
	}

	void OnRespawn(const ZcRespawnNotifyPacket& p, uint64 nowMs)
	{
		SEnt* e = Find(p.entityId);
		if( e == nullptr )
			return;
		SetStopped(*e, p.posX, p.posY, nowMs);
		e->hp = p.hp; e->maxHp = p.maxHp; e->dead = false;
		if( p.entityId == _cfg.charId )
		{
			++_stats.respawns;
			_dead = false;
			_awaitMoveAck = false;
		}
	}

	void OnHeal(const ZcHealNotifyPacket& p)
	{
		if( SEnt* e = Find(p.entityId) )
		{
			e->hp = p.hp; e->maxHp = p.maxHp;
		}
	}

	void OnSkill(const ZcSkillNotifyPacket& p)
	{
		if( p.casterId != _cfg.charId )
			return;
		++_stats.skillCasts;
		_stats.skillHits += p.hitCount;
		_castUntilMs = 0;		// 시전이 끝났다 (즉시 스킬은 곧바로 이 통지가 온다)
	}

	// 시전 시간이 있는 스킬: 끝나거나 취소될 때까지 제자리에서 기다린다. 이동하면 서버가 시전을 취소하고, 기본 공격은 Casting으로 거부된다.
	void OnSkillCastStart(const ZcSkillCastNotifyPacket& p, uint64 nowMs)
	{
		if( p.casterId != _cfg.charId )
			return;
		++_stats.castStarts;
		_castUntilMs = nowMs + p.castTimeMs + kCastSlackMs;
	}

	void OnSkillCancel(const ZcSkillCancelNotifyPacket& p)
	{
		if( p.casterId != _cfg.charId )
			return;
		if( p.reason < 8 )
			++_stats.castCancels[p.reason];
		_castUntilMs = 0;
	}

	void OnProjectile(const ZcProjectileNotifyPacket& p)
	{
		if( p.casterId != _cfg.charId )
			return;
		++_stats.projectiles;
		_castUntilMs = 0;		// 시전이 끝나고 날아가는 중. 비행 중에는 움직여도 된다
	}

	void OnImpact(const ZcSkillImpactNotifyPacket& p)
	{
		if( p.casterId != _cfg.charId )
			return;
		++_stats.impacts;
		_stats.impactHits += p.hitCount;
	}

	void OnStatus(const ZcStatusNotifyPacket& p)
	{
		if( p.entityId != _cfg.charId )
			return;
		if( p.applied != 0 )
			++_stats.statusApplied;
		else
			++_stats.statusEnded;
	}

	void OnSkillReject(const ZcSkillRejectNotifyPacket& p)
	{
		if( p.reason < 8 )
			++_stats.skillRejects[p.reason];
		// 이 영웅이 쓸 수 없는 스킬이면 반복해도 소용없다.
		if( p.reason == static_cast<uint8>(ESkillRejectReason::UnknownSkill) )
			_skillDisabled = true;
	}

	void OnAttackReject(const ZcAttackRejectNotifyPacket& p)
	{
		if( p.reason < 8 )
			++_stats.attackRejects[p.reason];
		_attackPending = false;

		switch( static_cast<EAttackRejectReason>(p.reason) )
		{
		case EAttackRejectReason::OutOfRange:
			// 내 영웅의 실제 사거리를 모르므로, 거부될 때마다 더 가까이 붙는다.
			_approachDist = std::max(0.3f, _approachDist * 0.7f);
			break;
		case EAttackRejectReason::TargetDead:
			if( p.targetId == _targetId ) { if( SEnt* e = Find(p.targetId) ) e->dead = true; _targetId = 0; }
			break;
		case EAttackRejectReason::TargetInvulnerable:
		case EAttackRejectReason::InvalidTarget:
			_targetId = 0;	// 다른 대상을 고른다
			break;
		case EAttackRejectReason::NoLineOfSight:
			_repathAtMs = 0;	// 위치 추정이 어긋난 것이므로 바로 경로를 다시 본다
			break;
		default:
			break;
		}
	}

	SEnt* Find(uint64 id)
	{
		if( id == _cfg.charId )
			return &_self;
		auto it = _ents.find(id);
		return it != _ents.end() ? &it->second : nullptr;
	}

	// --- 행동 ------------------------------------------------------------------

	void DropStaleTarget()
	{
		if( _targetId == 0 )
			return;
		auto it = _ents.find(_targetId);
		if( it == _ents.end() || it->second.dead )
			_targetId = 0;
	}

	// 장애물 맵이 있을 때 (ax,ay) -> (bx,by) 직선이 막히지 않는지. 막힌 직선은 서버가 중간에서 자르므로, 막혀 있으면 FindPath의 경유점으로 돌아간다.
	bool IsClear(float ax, float ay, float bx, float by) const
	{
		return _cfg.blockMap == nullptr || _cfg.blockMap->IsSegmentClear(Vec2{ ax, ay }, Vec2{ bx, by });
	}

	void PickTarget(uint64 nowMs)
	{
		float sx, sy;
		PosAt(_self, nowMs, sx, sy);

		// 직선이 트인 가장 가까운 대상을 먼저 고른다. 없으면 길을 찾을 수 있는지 시도해 볼 가장 가까운 대상을 고른다(Fight가 경로를 찾고,
		// 닿을 수 없으면 한동안 제외한다).
		float best = std::numeric_limits<float>::max();
		float bestBlocked = std::numeric_limits<float>::max();
		uint64 bestId = 0, bestBlockedId = 0;
		for( const auto& kv : _ents )
		{
			const SEnt& e = kv.second;
			if( e.type == kWireEntityPlayer || e.dead || IsUnreachable(kv.first, nowMs) )
				continue;
			float ex, ey;
			PosAt(e, nowMs, ex, ey);
			const float d = (ex - sx) * (ex - sx) + (ey - sy) * (ey - sy);
			if( IsClear(sx, sy, ex, ey) )
			{
				if( d < best ) { best = d; bestId = kv.first; }
			}
			else if( d < bestBlocked )
			{
				bestBlocked = d; bestBlockedId = kv.first;
			}
		}
		_targetId = bestId != 0 ? bestId : bestBlockedId;
	}

	// 길을 찾지 못한 대상은 잠시 제외한다. 닿을 수 없는 대상을 계속 고르고 탐색하는 낭비를 막는다.
	bool IsUnreachable(uint64 id, uint64 nowMs)
	{
		const auto it = _unreachableUntil.find(id);
		if( it == _unreachableUntil.end() )
			return false;
		if( nowMs >= it->second )
		{
			_unreachableUntil.erase(it);
			return false;
		}
		return true;
	}

	void MarkUnreachable(uint64 id, uint64 nowMs)
	{
		if( _unreachableUntil.size() >= 256 )
			_unreachableUntil.clear();		// 사라진 엔티티의 항목이 쌓이지 않게 상한을 둔다
		_unreachableUntil[id] = nowMs + kUnreachableMs;
	}

	// 직선이 막힌 대상에게 가기 위해 우회 경로의 첫 경유점으로 이동한다. 길이 없으면 대상을 포기한다.
	void StepAround(float sx, float sy, float tx, float ty, uint64 nowMs)
	{
		if( _self.moving && nowMs < _repathAtMs )
			return;		// 경유점으로 이동 중. 도착하거나 주기가 지나면 다시 본다
		_repathAtMs = nowMs + kRepathMs;

		if( !_cfg.blockMap->FindPath(Vec2{ sx, sy }, Vec2{ tx, ty }, _path) || _path.empty() )
		{
			MarkUnreachable(_targetId, nowMs);
			_targetId = 0;
			return;
		}
		MoveTo(sx, sy, _path.front().x, _path.front().y, nowMs, 0.5f);
	}

	void Fight(uint64 nowMs)
	{
		const SEnt& t = _ents[_targetId];
		float sx, sy, tx, ty;
		PosAt(_self, nowMs, sx, sy);
		PosAt(t, nowMs, tx, ty);

		const float dx = tx - sx, dy = ty - sy;
		const float dist = std::sqrt(dx * dx + dy * dy);

		// 스킬을 시전하는 중에는 움직이지도, 기본 공격을 하지도 않는다 (움직이면 시전이 취소된다).
		if( nowMs < _castUntilMs )
			return;

		// 대상과 나 사이에 장애물이 있으면 돌아간다(서버도 같은 이유로 벽 너머 공격을 거부한다).
		if( !IsClear(sx, sy, tx, ty) )
		{
			StepAround(sx, sy, tx, ty, nowMs);
			return;
		}

		if( dist > _approachDist )
		{
			// 대상 바로 앞(접근 거리의 80% 지점)까지 이동
			const float k = (dist - _approachDist * 0.8f) / dist;
			MoveTo(sx, sy, sx + dx * k, sy + dy * k, nowMs, 1.5f);
			return;
		}

		// 스킬: 간격이 지났으면 기본 공격 대신 대상 위치로 시전한다.
		if( _cfg.skillId != 0 && !_skillDisabled && nowMs - _lastSkillMs >= _cfg.skillIntervalMs )
		{
			ZcSkillReqPacket sreq{};
			InitZonePacket(sreq, EZoneClientPacketType::ZcSkillReq);
			sreq.skillId = _cfg.skillId;
			sreq.targetId = _targetId;
			sreq.x = tx;
			sreq.y = ty;
			if( Send(&sreq, sreq.size, nowMs) )
			{
				_lastSkillMs = nowMs;
				++_stats.skillReqs;
				return;
			}
		}

		if( nowMs - _lastAttackMs >= _cfg.attackIntervalMs )
		{
			ZcAttackReqPacket req{};
			InitZonePacket(req, EZoneClientPacketType::ZcAttackReq);
			req.targetId = _targetId;
			if( Send(&req, req.size, nowMs) )
			{
				_lastAttackMs = nowMs;
				++_stats.attackReqs;
				// 쿨다운으로 조용히 무시된 요청이 있을 수 있으므로, 지연은 "가장 최근 요청" 기준으로 잰다.
				_attackPending = true;
				_attackSentMs = nowMs;
				_attackTarget = _targetId;
			}
		}
	}

	void Wander(uint64 nowMs)
	{
		if( nowMs - _lastWanderMs < _cfg.wanderIntervalMs )
			return;
		_lastWanderMs = nowMs;

		std::uniform_real_distribution<float> d(-_cfg.wanderRadius, _cfg.wanderRadius);
		float sx, sy;
		PosAt(_self, nowMs, sx, sy);

		// 장애물이 있으면 직선이 막히지 않는 지점을 몇 번 골라 본다. 못 찾으면 이번 주기는 쉰다.
		for( int tries = 0; tries < 8; ++tries )
		{
			const float wx = std::max(1.0f, _homeX + d(_rng));
			const float wy = std::max(1.0f, _homeY + d(_rng));
			if( _cfg.blockMap != nullptr && (_cfg.blockMap->IsBlocked(Vec2{ wx, wy }) || !IsClear(sx, sy, wx, wy)) )
				continue;
			MoveTo(sx, sy, wx, wy, nowMs, 0.0f);
			return;
		}
	}

	// 이동 요청. 직전 요청의 서버 통지를 기다리는 중이면 보내지 않는다(내 위치 추정이 서버와 어긋나는 것을 막는다).
	void MoveTo(float sx, float sy, float dx, float dy, uint64 nowMs, float minDestDelta)
	{
		if( _awaitMoveAck )
		{
			if( nowMs - _lastMoveMs < _cfg.moveAckTimeoutMs )
				return;
			_awaitMoveAck = false;
		}
		if( nowMs - _lastMoveMs < _cfg.moveIntervalMs )
			return;

		// 이미 같은 곳으로 이동 중이면 중복 요청하지 않는다
		if( _self.moving && std::fabs(_self.destX - dx) < minDestDelta && std::fabs(_self.destY - dy) < minDestDelta )
			return;

		ZcMoveReqPacket req{};
		InitZonePacket(req, EZoneClientPacketType::ZcMoveReq);
		req.posX = sx; req.posY = sy; req.destX = dx; req.destY = dy;
		if( Send(&req, req.size, nowMs) )
		{
			_lastMoveMs = nowMs;
			_awaitMoveAck = true;
			++_stats.moveReqs;
		}
	}

private:
	SBotConfig			_cfg;
	SendFn				_send;
	std::mt19937		_rng;
	EBotState			_state = EBotState::Disconnected;
	SBotStats			_stats;

	uint64				_loginAtMs = 0;
	uint64				_retryEnterAtMs = 0;

	SEnt				_self;
	std::unordered_map<uint64, SEnt> _ents;		// 시야 내 다른 엔티티
	bool				_dead = false;
	float				_homeX = 0, _homeY = 0;

	uint64				_targetId = 0;
	static constexpr uint64 kUnreachableMs = 5000;	// 길을 못 찾은 대상을 제외하는 시간
	static constexpr uint64 kRepathMs = 300;		// 우회 중 경로를 다시 보는 최소 주기
	std::unordered_map<uint64, uint64> _unreachableUntil;	// 대상 ID -> 제외 해제 시각
	std::vector<Vec2>	_path;						// 경로탐색 작업 공간
	uint64				_repathAtMs = 0;
	float				_approachDist = 1.5f;
	uint64				_lastMoveMs = 0;
	uint64				_lastAttackMs = 0;
	uint64				_lastSkillMs = 0;
	uint64				_castUntilMs = 0;			// 시전 중이라고 보는 시각 (0이면 시전 중 아님)
	bool				_skillDisabled = false;
	uint64				_lastWanderMs = 0;
	bool				_awaitMoveAck = false;

	bool				_attackPending = false;
	uint64				_attackSentMs = 0;
	uint64				_attackTarget = 0;

	uint64				_winStartMs = 0;
	uint32				_winCount = 0;
};

#endif // ndef BOTBRAIN_H