
//***************************************************************************
// BotClient.cpp : 필드 전투 테스트용 봇 클라이언트 (Gateway 클라이언트 포트로 접속)
//
// 봇마다 TCP 세션 하나를 열고 로그인 -> 존 입장 -> 가까운 몬스터에게 접근/공격을 반복한다.
// 프로토콜 상태 머신과 전투 판단은 BotBrain.h(네트워크 비의존)에 있고, 이 파일은 IOCP 연결/송수신,
// 틱 스레드, 통계 출력만 담당한다.
//
// 설정: 우선순위는 명령행 인자 > JSON 설정 파일(--config) > SBotClientOptions 기본값이다 (BotClientConfig.h/.cpp).
// 실행 예: BotClient.exe --config Config\botclient_config.json --bots 200
//          BotClient.exe --host 127.0.0.1 --port 7000 --bots 50 --zone 1 --heroes 1,2 --duration 60
//          BotClient.exe --dump-config Config\botclient_config.json   (기본값 템플릿 생성)
// 옵션 목록은 BotClient.exe --help (PrintUsage) 참고.
//
// 종료 코드: 0 정상, 2 로그인/입장 실패 또는 예기치 않은 연결 종료가 있었음, -1 구동 실패
//***************************************************************************

#include "pch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
	{
		switch( ctrlType )
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			g_stop.store(true);
			return TRUE;
		default:
			return FALSE;
		}
	}

	uint64 NowMs()
	{
		return static_cast<uint64>(SteadyNowMs());
	}

	//***************************************************************************
	// @brief 봇 한 마리의 상태. 브레인은 수신(IOCP 워커)과 틱(틱 스레드)에서 번갈아 호출되므로 뮤텍스로 직렬화한다.
	// @details 재귀 뮤텍스: 송신 실패 등으로 세션이 같은 스레드에서 OnDisconnected()를 곧바로 호출해도 교착되지 않는다.
	//***************************************************************************
	struct SBotSlot
	{
		std::recursive_mutex			mtx;
		std::unique_ptr<CBotBrain>		brain;
		std::weak_ptr<CIocpSession>		session;		// 연결 중에만 유효
		bool							everConnected = false;
	};

	//***************************************************************************
	// @class CBotSession
	// @brief 봇 하나에 대응하는 클라이언트 세션. 수신 패킷을 브레인에 전달한다.
	//***************************************************************************
	class CBotSession : public CIocpSession
	{
	public:
		CBotSession(SBotSlot* slot, std::atomic<uint32>* disconnects, std::atomic<uint32>* connectFails)
			: _slot(slot), _disconnects(disconnects), _connectFails(connectFails)
		{
		}

	protected:
		void OnConnected() override
		{
			std::lock_guard<std::recursive_mutex> lock(_slot->mtx);
			_slot->session = std::static_pointer_cast<CIocpSession>(shared_from_this());
			_slot->everConnected = true;
			_slot->brain->OnConnected(NowMs());
		}

		void OnDisconnected() override
		{
			std::lock_guard<std::recursive_mutex> lock(_slot->mtx);
			_slot->session.reset();
			_slot->brain->OnDisconnected();

			if( !_slot->everConnected )
				_connectFails->fetch_add(1);
			else if( !g_stop.load() )
				_disconnects->fetch_add(1);
		}

		int32 OnRecv(BYTE* buffer, int32 len) override
		{
			std::lock_guard<std::recursive_mutex> lock(_slot->mtx);
			const uint64 now = NowMs();
			return ProcessPacketFrames(buffer, len, kMaxServerPacketBytes,
				[&](const PacketHeader* header) { return _slot->brain->OnPacket(header, now); });
		}

	private:
		SBotSlot* _slot;
		std::atomic<uint32>* _disconnects;
		std::atomic<uint32>* _connectFails;
	};

	//***************************************************************************
	// @brief 쉼표로 구분된 숫자 목록을 파싱합니다. 비어 있으면 {0}.
	//***************************************************************************
	std::vector<uint32> ParseHeroList(const _tstring& text)
	{
		std::vector<uint32> out;
		const TCHAR* p = text.c_str();
		while( *p != _T('\0') )
		{
			TCHAR* end = nullptr;
			const unsigned long v = _tcstoul(p, &end, 10);
			if( end == p )
				break;
			out.push_back(static_cast<uint32>(v));
			p = end;
			if( *p == _T(',') )
				++p;
		}
		if( out.empty() )
			out.push_back(0);
		return out;
	}

	void Accumulate(SBotStats& total, const SBotStats& s)
	{
		total.recvPackets += s.recvPackets;		total.recvBytes += s.recvBytes;
		total.sentPackets += s.sentPackets;		total.sentBytes += s.sentBytes;
		total.throttled += s.throttled;			total.sendFailed += s.sendFailed;
		total.unknownPackets += s.unknownPackets;
		total.moveReqs += s.moveReqs;			total.attackReqs += s.attackReqs;
		total.skillReqs += s.skillReqs;			total.skillCasts += s.skillCasts;
		total.skillHits += s.skillHits;
		for( size_t i = 0; i < 8; ++i )
			total.skillRejects[i] += s.skillRejects[i];
		for( int i = 0; i < 8; ++i )
		{
			total.attackRejects[i] += s.attackRejects[i];
			total.posCorrects[i] += s.posCorrects[i];
		}
		total.damageDealt += s.damageDealt;		total.damageTaken += s.damageTaken;
		total.hits += s.hits;					total.kills += s.kills;
		total.deaths += s.deaths;				total.respawns += s.respawns;
		total.zoneLost += s.zoneLost;
		for( size_t i = 0; i < total.zcRecv.size(); ++i )
			total.zcRecv[i] += s.zcRecv[i];
		for( size_t i = 0; i < total.gwRecv.size(); ++i )
			total.gwRecv[i] += s.gwRecv[i];
	}

	unsigned long long U(uint64 v) { return static_cast<unsigned long long>(v); }

	uint32 Percentile(const std::vector<uint32>& sorted, double q)
	{
		if( sorted.empty() )
			return 0;
		const size_t idx = static_cast<size_t>(q * static_cast<double>(sorted.size() - 1) + 0.5);
		return sorted[std::min(idx, sorted.size() - 1)];
	}
}

