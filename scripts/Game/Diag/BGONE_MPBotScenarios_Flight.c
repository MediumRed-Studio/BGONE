/*!
	\brief 2-client headless MP QA bot scenario `proxy_sync` verifying server-authoritative missile
	flight in EOnFrame, throttled ~20 Hz flight replication, proxy dead-reckoning convergence on
	both shooter (Client 1) and remote observer (Client 2), and single-shot detonation.

	Uses existing BGONE_BotRun.Query(int queryId) plumbing (zero new SCR_PlayerController RPCs).
*/
class BGONE_FlightBotConst
{
	static const string SCENARIO_PROXY_SYNC = "proxy_sync";
	static const ResourceName PREFAB_LAUNCHER_PLOS = "{E6E70B5FFAC5FEA4}Prefabs/Weapons/Launchers/BGONE_Launcher_PLOS.et";

	static const int QUERY_FLIGHT_MISSILE_STATE = 7201;
	static const int QUERY_FLIGHT_SYNC_READY = 7203;

	static const float MIN_FLIGHT_TIME_SEC = 0.5;
	static const float MIN_TRAVEL_DIST_M = 30.0;
	static const float MIN_PEAK_SPEED_MS = 25.0;
	static const float MAX_PEAK_SPEED_MS = 260.0;
	static const int MIN_PROXY_SYNC_COUNT = 3;
	static const float MAX_CONVERGED_SYNC_ERR_M = 2.0;
	static const int MAX_MIDFLIGHT_SNAPS = 10;
	static const float MAX_SYNC_SEND_RATE_HZ = 35.0;
}

//------------------------------------------------------------------------------------------------
//! Server-side coordinator synchronizing Client 1 (shooter) and Client 2 (remote observer)
//! for `proxy_sync` without introducing extra RPCs.
class BGONE_FlightBotCoordinator
{
	protected static const int READY_STALE_MS = 120000;

	protected static int s_iClient1ReadyTick;
	protected static int s_iClient2ReadyTick;
	protected static int s_iRunStartEpoch;
	protected static bool s_bRunStartEpochValid;

	//------------------------------------------------------------------------------------------------
	static void OnClient1Staged()
	{
		int now = System.GetTickCount();
		s_iClient1ReadyTick = 0;

		// Preserve Client 2 ready state if Client 2 already settled shortly before Client 1.
		if (s_iClient2ReadyTick > 0 && (now - s_iClient2ReadyTick) > READY_STALE_MS)
			s_iClient2ReadyTick = 0;

		s_iRunStartEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
		s_bRunStartEpochValid = true;
	}

	//------------------------------------------------------------------------------------------------
	static void MarkClientReady(int clientIndex)
	{
		int now = System.GetTickCount();
		if (clientIndex == 1)
			s_iClient1ReadyTick = now;
		else if (clientIndex == 2)
			s_iClient2ReadyTick = now;

		if (!s_bRunStartEpochValid)
		{
			s_iRunStartEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
			s_bRunStartEpochValid = true;
		}
	}

	//------------------------------------------------------------------------------------------------
	static bool IsClient1Ready()
	{
		if (s_iClient1ReadyTick <= 0)
			return false;

		return (System.GetTickCount() - s_iClient1ReadyTick) <= READY_STALE_MS;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsClient2Ready()
	{
		if (s_iClient2ReadyTick <= 0)
			return false;

		return (System.GetTickCount() - s_iClient2ReadyTick) <= READY_STALE_MS;
	}

	//------------------------------------------------------------------------------------------------
	static bool AreBothReady()
	{
		return IsClient1Ready() && IsClient2Ready();
	}

	//------------------------------------------------------------------------------------------------
	static int GetRunStartEpoch()
	{
		return s_iRunStartEpoch;
	}

	//------------------------------------------------------------------------------------------------
	static void MarkClientEnded(int clientIndex)
	{
		if (clientIndex == 1)
			s_iClient1ReadyTick = 0;
		else if (clientIndex == 2)
			s_iClient2ReadyTick = 0;
	}

	//------------------------------------------------------------------------------------------------
	static void ResetAll()
	{
		s_iClient1ReadyTick = 0;
		s_iClient2ReadyTick = 0;
		s_iRunStartEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
		s_bRunStartEpochValid = false;
	}
}

//------------------------------------------------------------------------------------------------
//! Token parsing helpers for structured server query detail strings ("key=value key2=value2").
class BGONE_FlightBotHelper
{
	//------------------------------------------------------------------------------------------------
	static string ExtractToken(string text, string key)
	{
		if (text.IsEmpty() || key.IsEmpty())
			return "";

		string prefix = key + "=";
		int idx = text.IndexOf(prefix);
		if (idx < 0)
			return "";

		int start = idx + prefix.Length();
		if (start >= text.Length())
			return "";

		string rest = text.Substring(start, text.Length() - start);
		int spaceIdx = rest.IndexOf(" ");
		if (spaceIdx >= 0)
			return rest.Substring(0, spaceIdx);

		return rest;
	}
}

//------------------------------------------------------------------------------------------------
//! Aims the character toward a fixed relative offset from its initial eye position.
class BGONE_BotBeat_AimRelativeOffset : BGONE_BotBeat
{
	protected static const int STABLE_MS = 250;

