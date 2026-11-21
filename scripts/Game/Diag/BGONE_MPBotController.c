/*!
	\brief Headless MP QA bot controller, single-file RplRpc table, identity scenario, and
	two-phase server deploy helper for BGONE.

	Always compiled (never conditionally compiled behind diag guards) and strictly runtime-gated on
	-bgoneMpBot so shipping retail sessions pay zero overhead and server/client RPC ordinals
	always match on 1.9.0.37.

	Log contract (parsed by tools/qa/mp-run.ps1 and tools/qa/engines.json):
	  CLIENT: BGONE_MPBOT | ARMED | ...
	          BGONE_MPBOT | SCENARIO | <name>
	          BGONE_MPBOT | CONNECTED | localPlayerId=<n> onClient=<0|1>
	          BGONE_MPBOT | VERDICT | PASS|FAIL | <scenario> | <detail>
	  SERVER: BGONE_MPBOT | SERVER_ACK | <scenario> | PASS|FAIL | <detail>
*/
modded class SCR_PlayerController : PlayerController
{
	static const string BGONE_MPBOT_SCENARIO_IDENTITY = "identity";
	static const string BGONE_MPBOT_SCENARIO_BATCH = "batch";
	static const int BGONE_MPBOT_MAX_CLIENTS = 4;

	protected bool m_bBGONE_BotDone;
	protected bool m_bBGONE_BotCliChecked;
	protected bool m_bBGONE_BotActive;
	protected string m_sBGONE_BotBatchScenario;
	protected int m_iBGONE_BotClientIndex;
	protected int m_iBGONE_BotNoSpawnPolls;

	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float timeSlice)
	{
		super.OnUpdate(timeSlice);
		BGONE_BotTick();
	}

	//------------------------------------------------------------------------------------------------
	//! Runs on the local client/host controller only; zero work when -bgoneMpBot is absent.
	protected void BGONE_BotTick()
	{
		if (m_bBGONE_BotDone)
			return;

		if (this != GetGame().GetPlayerController())
			return;

		if (!m_bBGONE_BotCliChecked)
		{
			m_bBGONE_BotCliChecked = true;
			m_bBGONE_BotActive = BGONE_BotCliEnabled();
			if (m_bBGONE_BotActive)
				Print("BGONE_MPBOT | ARMED | waiting for local player id", LogLevel.NORMAL);
		}

		if (!m_bBGONE_BotActive)
		{
			m_bBGONE_BotDone = true;
			return;
		}

		int localId = GetPlayerId();
		if (localId <= 0)
			return;

		m_bBGONE_BotDone = true;

		int onClient = 0;
		if (!Replication.IsServer())
			onClient = 1;

		string scenarioName = BGONE_BotScenario();
		Print(string.Format("BGONE_MPBOT | SCENARIO | %1", scenarioName), LogLevel.NORMAL);
		Print(string.Format("BGONE_MPBOT | CONNECTED | localPlayerId=%1 onClient=%2", localId, onClient), LogLevel.NORMAL);

		if (scenarioName == BGONE_MPBOT_SCENARIO_IDENTITY)
		{
			BGONE_BotStartIdentity();
			return;
		}

		BGONE_BotBeginScenario(scenarioName);
	}

	//------------------------------------------------------------------------------------------------
	protected bool BGONE_BotCliEnabled()
	{
		string cliVal;
		return System.GetCLIParam("bgoneMpBot", cliVal);
	}

	//------------------------------------------------------------------------------------------------
	string BGONE_BotScenario()
	{
		string cliVal;
		if (System.GetCLIParam("bgoneMpScenario", cliVal) && !cliVal.IsEmpty())
		{
			if (cliVal == BGONE_MPBOT_SCENARIO_BATCH && Replication.IsServer() && !m_sBGONE_BotBatchScenario.IsEmpty())
				return m_sBGONE_BotBatchScenario;

			return cliVal;
		}

		return BGONE_MPBOT_SCENARIO_IDENTITY;
	}

	//------------------------------------------------------------------------------------------------
	int BGONE_BotClientIndex()
	{
		if (Replication.IsServer() && m_iBGONE_BotClientIndex > 0)
			return m_iBGONE_BotClientIndex;

		string cliVal;
		if (!System.GetCLIParam("bgoneMpClient", cliVal))
			return 1;

		int parsed = cliVal.ToInt();
		if (parsed < 1)
			return 1;

		return parsed;
	}

	//------------------------------------------------------------------------------------------------
	//! Overridden by BGONE_MPBotDriver.c to dispatch beat-driven scenarios.
	protected void BGONE_BotBeginScenario(string scenarioName)
	{
		Print(string.Format(
			"BGONE_MPBOT | VERDICT | FAIL | %1 | unknown scenario - no bot layer claimed this name",
			scenarioName), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	protected void BGONE_BotStartIdentity()
	{
		if (Replication.IsServer())
		{
			RpcAsk_BGONE_BotPing();
			return;
		}

		Rpc(RpcAsk_BGONE_BotPing);
	}

	// ================================================================================================
	// ALL BOT RplRpc DECLARATIONS LIVE IN THIS SINGLE FILE SO SERVER AND CLIENT ORDINALS ALWAYS MATCH.
	// ================================================================================================

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_BGONE_BotPing()
	{
		if (!Replication.IsServer())
			return;

		if (!BGONE_BotCliEnabled())
			return;

		int derivedId = GetPlayerId();
		Print(string.Format(
			"BGONE_MPBOT | SERVER_ACK | identity | PASS | derivedPlayerId=%1", derivedId), LogLevel.NORMAL);

		if (this == GetGame().GetPlayerController())
		{
			RpcDo_BGONE_BotAck(derivedId);
			return;
		}

		Rpc(RpcDo_BGONE_BotAck, derivedId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_BGONE_BotAck(int serverDerivedId)
	{
		if (!BGONE_BotCliEnabled())
			return;

		int localId = GetPlayerId();
		if (serverDerivedId > 0 && serverDerivedId == localId)
		{
			Print(string.Format(
				"BGONE_MPBOT | VERDICT | PASS | identity | local=%1 server=%2", localId, serverDerivedId), LogLevel.NORMAL);
			return;
		}

		Print(string.Format(
			"BGONE_MPBOT | VERDICT | FAIL | identity | local=%1 server=%2", localId, serverDerivedId), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_BGONE_BotPrepare(string scenarioName, int clientIndex)
	{
		if (!Replication.IsServer())
			return;

		if (!BGONE_BotCliEnabled())
			return;

		string serverScenario;
		if (System.GetCLIParam("bgoneMpScenario", serverScenario) && serverScenario == BGONE_MPBOT_SCENARIO_BATCH)
		{
			string allowed = "abcdefghijklmnopqrstuvwxyz0123456789_";
			bool plain = !scenarioName.IsEmpty() && scenarioName.Length() <= 40;
			for (int i = 0; plain && i < scenarioName.Length(); i++)
			{
				string ch = scenarioName.Get(i);
				ch.ToLower();
				if (!allowed.Contains(ch))
					plain = false;
			}

			if (plain)
				m_sBGONE_BotBatchScenario = scenarioName;
		}

		if (clientIndex >= 1 && clientIndex <= BGONE_MPBOT_MAX_CLIENTS)
			m_iBGONE_BotClientIndex = clientIndex;

		BGONE_BotOnPrepare();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_BGONE_BotReady(int status, RplId fixtureId, EntityID fixtureEntity, vector fixturePos, string detail)
	{
		if (!BGONE_BotCliEnabled())
			return;

		BGONE_BotOnReady(status, fixtureId, fixtureEntity, fixturePos, detail);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_BGONE_BotQuery(int queryId, int seq)
	{
		if (!Replication.IsServer())
			return;

		if (!BGONE_BotCliEnabled())
			return;

		BGONE_BotOnQuery(queryId, seq);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_BGONE_BotAnswer(int queryId, int seq, int ok, vector val, string detail)
	{
		if (!BGONE_BotCliEnabled())
			return;

		BGONE_BotOnAnswer(queryId, seq, ok, val, detail);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_BGONE_BotEnd(int passed, string detail)
	{
		if (!Replication.IsServer())
			return;

		if (!BGONE_BotCliEnabled())
			return;

		BGONE_BotOnEnd(passed, detail);
	}

	//------------------------------------------------------------------------------------------------
	// Virtual hooks implemented by BGONE_MPBotDriver.c.
	protected void BGONE_BotOnPrepare();
	protected void BGONE_BotOnReady(int status, RplId fixtureId, EntityID fixtureEntity, vector fixturePos, string detail);
	protected void BGONE_BotOnQuery(int queryId, int seq);
	protected void BGONE_BotOnAnswer(int queryId, int seq, int ok, vector val, string detail);
	protected void BGONE_BotOnEnd(int passed, string detail);

	// ================================================================================================
	// RELIABLE TWO-PHASE SERVER DEPLOY HELPERS
	// ================================================================================================

	//------------------------------------------------------------------------------------------------
	IEntity BGONE_BotControlledEntityServer(int playerId)
	{
		if (playerId <= 0)
			return null;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return null;

		return pm.GetPlayerControlledEntity(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Checks GetPlayerControlledEntity first (GameMode_Plain may auto-spawn); otherwise runs
	//! Phase 1 (EnsureFactionAndLoadout) on one poll and Phase 2 (RequestSpawn with SkipPreload) on
	//! a subsequent poll. Returns true once the player controls an entity.
	bool BGONE_BotEnsureDeployedServer(int playerId, int pollAttempt, inout int spawnRequests, inout int lastRequestPoll)
	{
		if (!Replication.IsServer())
			return false;

		IEntity existing = BGONE_BotControlledEntityServer(playerId);
		if (existing)
			return true;

		if (BGONE_BotEnsureFactionAndLoadoutServer(playerId))
		{
			if (spawnRequests == 0 || (pollAttempt - lastRequestPoll) >= 20)
			{
				lastRequestPoll = pollAttempt;
				BGONE_BotRequestSpawnServer(playerId, spawnRequests);
				spawnRequests = spawnRequests + 1;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Phase 1: ensure faction and deterministic first loadout are resolved before requesting spawn.
	bool BGONE_BotEnsureFactionAndLoadoutServer(int playerId)
	{
		if (!Replication.IsServer())
			return false;

		if (playerId <= 0)
			return false;

		FactionManager factionManager = GetGame().GetFactionManager();
		if (!factionManager)
			return false;

		SCR_PlayerFactionAffiliationComponent factionComponent = SCR_PlayerFactionAffiliationComponent.Cast(FindComponent(SCR_PlayerFactionAffiliationComponent));
		SCR_PlayerLoadoutComponent loadoutComponent = SCR_PlayerLoadoutComponent.Cast(FindComponent(SCR_PlayerLoadoutComponent));
		if (!factionComponent || !loadoutComponent)
			return false;

		Faction faction = factionComponent.GetAffiliatedFaction();
		if (!faction)
		{
			array<Faction> factions = {};
			factionManager.GetFactionsList(factions);

			Faction wanted;
			foreach (Faction candidate : factions)
			{
				if (!candidate)
					continue;

				if (SCR_SpawnPoint.GetSpawnPointCountForFaction(candidate.GetFactionKey()) > 0)
				{
					wanted = candidate;
					break;
				}
			}

			if (!wanted)
			{
				m_iBGONE_BotNoSpawnPolls = m_iBGONE_BotNoSpawnPolls + 1;
				if (m_iBGONE_BotNoSpawnPolls % 20 == 1)
				{
					Print(string.Format(
						"BGONE_MPBOT | DEPLOY | no faction has a spawn point yet (%1 total)",
						SCR_SpawnPoint.GetSpawnPoints().Count()), LogLevel.WARNING);
				}

				return false;
			}

			Print(string.Format(
				"BGONE_MPBOT | DEPLOY | requesting faction %1 for player %2",
				wanted.GetFactionKey(), playerId), LogLevel.NORMAL);
			factionComponent.RequestFaction(wanted);
			return false;
		}

		if (!loadoutComponent.GetLoadout())
		{
			SCR_LoadoutManager loadoutManager = GetGame().GetLoadoutManager();
			if (!loadoutManager)
				return false;

			array<ref SCR_BasePlayerLoadout> loadouts = {};
			loadoutManager.GetPlayerLoadoutsByFaction(faction, loadouts);
			if (loadouts.IsEmpty())
			{
				Print(string.Format(
					"BGONE_MPBOT | DEPLOY | faction %1 offers no loadouts",
					faction.GetFactionKey()), LogLevel.WARNING);
				return false;
			}

			SCR_BasePlayerLoadout wantedLoadout = loadouts[0];
			Print(string.Format(
				"BGONE_MPBOT | DEPLOY | requesting loadout '%1' for player %2 (faction %3)",
				wantedLoadout.GetLoadoutName(), playerId, faction.GetFactionKey()), LogLevel.NORMAL);
			loadoutComponent.RequestLoadout(wantedLoadout);
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Phase 2: request spawn at a reservable faction spawn point with SetSkipPreload(true).
	bool BGONE_BotRequestSpawnServer(int playerId, int skipUsable)
	{
		if (!Replication.IsServer())
			return false;

		if (playerId <= 0)
			return false;

		SCR_PlayerFactionAffiliationComponent factionComponent = SCR_PlayerFactionAffiliationComponent.Cast(FindComponent(SCR_PlayerFactionAffiliationComponent));
		SCR_PlayerLoadoutComponent loadoutComponent = SCR_PlayerLoadoutComponent.Cast(FindComponent(SCR_PlayerLoadoutComponent));
		if (!factionComponent || !loadoutComponent)
			return false;

		Faction faction = factionComponent.GetAffiliatedFaction();
		SCR_BasePlayerLoadout loadout = loadoutComponent.GetLoadout();
		if (!faction || !loadout)
			return false;

		SCR_RespawnComponent respawnComponent = SCR_RespawnComponent.SGetPlayerRespawnComponent(playerId);
		if (!respawnComponent)
			return false;

		array<SCR_SpawnPoint> points = SCR_SpawnPoint.GetSpawnPointsForFaction(faction.GetFactionKey());
		if (!points || points.IsEmpty())
			return false;

		array<SCR_SpawnPoint> usable = {};
		foreach (SCR_SpawnPoint candidate : points)
		{
			if (!candidate)
				continue;

			if (!candidate.IsSpawnPointEnabled())
				continue;

			SCR_ESpawnResult reserveResult;
			if (!candidate.CanReserveFor_S(playerId, reserveResult))
				continue;

			usable.Insert(candidate);
		}

		if (usable.IsEmpty())
			return false;

		int idx = skipUsable % usable.Count();
		SCR_SpawnPoint chosen = usable[idx];

		SCR_SpawnPointSpawnData spawnData = new SCR_SpawnPointSpawnData(loadout.GetLoadoutResource(), chosen.GetRplId());
		spawnData.SetSkipPreload(true);

		if (!respawnComponent.CanSpawn(spawnData))
			return false;

		Print(string.Format(
			"BGONE_MPBOT | DEPLOY | requesting spawn for player %1 (faction %2) at spawn point %3/%4",
			playerId, faction.GetFactionKey(), idx, usable.Count()), LogLevel.NORMAL);

		return respawnComponent.RequestSpawn(spawnData);
	}
}
