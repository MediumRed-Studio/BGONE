/*!
	\brief Headless MP bot beat driver, spawn ledger, and server staging/query/cleanup hooks for BGONE.

	All client and server deadlines tick from SCR_PlayerController.OnUpdate (never CallLater for deadlines).
	Structured logs:
	  BGONE_MPBOT | RUN | <scenario> | <n> beat(s) | ...
	  BGONE_MPBOT | BEAT | <n> | <name> | START|PASS|FAIL [| <detail>]
	  BGONE_MPBOT | TELEMETRY | <key>=<value> ...
	  BGONE_MPBOT | VERDICT | PASS|FAIL | <scenario> | <detail>
*/
enum EBGONE_BotBeatState
{
	RUNNING,
	PASS,
	FAIL
}

//------------------------------------------------------------------------------------------------
//! Base class for one headless scenario beat.
class BGONE_BotBeat
{
	string m_sName;
	int m_iTimeoutMs;
	bool m_bIsSetup;
	string m_sDetail;
	string m_sFailSay;

	//------------------------------------------------------------------------------------------------
	protected void Setup(string beatName, int timeoutMs)
	{
		m_sName = beatName;
		m_iTimeoutMs = timeoutMs;
		m_sFailSay = "This step did not work.";
	}

	//------------------------------------------------------------------------------------------------
	protected void SetFailSay(string text)
	{
		m_sFailSay = text;
	}

	//------------------------------------------------------------------------------------------------
	protected void MarkSetup()
	{
		m_bIsSetup = true;
	}

	//------------------------------------------------------------------------------------------------
	void OnStart(BGONE_BotRun run);

	//------------------------------------------------------------------------------------------------
	EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("timed out after %1 ms", m_iTimeoutMs);
	}
}

//------------------------------------------------------------------------------------------------
//! Tracks server-spawned scenario entities so -OneServer sweeps can clean up prior scenarios cleanly.
class BGONE_BotSpawnLedger
{
	protected static ref array<IEntity> s_aEntities;
	protected static ref array<string> s_aScenarios;

	//------------------------------------------------------------------------------------------------
	static void Add(IEntity entity, string scenarioName)
	{
		if (!entity)
			return;

		if (!s_aEntities)
		{
			s_aEntities = {};
			s_aScenarios = {};
		}

		s_aEntities.Insert(entity);
		s_aScenarios.Insert(scenarioName);
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes everything spawned by scenarios OTHER than `scenarioName`.
	static int ClearOthers(string scenarioName)
	{
		if (!s_aEntities)
			return 0;

		int deleted = 0;
		for (int i = s_aEntities.Count() - 1; i >= 0; i--)
		{
			if (s_aScenarios[i] == scenarioName)
				continue;

			IEntity ent = s_aEntities[i];
			if (ent)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(ent);
				deleted = deleted + 1;
			}

			s_aEntities.Remove(i);
			s_aScenarios.Remove(i);
		}

		return deleted;
	}

	//------------------------------------------------------------------------------------------------
	//! Deletes everything spawned by `scenarioName`.
	static int ClearScenario(string scenarioName)
	{
		if (!s_aEntities)
			return 0;

		int deleted = 0;
		for (int i = s_aEntities.Count() - 1; i >= 0; i--)
		{
			if (s_aScenarios[i] != scenarioName)
				continue;

			IEntity ent = s_aEntities[i];
			if (ent)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(ent);
				deleted = deleted + 1;
			}

			s_aEntities.Remove(i);
			s_aScenarios.Remove(i);
		}

		return deleted;
	}
}

//------------------------------------------------------------------------------------------------
//! Client-side state machine for one beat-driven MP QA scenario.
class BGONE_BotRun
{
	static const int QUERY_SERVER_POSITION = 1;
	static const int QUERY_SERVER_FIXTURE_POS = 2;
	static const int QUERY_START_POSITION = 3;

	static const int READY_TIMEOUT_MS = 120000;
	static const int CHARACTER_TIMEOUT_MS = 60000;
	static const int RENDER_SETTLE_MS = 3000;
	static const int FPS_EVERY_MS = 2000;