	protected vector m_vOffset;
	protected vector m_vTargetPoint;
	protected bool m_bTargetInitialized;
	protected float m_fToleranceDeg;
	protected int m_iOnTargetSinceMs;
	protected float m_fLastYawErr;
	protected float m_fLastPitchErr;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_AimRelativeOffset(string beatName, vector relOffset, float toleranceDeg, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not aim along the clear flight corridor.");
		m_vOffset = relOffset;
		if (toleranceDeg <= 0)
			m_fToleranceDeg = 6.0;
		else
			m_fToleranceDeg = toleranceDeg;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bTargetInitialized = false;
		m_iOnTargetSinceMs = -1;
		m_fLastYawErr = 999.0;
		m_fLastPitchErr = 999.0;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (!ch || !ctrl)
		{
			m_sDetail = "no controlled character";
			return EBGONE_BotBeatState.FAIL;
		}

		if (!m_bTargetInitialized)
		{
			m_vTargetPoint = ch.EyePosition() + m_vOffset;
			m_bTargetInitialized = true;
		}

		vector eye = ch.EyePosition();
		vector toTarget = m_vTargetPoint - eye;
		vector flat = toTarget;
		flat[1] = 0;

		float desiredYaw = flat.ToYaw();
		float bodyYaw = BGONE_BotMath.FacingYaw(ch);
		float yawErr = BGONE_BotMath.AngleDelta(bodyYaw, desiredYaw);
		ctrl.SetHeadingAngle(BGONE_BotMath.AngleDelta(0, bodyYaw + yawErr) * Math.DEG2RAD, true);

		float lookYaw;
		if (BGONE_BotMath.LookYaw(lookYaw))
			m_fLastYawErr = BGONE_BotMath.AngleDelta(lookYaw, desiredYaw);
		else
			m_fLastYawErr = yawErr;

		float desiredPitch = Math.Atan2(toTarget[1], flat.Length()) * Math.RAD2DEG;
		CharacterInputContext inputCtx = ctrl.GetInputContext();
		float curPitch = 0.0;
		if (inputCtx)
			curPitch = inputCtx.GetAimingAngles()[1] * Math.RAD2DEG;

		m_fLastPitchErr = desiredPitch - curPitch;

		BGONE_BotInput.Release("CharacterAimUp");
		BGONE_BotInput.Release("CharacterAimDown");
		if (Math.AbsFloat(m_fLastPitchErr) > m_fToleranceDeg)
		{
			float aimVal = Math.Clamp(m_fLastPitchErr / 30.0, -1.0, 1.0);
			if (Math.AbsFloat(aimVal) < 0.15)
			{
				if (m_fLastPitchErr > 0)
					aimVal = 0.15;
				else
					aimVal = -0.15;
			}

			if (aimVal > 0)
				BGONE_BotInput.Hold("CharacterAimUp", aimVal);
			else
				BGONE_BotInput.Hold("CharacterAimDown", -aimVal);

			CharacterAimingComponent aiming = ctrl.GetAimingComponent();
			if (aiming)
			{
				vector rotDeg = aiming.GetAimingRotation();
				vector wantedRad = Vector(rotDeg[0] * Math.DEG2RAD, desiredPitch * Math.DEG2RAD, 0);
				aiming.SetAimingRotationWanted(wantedRad);
			}
		}

		bool onTarget = (Math.AbsFloat(m_fLastYawErr) <= m_fToleranceDeg)
			&& (Math.AbsFloat(m_fLastPitchErr) <= m_fToleranceDeg);
		if (!onTarget)
		{
			m_iOnTargetSinceMs = -1;
			return EBGONE_BotBeatState.RUNNING;
		}

		if (m_iOnTargetSinceMs < 0)
			m_iOnTargetSinceMs = elapsedMs;

		if (elapsedMs - m_iOnTargetSinceMs < STABLE_MS)
			return EBGONE_BotBeatState.RUNNING;

		m_sDetail = string.Format(
			"aimed along corridor (yawErr=%1 deg, pitchErr=%2 deg, tol=%3 deg)",
			m_fLastYawErr, m_fLastPitchErr, m_fToleranceDeg);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"corridor aim timed out after %1 ms (yawErr=%2 deg, pitchErr=%3 deg)",
			m_iTimeoutMs, m_fLastYawErr, m_fLastPitchErr);
	}
}