void MainClose()
{
	BaseGlobal::Destroy();
	CSocketUtils::Clear();
}

int _tmain(int argc, wchar_t* argv[])
{
#ifdef	_MSC_VER
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	InitUtf8Console();

	CSocketUtils::Init();
	BaseGlobal::Init();

	::SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	// 설정 — 우선순위: 명령행 인자 > JSON 설정 파일 > SBotClientOptions 기본값
	int exitCode = 0;
	if( !ParseArgs(argc, argv, exitCode) )
	{
		MainClose();
		return exitCode;
	}

	const _tstring host = g_cfg.host;
	const uint32 port = g_cfg.port;
	const uint32 botCount = static_cast<uint32>(g_cfg.botCount);
	const uint32 zoneId = static_cast<uint32>(g_cfg.zoneId);
	const std::vector<uint32> heroes = ParseHeroList(g_cfg.heroes);
	const uint64 charIdBase = static_cast<uint64>(g_cfg.charIdBase);
	const uint32 rampMs = static_cast<uint32>(g_cfg.rampMs);
	const uint32 tickMs = static_cast<uint32>(g_cfg.tickMs);
	const uint32 moveMs = static_cast<uint32>(g_cfg.moveIntervalMs);
	const uint32 attackMs = static_cast<uint32>(g_cfg.attackIntervalMs);
	const uint32 skillId = static_cast<uint32>(g_cfg.skillId);
	const uint32 skillMs = static_cast<uint32>(g_cfg.skillIntervalMs);
	const uint32 workers = static_cast<uint32>(g_cfg.workerCount);
	const uint32 statsSec = static_cast<uint32>(g_cfg.statsSec);
	const uint32 durationSec = static_cast<uint32>(g_cfg.durationSec);

	// 장애물 맵(선택). 서버가 읽는 Block.json과 같은 파일을 읽어 모든 봇이 공유한다.
	std::shared_ptr<const CBlockMap> blockMap;
	if( !g_cfg.blockFile.empty() )
	{
		if( !LoadBlockMapFile(g_cfg.blockFile, zoneId, g_cfg.areaMinX, g_cfg.areaMinY, g_cfg.areaMaxX, g_cfg.areaMaxY, CBlockMap::kDefaultCellSize, blockMap) )
		{
			LOG_ERROR(_T("block map load failed. file=%s"), g_cfg.blockFile.c_str());
			MainClose();
			return -1;
		}
		if( blockMap == nullptr )
			LOG_WARNING(_T("no obstacles for zone %u in %s"), zoneId, g_cfg.blockFile.c_str());
	}

	// 봇 슬롯 준비 (연결 전에 모두 만들어 둔다 — 세션 콜백이 슬롯을 참조한다)
	std::vector<std::unique_ptr<SBotSlot>> slots;
	slots.reserve(botCount);
	for( uint32 i = 0; i < botCount; ++i )
	{
		SBotConfig cfg;
		cfg.charId = charIdBase + i;
		cfg.zoneId = zoneId;
		cfg.heroId = heroes[i % heroes.size()];
		cfg.loginDelayMs = i * rampMs;
		cfg.moveIntervalMs = moveMs;
		cfg.attackIntervalMs = attackMs;
		cfg.skillId = skillId;
		cfg.skillIntervalMs = skillMs;
		cfg.blockMap = blockMap;

		auto slot = std::make_unique<SBotSlot>();
		SBotSlot* raw = slot.get();
		slot->brain = std::make_unique<CBotBrain>(cfg, [raw](const void* data, uint16 size)
			{
				// 브레인 호출은 항상 raw->mtx를 쥔 상태에서 일어난다.
				std::shared_ptr<CIocpSession> session = raw->session.lock();
				return session != nullptr && session->Send(data, size);
			});
		slots.push_back(std::move(slot));
	}

	std::atomic<uint32> disconnects{ 0 };
	std::atomic<uint32> connectFails{ 0 };
	std::atomic<uint32> nextSlot{ 0 };

	// 클라이언트 서비스 생성 + 구동
	CIocpCoreRef core = MakeShared<CIocpCore>();
	SessionFactory factory = [&]() -> CSessionRef
		{
			const uint32 index = nextSlot.fetch_add(1);
			if( index >= slots.size() )
				return nullptr;
			return std::make_shared<CBotSession>(slots[index].get(), &disconnects, &connectFails);
		};

	EngineCoreRef engine = core;
	CNetServiceRef service = CNetworkFactory::CreateClientService(
		engine, CNetAddress(host, static_cast<uint16>(port)), factory, static_cast<int32>(botCount), workers);

	std::shared_ptr<CIocpClientService> clientService = std::static_pointer_cast<CIocpClientService>(service);
	if( clientService == nullptr )
	{
		LOG_ERROR(_T("CreateClientService failed."));
		MainClose();
		return -1;
	}

	// 작은 패킷을 자주 주고받으므로 Nagle을 끄고, 조용히 끊긴 연결은 keep-alive로 감지한다.
	CIocpSession::SocketOptions socketOptions;
	socketOptions.noDelay = true;
	socketOptions.keepAlive = true;
	clientService->SetSessionSocketOptions(socketOptions);

	if( !clientService->Start() )
	{
		LOG_ERROR(_T("CIocpClientService::Start failed. host=%s port=%u bots=%u"), host.c_str(), port, botCount);
		MainClose();
		return -1;
	}

	std::printf("[bot] started. bots=%u zone=%u port=%u workers=%u tick=%ums\n", botCount, zoneId, port, workers, tickMs);

	// 틱 스레드: 봇 전체의 행동 결정을 한 스레드에서 돌린다
	std::atomic<bool> tickStop{ false };
	std::thread tickThread([&]()
		{
			while( !tickStop.load() )
			{
				const uint64 now = NowMs();
				for( auto& slot : slots )
				{
					std::lock_guard<std::recursive_mutex> lock(slot->mtx);
					slot->brain->Tick(now);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(tickMs));
			}
		});

	// 메인 스레드: 통계 출력
	const uint64 startMs = NowMs();
	uint64 lastReportMs = startMs;
	SBotStats prev;
	bool anyFailed = false;

	auto report = [&](bool isFinal)
		{
			const uint64 now = NowMs();
			const double dt = std::max<double>(static_cast<double>(now - lastReportMs) / 1000.0, 0.001);
			lastReportMs = now;

			SBotStats total;
			uint32 stateCount[6] = {};
			std::vector<uint32> latency;
			for( auto& slot : slots )
			{
				std::lock_guard<std::recursive_mutex> lock(slot->mtx);
				const SBotStats& st = slot->brain->Stats();
				Accumulate(total, st);
				++stateCount[static_cast<size_t>(slot->brain->State())];
				latency.insert(latency.end(), st.latencyMs.begin(), st.latencyMs.end());
				slot->brain->ClearLatencySamples();
			}
			std::sort(latency.begin(), latency.end());
			if( stateCount[static_cast<size_t>(EBotState::Failed)] > 0 )
				anyFailed = true;

			std::printf("[bot] t=%llus conn=%u login=%u enter=%u inzone=%u failed=%u down=%u | disc=%u connfail=%u zonelost=%llu\n",
				U((now - startMs) / 1000),
				stateCount[(size_t)EBotState::Connected], stateCount[(size_t)EBotState::LoginSent], stateCount[(size_t)EBotState::EnterSent],
				stateCount[(size_t)EBotState::InZone], stateCount[(size_t)EBotState::Failed], stateCount[(size_t)EBotState::Disconnected],
				disconnects.load(), connectFails.load(), U(total.zoneLost));
			std::printf("      recv %.0f pkt/s %.1f KB/s | sent %.0f pkt/s %.1f KB/s | move=%llu atk=%llu\n",
				static_cast<double>(total.recvPackets - prev.recvPackets) / dt, static_cast<double>(total.recvBytes - prev.recvBytes) / dt / 1024.0,
				static_cast<double>(total.sentPackets - prev.sentPackets) / dt, static_cast<double>(total.sentBytes - prev.sentBytes) / dt / 1024.0,
				U(total.moveReqs), U(total.attackReqs));
			std::printf("      hits=%llu kills=%llu deaths=%llu respawns=%llu dealt=%llu taken=%llu\n",
				U(total.hits), U(total.kills), U(total.deaths), U(total.respawns), U(total.damageDealt), U(total.damageTaken));
			std::printf("      attack-reject range/dead/invalid/invul=%llu/%llu/%llu/%llu | pos-correct mismatch/bounds/immobile/tele/dead=%llu/%llu/%llu/%llu/%llu\n",
				U(total.attackRejects[(int)EAttackRejectReason::OutOfRange]), U(total.attackRejects[(int)EAttackRejectReason::TargetDead]),
				U(total.attackRejects[(int)EAttackRejectReason::InvalidTarget]), U(total.attackRejects[(int)EAttackRejectReason::TargetInvulnerable]),
				U(total.posCorrects[(int)ECorrectPosReason::PositionMismatch]), U(total.posCorrects[(int)ECorrectPosReason::OutOfBounds]),
				U(total.posCorrects[(int)ECorrectPosReason::Immobile]), U(total.posCorrects[(int)ECorrectPosReason::Teleported]),
				U(total.posCorrects[(int)ECorrectPosReason::Dead]));
			if( g_cfg.skillId != 0 )
				std::printf("      skill req=%llu cast=%llu hit=%llu | reject unknown/dead/cooldown/range/los/invalid=%llu/%llu/%llu/%llu/%llu/%llu\n",
					U(total.skillReqs), U(total.skillCasts), U(total.skillHits),
					U(total.skillRejects[(int)ESkillRejectReason::UnknownSkill]), U(total.skillRejects[(int)ESkillRejectReason::CasterDead]),
					U(total.skillRejects[(int)ESkillRejectReason::Cooldown]), U(total.skillRejects[(int)ESkillRejectReason::OutOfRange]),
					U(total.skillRejects[(int)ESkillRejectReason::NoLineOfSight]), U(total.skillRejects[(int)ESkillRejectReason::InvalidTarget]));
			std::printf("      throttled=%llu sendfail=%llu unknown=%llu | attack->damage latency ms p50/p95/p99/max=%u/%u/%u/%u (n=%zu)\n",
				U(total.throttled), U(total.sendFailed), U(total.unknownPackets),
				Percentile(latency, 0.50), Percentile(latency, 0.95), Percentile(latency, 0.99), latency.empty() ? 0u : latency.back(), latency.size());

			if( isFinal )
			{
				std::printf("      recv by type: move=%llu stop=%llu spawn=%llu despawn=%llu correct=%llu damage=%llu death=%llu respawn=%llu reject=%llu heal=%llu\n",
					U(total.zcRecv[2]), U(total.zcRecv[3]), U(total.zcRecv[4]), U(total.zcRecv[5]), U(total.zcRecv[6]),
					U(total.zcRecv[8]), U(total.zcRecv[9]), U(total.zcRecv[10]), U(total.zcRecv[11]), U(total.zcRecv[12]));
			}
			prev = total;
		};

	while( !g_stop.load() )
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		const uint64 now = NowMs();
		if( durationSec != 0 && now - startMs >= static_cast<uint64>(durationSec) * 1000 )
			break;
		if( now - lastReportMs >= static_cast<uint64>(statsSec) * 1000 )
			report(false);
	}

	report(true);

	// 종료: 틱 정지 -> 서비스 종료(세션 정리)
	g_stop.store(true);
	tickStop.store(true);
	tickThread.join();

	clientService->Close();
	clientService = nullptr;
	service = nullptr;
	core = nullptr;
	slots.clear();

	exitCode = (anyFailed || disconnects.load() > 0 || connectFails.load() > 0) ? 2 : 0;
	MainClose();
	return exitCode;
}