	string m_sScenario;
	string m_sTitle;
	SCR_PlayerController m_Controller;
	ref array<ref BGONE_BotBeat> m_aBeats;

	RplId m_FixtureId;
	EntityID m_FixtureEntityId;
	vector m_vFixturePos;
	string m_sFixtureDetail;
	string m_sSummary;

	protected bool m_bReady;
	protected bool m_bFinished;
	protected int m_iStartTick;
	protected int m_iLastFpsTick;
	protected int m_iBeatIndex;
	protected int m_iBeatStartTick;
	protected bool m_bBeatStarted;
	protected IEntity m_Character;
	protected int m_iCharacterTick;
	protected bool m_bSettled;
	protected bool m_bPlayLogged;
	protected int m_iDwellUntil;
	protected int m_iPaceMs;

	protected int m_iNextSeq;
	protected ref map<int, int> m_mAnswerOk;
	protected ref map<int, vector> m_mAnswerValue;
	protected ref map<int, string> m_mAnswerDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotRun(string scenarioName)
	{
		m_sScenario = scenarioName;
		m_aBeats = {};
		m_FixtureId = RplId.Invalid();
		m_FixtureEntityId = EntityID.INVALID;
		m_mAnswerOk = new map<int, int>();
		m_mAnswerValue = new map<int, vector>();
		m_mAnswerDetail = new map<int, string>();

		string paceVal;
		if (System.GetCLIParam("bgoneBotPace", paceVal))
			m_iPaceMs = paceVal.ToInt();
	}

	//------------------------------------------------------------------------------------------------
	void Add(BGONE_BotBeat beat)
	{
		if (beat)
			m_aBeats.Insert(beat);
	}

	//------------------------------------------------------------------------------------------------
	bool IsFinished()
	{
		return m_bFinished;
	}