//------------------------------------------------------------------------------------------------
//! Synchronizes Client 1 (shooter) and Client 2 (remote observer) via server barrier query so
//! the observer is deployed and watching before the shooter launches.
class BGONE_BotBeat_FlightSyncBarrier : BGONE_BotBeat
{
	protected static const int POLL_MS = 250;

	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_FlightSyncBarrier(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Shooter and remote observer did not synchronize before launch.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
		BGONE_GuidedMissileComponent.ResetLastTrackedTelemetry();
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				m_sDetail = string.Format("both clients synchronized in %1 ms (%2)", elapsedMs, m_sLastDetail);
				return EBGONE_BotBeatState.PASS;
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_FlightBotConst.QUERY_FLIGHT_SYNC_READY);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("sync barrier timed out after %1 ms (last=%2)", m_iTimeoutMs, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Client 1 (shooter): enters ADS, engages PLOS lock briefly to seed launchDir cleanly, fires
//! the guided missile, and waits for server flight telemetry to confirm launch.
class BGONE_BotBeat_FireFlightMissile : BGONE_BotBeat
{
	protected static const int ADS_FALLBACK_MS = 1200;
	protected static const int LOCK_SEED_MS = 150;
	protected static const int SERVER_POLL_MS = 200;

	protected bool m_bLockStarted;
	protected int m_iLockStartMs;
	protected bool m_bTriggerPulled;
	protected bool m_bFireReleased;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastServerDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_FireFlightMissile(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Shooter failed to launch the guided missile.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bLockStarted = false;
		m_iLockStartMs = -1;
		m_bTriggerPulled = false;
		m_bFireReleased = false;
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastServerDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ctrl || !launcherEnt)
		{
			m_sDetail = "missing character controller or launcher fixture";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "launcher missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		bool muzzleEmpty = false;
		BaseWeaponComponent wep = BaseWeaponComponent.Cast(launcherEnt.FindComponent(BaseWeaponComponent));
		if (wep)
		{
			BaseMuzzleComponent muzzle = wep.GetCurrentMuzzle();
			if (muzzle && muzzle.GetAmmoCount() <= 0)
				muzzleEmpty = true;
		}

		if (!muzzleEmpty)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);

			if (!m_bLockStarted && (ctrl.IsWeaponADS() || elapsedMs >= ADS_FALLBACK_MS))
			{
				launcher.SetLockInputState(true);
				m_bLockStarted = true;
				m_iLockStartMs = elapsedMs;
			}

			if (m_bLockStarted && (elapsedMs - m_iLockStartMs >= LOCK_SEED_MS))
			{
				ctrl.SetFireWeaponWanted(true);
				BGONE_BotInput.Hold("CharacterFire", 1.0);
				m_bTriggerPulled = true;
			}
		}
		else if (!m_bFireReleased)
		{
			ctrl.SetFireWeaponWanted(false);
			ctrl.SetWeaponADSInput(false);
			ctrl.SetWeaponADS(false);
			BGONE_BotInput.Release("CharacterFire");
			m_bFireReleased = true;
		}

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastServerDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				ctrl.SetFireWeaponWanted(false);
				ctrl.SetWeaponADSInput(false);
				ctrl.SetWeaponADS(false);
				BGONE_BotInput.Release("CharacterFire");

				string rplStr = BGONE_FlightBotHelper.ExtractToken(m_sLastServerDetail, "rpl");
				run.LogTelemetry(string.Format(
					"role=shooter event=launched rpl=%1 srvFlightTime=%2 srvDist=%3",
					rplStr, srvVal[0], srvVal[1]));
				m_sDetail = string.Format("server confirmed launch in %1 ms (%2)", elapsedMs, m_sLastServerDetail);
				return EBGONE_BotBeatState.PASS;
			}
		}

		if (m_bTriggerPulled && (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS))
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_FlightBotConst.QUERY_FLIGHT_MISSILE_STATE);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (ctrl)
		{
			ctrl.SetFireWeaponWanted(false);
			ctrl.SetWeaponADSInput(false);
			ctrl.SetWeaponADS(false);
		}

		BGONE_BotInput.Release("CharacterFire");
		return string.Format(
			"missile launch timed out after %1 ms (lockStarted=%2 trigger=%3 srv=%4)",
			m_iTimeoutMs, m_bLockStarted, m_bTriggerPulled, m_sLastServerDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Runs on BOTH Client 1 (shooter) and Client 2 (remote observer): asserts server EOnFrame
//! flight progression, ~20 Hz sync throttling, matching replicated missile RplId, proxy
//! dead-reckoning convergence (< 2.0m), and clean single-shot detonation (count == 1).
class BGONE_BotBeat_VerifyProxyFlightAndDetonation : BGONE_BotBeat
{
	protected static const int SERVER_POLL_MS = 250;
	protected static const int CLIENT_CLEANUP_GRACE_MS = 3000;

	protected bool m_bIsShooter;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;

	protected string m_sServerRpl;
	protected float m_fPeakSrvFlightTime;
	protected float m_fPeakSrvDist;
	protected float m_fPeakSrvSpeed;
	protected int m_iLastSrvSends;
	protected int m_iLastSrvDetCount;
	protected int m_iServerDetonatedAtMs;

	protected bool m_bMatchedRpl;
	protected bool m_bObservedConvergedSync;
	protected float m_fBestConvergedErr;
	protected int m_iPeakCliSyncCount;
	protected float m_fLastCliErr;
	protected float m_fPeakCliMaxErr;
	protected int m_iLastCliSnapCount;
	protected string m_sLastServerDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyProxyFlightAndDetonation(string beatName, bool isShooter, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Missile flight replication, proxy dead-reckoning, or single-shot detonation check failed.");
		m_bIsShooter = isShooter;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sServerRpl = "";
		m_fPeakSrvFlightTime = 0.0;
		m_fPeakSrvDist = 0.0;
		m_fPeakSrvSpeed = 0.0;
		m_iLastSrvSends = 0;
		m_iLastSrvDetCount = 0;
		m_iServerDetonatedAtMs = -1;
		m_bMatchedRpl = false;
		m_bObservedConvergedSync = false;
		m_fBestConvergedErr = 999.0;
		m_iPeakCliSyncCount = 0;
		m_fLastCliErr = 999.0;
		m_fPeakCliMaxErr = 0.0;
		m_iLastCliSnapCount = 0;
		m_sLastServerDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	protected string RoleLabel()
	{
		if (m_bIsShooter)
			return "shooter";

		return "observer";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		// Sample local client proxy telemetry every tick.
		RplId cliRpl = BGONE_GuidedMissileComponent.GetLastMissileRplId();
		string cliRplStr = "";
		if (cliRpl.IsValid())
			cliRplStr = cliRpl.AsString();

		int cliSyncCount = BGONE_GuidedMissileComponent.GetLastTrackedSyncReceiveCount();
		float cliLastErr = BGONE_GuidedMissileComponent.GetLastTrackedSyncErrorMeters();
		float cliMaxErr = BGONE_GuidedMissileComponent.GetLastTrackedMaxSyncErrorMeters();
		int cliSnapCount = BGONE_GuidedMissileComponent.GetLastTrackedSnapCorrectionCount();
		int cliDetCount = BGONE_GuidedMissileComponent.GetLastTrackedDetonationCount();
		bool cliDetonated = BGONE_GuidedMissileComponent.IsLastTrackedDetonated();
		BGONE_GuidedMissileComponent liveMissile = BGONE_GuidedMissileComponent.GetLastMissile();

		if (cliDetCount > 1)
		{
			m_sDetail = string.Format("client double detonation detected (role=%1 cliDetCount=%2)", RoleLabel(), cliDetCount);
			return EBGONE_BotBeatState.FAIL;
		}

		if (!m_sServerRpl.IsEmpty() && !cliRplStr.IsEmpty() && cliRplStr == m_sServerRpl)
		{
			m_bMatchedRpl = true;
			if (cliSyncCount > m_iPeakCliSyncCount)
				m_iPeakCliSyncCount = cliSyncCount;

			m_fLastCliErr = cliLastErr;
			if (cliMaxErr > m_fPeakCliMaxErr)
				m_fPeakCliMaxErr = cliMaxErr;

			m_iLastCliSnapCount = cliSnapCount;

			if (cliSyncCount >= BGONE_FlightBotConst.MIN_PROXY_SYNC_COUNT
				&& cliLastErr < BGONE_FlightBotConst.MAX_CONVERGED_SYNC_ERR_M)
			{
				m_bObservedConvergedSync = true;
				if (cliLastErr < m_fBestConvergedErr)
					m_fBestConvergedErr = cliLastErr;
			}
		}

		// Poll server flight & detonation status.
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastServerDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				float srvFlightTime = srvVal[0];
				float srvDist = srvVal[1];
				int srvDetCount = Math.Round(srvVal[2]);

				if (srvDetCount > 1)
				{
					m_sDetail = string.Format("server double detonation detected (srvDetCount=%1)", srvDetCount);
					return EBGONE_BotBeatState.FAIL;
				}

				if (srvFlightTime > m_fPeakSrvFlightTime)
					m_fPeakSrvFlightTime = srvFlightTime;

				if (srvDist > m_fPeakSrvDist)
					m_fPeakSrvDist = srvDist;

				m_iLastSrvDetCount = srvDetCount;

				string rplTok = BGONE_FlightBotHelper.ExtractToken(m_sLastServerDetail, "rpl");
				if (!rplTok.IsEmpty())
					m_sServerRpl = rplTok;

				float srvSpeed = BGONE_FlightBotHelper.ExtractToken(m_sLastServerDetail, "speed").ToFloat();
				if (srvSpeed > m_fPeakSrvSpeed)
					m_fPeakSrvSpeed = srvSpeed;

				int srvSends = BGONE_FlightBotHelper.ExtractToken(m_sLastServerDetail, "sends").ToInt();
				if (srvSends > m_iLastSrvSends)
					m_iLastSrvSends = srvSends;

				if (srvDetCount == 1 && m_iServerDetonatedAtMs < 0)
					m_iServerDetonatedAtMs = elapsedMs;
			}
		}

		if (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_FlightBotConst.QUERY_FLIGHT_MISSILE_STATE);
		}

		// Wait until server confirms single-shot detonation.
		if (m_iLastSrvDetCount < 1)
			return EBGONE_BotBeatState.RUNNING;

		// Allow client up to CLIENT_CLEANUP_GRACE_MS to receive RpcDo_Explode or remove entity cleanly.
		bool clientClean = cliDetonated || (liveMissile == null);
		if (!clientClean && (elapsedMs - m_iServerDetonatedAtMs < CLIENT_CLEANUP_GRACE_MS))
			return EBGONE_BotBeatState.RUNNING;

		// Validate all assertions once detonation has completed.
		if (m_fPeakSrvFlightTime <= BGONE_FlightBotConst.MIN_FLIGHT_TIME_SEC)
		{
			m_sDetail = string.Format(
				"server flightTime too short (%1 s <= %2 s)",
				m_fPeakSrvFlightTime, BGONE_FlightBotConst.MIN_FLIGHT_TIME_SEC);
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_fPeakSrvDist <= BGONE_FlightBotConst.MIN_TRAVEL_DIST_M)
		{
			m_sDetail = string.Format(
				"server travel distance too short (%1 m <= %2 m)",
				m_fPeakSrvDist, BGONE_FlightBotConst.MIN_TRAVEL_DIST_M);
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_fPeakSrvSpeed < BGONE_FlightBotConst.MIN_PEAK_SPEED_MS
			|| m_fPeakSrvSpeed > BGONE_FlightBotConst.MAX_PEAK_SPEED_MS)
		{
			m_sDetail = string.Format(
				"server peak speed %1 m/s outside expected [%2, %3] envelope",
				m_fPeakSrvSpeed, BGONE_FlightBotConst.MIN_PEAK_SPEED_MS, BGONE_FlightBotConst.MAX_PEAK_SPEED_MS);
			return EBGONE_BotBeatState.FAIL;
		}

		float sendRateHz = 0.0;
		if (m_fPeakSrvFlightTime > 0.1)
			sendRateHz = m_iLastSrvSends / m_fPeakSrvFlightTime;

		if (m_iLastSrvSends < BGONE_FlightBotConst.MIN_PROXY_SYNC_COUNT
			|| sendRateHz > BGONE_FlightBotConst.MAX_SYNC_SEND_RATE_HZ)
		{
			m_sDetail = string.Format(
				"server sync throttling out of bounds (sends=%1 rate=%2 Hz max=%3 Hz)",
				m_iLastSrvSends, sendRateHz, BGONE_FlightBotConst.MAX_SYNC_SEND_RATE_HZ);
			return EBGONE_BotBeatState.FAIL;
		}

		if (!m_bMatchedRpl)
		{
			m_sDetail = string.Format(
				"client never observed server missile RplId (role=%1 srvRpl=%2 cliRpl=%3)",
				RoleLabel(), m_sServerRpl, cliRplStr);
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_iPeakCliSyncCount < BGONE_FlightBotConst.MIN_PROXY_SYNC_COUNT)
		{
			m_sDetail = string.Format(
				"client received too few proxy syncs (role=%1 syncCount=%2 < %3)",
				RoleLabel(), m_iPeakCliSyncCount, BGONE_FlightBotConst.MIN_PROXY_SYNC_COUNT);
			return EBGONE_BotBeatState.FAIL;
		}

		if (!m_bObservedConvergedSync)
		{
			m_sDetail = string.Format(
				"proxy dead-reckoning never converged below %1 m (role=%2 lastErr=%3 maxErr=%4 snaps=%5 ticks=%6)",
				BGONE_FlightBotConst.MAX_CONVERGED_SYNC_ERR_M, RoleLabel(),
				m_fLastCliErr, m_fPeakCliMaxErr, m_iLastCliSnapCount,
				BGONE_GuidedMissileComponent.GetLastTrackedProxyFrameTicks());
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_iLastCliSnapCount > BGONE_FlightBotConst.MAX_MIDFLIGHT_SNAPS)
		{
			m_sDetail = string.Format(
				"proxy required too many snap teleports (role=%1 snaps=%2 max=%3 syncs=%4 maxErr=%5 ticks=%6)",
				RoleLabel(), m_iLastCliSnapCount, BGONE_FlightBotConst.MAX_MIDFLIGHT_SNAPS,
				m_iPeakCliSyncCount, m_fPeakCliMaxErr,
				BGONE_GuidedMissileComponent.GetLastTrackedProxyFrameTicks());
			return EBGONE_BotBeatState.FAIL;
		}

		if (!clientClean)
		{
			m_sDetail = string.Format(
				"client missile neither detonated nor cleaned up after server detonation (role=%1 cliDet=%2 live=%3)",
				RoleLabel(), cliDetonated, liveMissile != null);
			return EBGONE_BotBeatState.FAIL;
		}

		int cliDetFlag = 0;
		if (cliDetonated)
			cliDetFlag = 1;

		run.LogTelemetry(string.Format(
			"role=%1 rpl=%2 flightTime=%3 distance=%4 speed=%5 srvSends=%6 srvRateHz=%7",
			RoleLabel(), m_sServerRpl, m_fPeakSrvFlightTime, m_fPeakSrvDist,
			m_fPeakSrvSpeed, m_iLastSrvSends, sendRateHz));
		run.LogTelemetry(string.Format(
			"role=%1 syncCount=%2 bestSyncErr=%3 lastSyncErr=%4 maxSyncErr=%5 snapCount=%6 proxyTicks=%7 srvDet=%8 cliDet=%9",
			RoleLabel(), m_iPeakCliSyncCount, m_fBestConvergedErr, m_fLastCliErr,
			m_fPeakCliMaxErr, m_iLastCliSnapCount, BGONE_GuidedMissileComponent.GetLastTrackedProxyFrameTicks(),
			m_iLastSrvDetCount, cliDetFlag));

		m_sDetail = string.Format(
			"role=%1 rpl=%2 flight=%3s dist=%4m speed=%5m/s syncs=%6 err=%7m det=%8",
			RoleLabel(), m_sServerRpl, m_fPeakSrvFlightTime, m_fPeakSrvDist,
			m_fPeakSrvSpeed, m_iPeakCliSyncCount, m_fBestConvergedErr, m_iLastSrvDetCount);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"flight/sync verification timed out after %1 ms (role=%2 srvRpl=%3 flight=%4 dist=%5 srvDet=%6 cliSyncs=%7 err=%8)",
			m_iTimeoutMs, RoleLabel(), m_sServerRpl, m_fPeakSrvFlightTime,
			m_fPeakSrvDist, m_iLastSrvDetCount, m_iPeakCliSyncCount, m_fLastCliErr);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	//------------------------------------------------------------------------------------------------
	override protected BGONE_BotRun BGONE_BotBuildScenario(string scenarioName)
	{
		if (scenarioName != BGONE_FlightBotConst.SCENARIO_PROXY_SYNC)
			return super.BGONE_BotBuildScenario(scenarioName);

		int clientIdx = BGONE_BotClientIndex();
		BGONE_BotRun run = new BGONE_BotRun(scenarioName);

		if (clientIdx == 1)
		{
			run.m_sTitle = "BGONE Missile Flight, 20 Hz Proxy Sync & Single-Shot Detonation (Shooter)";
			run.m_sSummary = "Verified server EOnFrame missile flight, ~20 Hz throttled replication, shooter proxy dead-reckoning convergence, and single-shot detonation.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_plos", 20000));
			run.Add(new BGONE_BotBeat_FlightSyncBarrier("wait_observer_ready", 90000));
			run.Add(new BGONE_BotBeat_AimRelativeOffset("aim_clear_corridor", Vector(0, 40, 100), 6.0, 12000));
			run.Add(new BGONE_BotBeat_FireFlightMissile("fire_plos_missile", 15000));
			run.Add(new BGONE_BotBeat_VerifyProxyFlightAndDetonation("verify_shooter_flight_sync", true, 30000));
			return run;
		}

		run.m_sTitle = "BGONE Missile Flight, 20 Hz Proxy Sync & Single-Shot Detonation (Observer)";
		run.m_sSummary = "Verified remote observer received throttled proxy syncs for matching RplId, converged dead-reckoning (< 2.0m), and observed clean detonation.";
		run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
		run.Add(new BGONE_BotBeat_AimRelativeOffset("observer_aim_corridor", Vector(0, 25, 100), 8.0, 12000));
		run.Add(new BGONE_BotBeat_FlightSyncBarrier("signal_observer_ready", 90000));
		run.Add(new BGONE_BotBeat_VerifyProxyFlightAndDetonation("verify_observer_flight_sync", false, 45000));
		return run;
	}

	//------------------------------------------------------------------------------------------------
	override protected int BGONE_BotStageScenario(string scenarioName, IEntity character, out RplId fixtureId, out vector fixturePos, out string detail)
	{
		if (scenarioName != BGONE_FlightBotConst.SCENARIO_PROXY_SYNC)
			return super.BGONE_BotStageScenario(scenarioName, character, fixtureId, fixturePos, detail);

		int clientIdx = BGONE_BotClientIndex();
		if (clientIdx == 1)
		{
			BGONE_FlightBotCoordinator.OnClient1Staged();
			return BGONE_StageFlightLauncher(
				scenarioName,
				BGONE_FlightBotConst.PREFAB_LAUNCHER_PLOS,
				character,
				0.9,
				0.35,
				fixtureId,
				fixturePos,
				detail);
		}

		fixtureId = RplId.Invalid();
		fixturePos = character.GetOrigin();
		detail = string.Format("observer client %1 ready at %2", clientIdx, fixturePos);
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected int BGONE_StageFlightLauncher(
		string scenarioName,
		ResourceName prefab,
		IEntity character,
		float forwardDist,
		float heightOffset,
		out RplId fixtureId,
		out vector fixturePos,
		out string detail)
	{
		fixtureId = RplId.Invalid();
		fixturePos = vector.Zero;

		if (!character)
		{
			detail = "no character to stage near";
			return 10;
		}

		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
		{
			detail = string.Format("failed to load prefab %1", prefab);
			return 11;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			detail = "no active world";
			return 12;
		}

		vector fwd = character.GetWorldTransformAxis(2);
		fwd[1] = 0;
		if (fwd.LengthSq() < 0.001)
			fwd = Vector(0, 0, 1);
		else
			fwd.Normalize();

		vector origin = character.GetOrigin() + fwd * forwardDist;
		origin[1] = world.GetSurfaceY(origin[0], origin[2]) + heightOffset;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = origin;

		IEntity spawned = GetGame().SpawnEntityPrefab(res, world, params);
		if (!spawned)
		{
			detail = string.Format("failed to spawn %1 at %2", prefab, origin);
			return 13;
		}

		BGONE_BotSpawnLedger.Add(spawned, scenarioName);
		m_BGONE_ServerStagedEntityId = spawned.GetID();
		m_vBGONE_ServerStartPos = character.GetOrigin();
		fixturePos = spawned.GetOrigin();

		RplComponent rpl = RplComponent.Cast(spawned.FindComponent(RplComponent));
		if (rpl && rpl.Id().IsValid())
			fixtureId = rpl.Id();

		detail = string.Format("staged %1 at %2", prefab, fixturePos);
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	override protected bool BGONE_BotAnswerQuery(int queryId, out int ok, out vector val, out string detail)
	{
		if (queryId == BGONE_FlightBotConst.QUERY_FLIGHT_SYNC_READY)
		{
			int clientIdx = BGONE_BotClientIndex();
			BGONE_FlightBotCoordinator.MarkClientReady(clientIdx);

			float c1 = 0.0;
			if (BGONE_FlightBotCoordinator.IsClient1Ready())
				c1 = 1.0;

			float c2 = 0.0;
			if (BGONE_FlightBotCoordinator.IsClient2Ready())
				c2 = 1.0;

			if (BGONE_FlightBotCoordinator.AreBothReady())
				ok = 1;
			else
				ok = 0;

			val = Vector(c1, c2, BGONE_FlightBotCoordinator.GetRunStartEpoch());
			detail = string.Format("c1=%1 c2=%2 startEpoch=%3", c1, c2, BGONE_FlightBotCoordinator.GetRunStartEpoch());
			return true;
		}

		if (queryId == BGONE_FlightBotConst.QUERY_FLIGHT_MISSILE_STATE)
		{
			int curEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
			int startEpoch = BGONE_FlightBotCoordinator.GetRunStartEpoch();
			if (curEpoch <= startEpoch)
			{
				ok = 0;
				val = vector.Zero;
				detail = string.Format("waiting for launch (epoch=%1 start=%2)", curEpoch, startEpoch);
				return true;
			}

			RplId rplId = BGONE_GuidedMissileComponent.GetLastMissileRplId();
			string rplStr = "invalid";
			if (rplId.IsValid())
				rplStr = rplId.AsString();

			float flightTime = BGONE_GuidedMissileComponent.GetLastTrackedFlightTime();
			float travelDist = BGONE_GuidedMissileComponent.GetLastTrackedTravelDistance();
			float speed = BGONE_GuidedMissileComponent.GetLastTrackedSpeed();
			int sends = BGONE_GuidedMissileComponent.GetLastTrackedSyncSendCount();
			int detCount = BGONE_GuidedMissileComponent.GetLastTrackedDetonationCount();
			int detFlag = 0;
			if (BGONE_GuidedMissileComponent.IsLastTrackedDetonated())
				detFlag = 1;

			ok = 1;
			val = Vector(flightTime, travelDist, detCount);
			detail = string.Format(
				"rpl=%1 speed=%2 sends=%3 det=%4",
				rplStr, speed, sends, detFlag);
			return true;
		}

		return super.BGONE_BotAnswerQuery(queryId, ok, val, detail);
	}

	//------------------------------------------------------------------------------------------------
	override protected void BGONE_BotOnEnd(int passed, string detail)
	{
		if (BGONE_BotScenario() == BGONE_FlightBotConst.SCENARIO_PROXY_SYNC)
			BGONE_FlightBotCoordinator.MarkClientEnded(BGONE_BotClientIndex());

		super.BGONE_BotOnEnd(passed, detail);
	}
}