	//------------------------------------------------------------------------------------------------
	void LogTelemetry(string payload)
	{
		Print(string.Format("BGONE_MPBOT | TELEMETRY | %1", payload), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	IEntity FixtureEntity()
	{
		if (!m_FixtureId.IsValid())
		{
			if (m_FixtureEntityId == EntityID.INVALID)
				return null;

			BaseWorld world = GetGame().GetWorld();
			if (!world)
				return null;

			return world.FindEntityByID(m_FixtureEntityId);
		}

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_FixtureId));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	ChimeraCharacter Character()
	{
		if (!m_Controller)
			return null;

		return ChimeraCharacter.Cast(m_Controller.GetControlledEntity());
	}

	//------------------------------------------------------------------------------------------------
	SCR_CharacterControllerComponent CharacterController()
	{
		ChimeraCharacter ch = Character();
		if (!ch)
			return null;

		return SCR_CharacterControllerComponent.Cast(ch.GetCharacterController());
	}

	//------------------------------------------------------------------------------------------------
	vector Position()
	{
		ChimeraCharacter ch = Character();
		if (!ch)
			return vector.Zero;

		return ch.GetOrigin();
	}

	//------------------------------------------------------------------------------------------------
	float Yaw()
	{
		ChimeraCharacter ch = Character();
		if (!ch)
			return 0;

		return ch.GetYawPitchRoll()[0];
	}

	//------------------------------------------------------------------------------------------------
	int Query(int queryId)
	{
		m_iNextSeq = m_iNextSeq + 1;
		m_Controller.BGONE_BotSendQuery(queryId, m_iNextSeq);
		return m_iNextSeq;
	}

	//------------------------------------------------------------------------------------------------
	bool HasAnswer(int seq)
	{
		return m_mAnswerOk.Contains(seq);
	}

	//------------------------------------------------------------------------------------------------
	bool AnswerOk(int seq)
	{
		return m_mAnswerOk.Get(seq) == 1;
	}

	//------------------------------------------------------------------------------------------------
	vector AnswerValue(int seq)
	{
		return m_mAnswerValue.Get(seq);
	}

	//------------------------------------------------------------------------------------------------
	string AnswerDetail(int seq)
	{
		return m_mAnswerDetail.Get(seq);
	}

	//------------------------------------------------------------------------------------------------
	void OnAnswer(int seq, int ok, vector val, string detail)
	{
		m_mAnswerOk.Set(seq, ok);
		m_mAnswerValue.Set(seq, val);
		m_mAnswerDetail.Set(seq, detail);
	}

	//------------------------------------------------------------------------------------------------
	void Start(SCR_PlayerController controller)
	{
		m_Controller = controller;
		m_iStartTick = System.GetTickCount();
		m_iLastFpsTick = m_iStartTick;
		BGONE_BotInput.EndRun();
		Print(string.Format(
			"BGONE_MPBOT | RUN | %1 | %2 beat(s) | requesting authority deploy and stage",
			m_sScenario, m_aBeats.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	void OnReady(int status, RplId fixtureId, EntityID fixtureEntity, vector fixturePos, string detail)
	{
		if (m_bFinished)
			return;

		if (status != 0)
		{
			Finish(false, string.Format("authority could not stage scenario (status %1): %2", status, detail));
			return;
		}

		m_bReady = true;
		m_FixtureId = fixtureId;
		m_FixtureEntityId = fixtureEntity;
		m_vFixturePos = fixturePos;
		m_sFixtureDetail = detail;
		m_iStartTick = System.GetTickCount();
		Print(string.Format(
			"BGONE_MPBOT | READY | fixture=%1 at %2 | %3", fixtureId, fixturePos, detail), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	void Tick()
	{
		if (m_bFinished)
			return;

		int now = System.GetTickCount();
		if (now - m_iLastFpsTick >= FPS_EVERY_MS)
		{
			m_iLastFpsTick = now;
			LogTelemetry(string.Format("fps=%1", System.GetFPS()));
		}

		if (!m_bReady)
		{
			if (now - m_iStartTick > READY_TIMEOUT_MS)
				Finish(false, "authority never reported scenario staged");

			return;
		}

		if (!m_Character)
		{
			ChimeraCharacter ch = Character();
			if (!ch)
			{
				if (now - m_iStartTick > CHARACTER_TIMEOUT_MS)
					Finish(false, "local player never gained a controlled character");

				return;
			}

			m_Character = ch;
			m_iCharacterTick = now;
			Print(string.Format(
				"BGONE_MPBOT | CHARACTER | controlled at %1 yaw=%2", ch.GetOrigin(), Yaw()), LogLevel.NORMAL);
		}

		if (Character() != m_Character)
		{
			Finish(false, "local character changed mid-run (died, respawned or lost control)");
			return;
		}

		if (!m_bSettled)
		{
			if (now - m_iCharacterTick < RENDER_SETTLE_MS)
				return;

			m_bSettled = true;
			Print(string.Format(
				"BGONE_MPBOT | SETTLED | %1 ms after character controlled", RENDER_SETTLE_MS), LogLevel.NORMAL);
		}

		if (now < m_iDwellUntil)
			return;

		if (m_iBeatIndex >= m_aBeats.Count())
		{
			Finish(true, m_sSummary);
			return;
		}

		BGONE_BotBeat beat = m_aBeats[m_iBeatIndex];
		if (!m_bBeatStarted)
		{
			if (!beat.m_bIsSetup && !m_bPlayLogged)
			{
				m_bPlayLogged = true;
				if (!m_sTitle.IsEmpty())
					Print(string.Format("BGONE_MPBOT | TITLE | %1", m_sTitle), LogLevel.NORMAL);

				Print("BGONE_MPBOT | PLAY | START | the scene is set; the video starts here", LogLevel.NORMAL);
			}

			m_bBeatStarted = true;
			m_iBeatStartTick = now;
			Print(string.Format(
				"BGONE_MPBOT | BEAT | %1 | %2 | START", m_iBeatIndex + 1, beat.m_sName), LogLevel.NORMAL);
			beat.OnStart(this);
		}

		int elapsed = now - m_iBeatStartTick;
		EBGONE_BotBeatState st = beat.Tick(this, elapsed);
		if (st == EBGONE_BotBeatState.RUNNING && elapsed > beat.m_iTimeoutMs)
		{
			st = EBGONE_BotBeatState.FAIL;
			beat.m_sDetail = beat.TimeoutDetail(this);
		}

		if (st == EBGONE_BotBeatState.RUNNING)
			return;

		BGONE_BotInput.ReleaseAll();

		if (st == EBGONE_BotBeatState.PASS)
		{
			Print(string.Format(
				"BGONE_MPBOT | BEAT | %1 | %2 | PASS | %3", m_iBeatIndex + 1, beat.m_sName, beat.m_sDetail), LogLevel.NORMAL);
			m_iBeatIndex = m_iBeatIndex + 1;
			m_bBeatStarted = false;
			if (!beat.m_bIsSetup)
				m_iDwellUntil = now + m_iPaceMs;

			return;
		}

		Print(string.Format(
			"BGONE_MPBOT | BEAT | %1 | %2 | FAIL | %3 | SAY: %4",
			m_iBeatIndex + 1, beat.m_sName, beat.m_sDetail, beat.m_sFailSay), LogLevel.WARNING);
		Finish(false, string.Format("beat %1 '%2' failed: %3", m_iBeatIndex + 1, beat.m_sName, beat.m_sDetail));
	}

	//------------------------------------------------------------------------------------------------
	void Finish(bool pass, string detail)
	{
		if (m_bFinished)
			return;

		m_bFinished = true;
		BGONE_BotInput.EndRun();

		if (m_Controller)
			m_Controller.BGONE_BotSendEnd(pass, detail);

		if (pass)
		{
			Print(string.Format(
				"BGONE_MPBOT | VERDICT | PASS | %1 | %2 beat(s) passed. %3",
				m_sScenario, m_aBeats.Count(), detail), LogLevel.NORMAL);
			return;
		}

		Print(string.Format(
			"BGONE_MPBOT | VERDICT | FAIL | %1 | %2", m_sScenario, detail), LogLevel.ERROR);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	static const float BGONE_BOT_DEPLOY_POLL_SEC = 1.0;
	static const int BGONE_BOT_DEPLOY_MAX_TRIES = 180;
	static const float BGONE_BOT_SETTLE_SEC = 2.0;

	protected ref BGONE_BotRun m_BGONE_BotRun;

	// Server-side OnUpdate deploy/stage state machine (never relies on CallLater for deadlines).
	protected bool m_bBGONE_ServerDeploying;
	protected bool m_bBGONE_ServerSettling;
	protected float m_fBGONE_ServerTimerSec;
	protected int m_iBGONE_ServerDeployTries;
	protected int m_iBGONE_ServerSpawnRequests;
	protected int m_iBGONE_ServerLastRequestPoll;
	protected IEntity m_BGONE_ServerFixture;
	protected EntityID m_BGONE_ServerStagedEntityId;
	protected vector m_vBGONE_ServerStartPos;

	//------------------------------------------------------------------------------------------------
	override void OnUpdate(float timeSlice)
	{
		super.OnUpdate(timeSlice);

		if (m_BGONE_BotRun && this == GetGame().GetPlayerController())
			m_BGONE_BotRun.Tick();

		if (Replication.IsServer() && (m_bBGONE_ServerDeploying || m_bBGONE_ServerSettling))
			BGONE_BotServerTick(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	//! CLIENT hook: future scenario files override this to return a BGONE_BotRun for their scenario name.
	protected BGONE_BotRun BGONE_BotBuildScenario(string scenarioName)
	{
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER hook: future scenario files override this to stage scenario fixtures. Return 0 on success.
	protected int BGONE_BotStageScenario(string scenarioName, IEntity character, out RplId fixtureId, out vector fixturePos, out string detail)
	{
		fixtureId = RplId.Invalid();
		fixturePos = character.GetOrigin();
		detail = "no fixtures staged";
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER hook: future scenario files override this for custom server-side assertions/queries.
	protected bool BGONE_BotAnswerQuery(int queryId, out int ok, out vector val, out string detail)
	{
		if (queryId == BGONE_BotRun.QUERY_SERVER_POSITION)
		{
			IEntity ch = BGONE_BotControlledEntityServer(GetPlayerId());
			if (!ch)
			{
				ok = 0;
				val = vector.Zero;
				detail = "no server character";
				return true;
			}

			ok = 1;
			val = ch.GetOrigin();
			detail = string.Format("server pos %1", val);
			return true;
		}

		if (queryId == BGONE_BotRun.QUERY_SERVER_FIXTURE_POS)
		{
			if (!m_BGONE_ServerFixture)
			{
				ok = 0;
				val = vector.Zero;
				detail = "no staged fixture";
				return true;
			}

			ok = 1;
			val = m_BGONE_ServerFixture.GetOrigin();
			detail = string.Format("fixture pos %1", val);
			return true;
		}

		if (queryId == BGONE_BotRun.QUERY_START_POSITION)
		{
			ok = 1;
			val = m_vBGONE_ServerStartPos;
			detail = string.Format("start pos %1", val);
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotBeginScenario(string scenarioName)
	{
		BGONE_BotRun run = BGONE_BotBuildScenario(scenarioName);
		if (!run)
		{
			super.BGONE_BotBeginScenario(scenarioName);
			return;
		}

		m_BGONE_BotRun = run;
		run.Start(this);

		if (Replication.IsServer())
		{
			RpcAsk_BGONE_BotPrepare(scenarioName, BGONE_BotClientIndex());
			return;
		}

		Rpc(RpcAsk_BGONE_BotPrepare, scenarioName, BGONE_BotClientIndex());
	}

	//------------------------------------------------------------------------------------------------
	void BGONE_BotSendQuery(int queryId, int seq)
	{
		if (Replication.IsServer())
		{
			RpcAsk_BGONE_BotQuery(queryId, seq);
			return;
		}

		Rpc(RpcAsk_BGONE_BotQuery, queryId, seq);
	}

	//------------------------------------------------------------------------------------------------
	void BGONE_BotSendEnd(bool pass, string detail)
	{
		int passed = 0;
		if (pass)
			passed = 1;

		if (Replication.IsServer())
		{
			RpcAsk_BGONE_BotEnd(passed, detail);
			return;
		}

		Rpc(RpcAsk_BGONE_BotEnd, passed, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnReady(int status, RplId fixtureId, EntityID fixtureEntity, vector fixturePos, string detail)
	{
		if (m_BGONE_BotRun)
			m_BGONE_BotRun.OnReady(status, fixtureId, fixtureEntity, fixturePos, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnAnswer(int queryId, int seq, int ok, vector val, string detail)
	{
		if (m_BGONE_BotRun)
			m_BGONE_BotRun.OnAnswer(seq, ok, val, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnPrepare()
	{
		string scenarioName = BGONE_BotScenario();
		int cleared = BGONE_BotSpawnLedger.ClearOthers(scenarioName);
		if (cleared > 0)
			Print(string.Format("BGONE_MPBOT | SERVER | cleared %1 leftover fixture(s) from prior scenario", cleared), LogLevel.NORMAL);

		m_bBGONE_ServerDeploying = true;
		m_bBGONE_ServerSettling = false;
		m_fBGONE_ServerTimerSec = BGONE_BOT_DEPLOY_POLL_SEC;
		m_iBGONE_ServerDeployTries = 0;
		m_iBGONE_ServerSpawnRequests = 0;
		m_iBGONE_ServerLastRequestPoll = 0;
		m_BGONE_ServerFixture = null;
		m_BGONE_ServerStagedEntityId = EntityID.INVALID;
		m_vBGONE_ServerStartPos = vector.Zero;

		Print(string.Format("BGONE_MPBOT | SERVER_ACK | %1 | PASS | prepare received", scenarioName), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void BGONE_BotServerTick(float timeSlice)
	{
		m_fBGONE_ServerTimerSec = m_fBGONE_ServerTimerSec + timeSlice;

		if (m_bBGONE_ServerDeploying)
		{
			if (m_fBGONE_ServerTimerSec < BGONE_BOT_DEPLOY_POLL_SEC)
				return;

			m_fBGONE_ServerTimerSec = 0;
			int playerId = GetPlayerId();
			m_iBGONE_ServerDeployTries = m_iBGONE_ServerDeployTries + 1;

			if (BGONE_BotEnsureDeployedServer(playerId, m_iBGONE_ServerDeployTries, m_iBGONE_ServerSpawnRequests, m_iBGONE_ServerLastRequestPoll))
			{
				IEntity ch = BGONE_BotControlledEntityServer(playerId);
				Print(string.Format(
					"BGONE_MPBOT | SERVER | player %1 deployed after %2 poll(s) at %3",
					playerId, m_iBGONE_ServerDeployTries, ch.GetOrigin()), LogLevel.NORMAL);
				m_bBGONE_ServerDeploying = false;
				m_bBGONE_ServerSettling = true;
				m_fBGONE_ServerTimerSec = 0;
				return;
			}

			if (m_iBGONE_ServerDeployTries >= BGONE_BOT_DEPLOY_MAX_TRIES)
			{
				m_bBGONE_ServerDeploying = false;
				BGONE_BotSendServerReady(1, RplId.Invalid(), vector.Zero,
					string.Format("player %1 never gained a character after %2 polls", playerId, m_iBGONE_ServerDeployTries));
			}

			return;
		}

		if (m_bBGONE_ServerSettling)
		{
			if (m_fBGONE_ServerTimerSec < BGONE_BOT_SETTLE_SEC)
				return;

			m_bBGONE_ServerSettling = false;
			BGONE_BotStageNow();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void BGONE_BotStageNow()
	{
		IEntity ch = BGONE_BotControlledEntityServer(GetPlayerId());
		if (!ch)
		{
			BGONE_BotSendServerReady(2, RplId.Invalid(), vector.Zero, "character vanished before staging");
			return;
		}

		RplId fixtureId = RplId.Invalid();
		vector fixturePos = vector.Zero;
		string detail = "";
		m_BGONE_ServerStagedEntityId = EntityID.INVALID;
		int st = BGONE_BotStageScenario(BGONE_BotScenario(), ch, fixtureId, fixturePos, detail);

		m_BGONE_ServerFixture = null;
		if (fixtureId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(fixtureId));
			if (rpl)
				m_BGONE_ServerFixture = rpl.GetEntity();
		}
		else if (m_BGONE_ServerStagedEntityId != EntityID.INVALID)
		{
			BaseWorld world = GetGame().GetWorld();
			if (world)
				m_BGONE_ServerFixture = world.FindEntityByID(m_BGONE_ServerStagedEntityId);
		}

		BGONE_BotSendServerReady(st, fixtureId, fixturePos, detail);
	}

	//------------------------------------------------------------------------------------------------
	protected void BGONE_BotSendServerReady(int status, RplId fixtureId, vector fixturePos, string detail)
	{
		if (this == GetGame().GetPlayerController())
		{
			RpcDo_BGONE_BotReady(status, fixtureId, m_BGONE_ServerStagedEntityId, fixturePos, detail);
			return;
		}

		Rpc(RpcDo_BGONE_BotReady, status, fixtureId, m_BGONE_ServerStagedEntityId, fixturePos, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnQuery(int queryId, int seq)
	{
		int ok = 0;
		vector val = vector.Zero;
		string detail = string.Format("unknown queryId %1", queryId);
		BGONE_BotAnswerQuery(queryId, ok, val, detail);

		if (this == GetGame().GetPlayerController())
		{
			RpcDo_BGONE_BotAnswer(queryId, seq, ok, val, detail);
			return;
		}

		Rpc(RpcDo_BGONE_BotAnswer, queryId, seq, ok, val, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnEnd(int passed, string detail)
	{
		Print(string.Format(
			"BGONE_MPBOT | SERVER | scenario %1 ended passed=%2 | %3",
			BGONE_BotScenario(), passed, detail), LogLevel.NORMAL);
	}
}
