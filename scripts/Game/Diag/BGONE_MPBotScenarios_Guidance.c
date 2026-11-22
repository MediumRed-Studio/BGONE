/*!
	\brief Headless MP QA bot scenarios for VIS, PLOS, and SACLOS missile guidance math,
	target switching, seeker self-destruct on lost track, and beam-riding steering.

	Scenarios (single-client + dedicated server):
	  - vis_top_static    : VIS launcher in Top Attack (profile 1) locking static BTR70, climbing
	                        past +12m loft altitude into terminal dive without FOV self-destruct.
	  - vis_target_switch : VIS launcher locking Target A then shifting aim to Target B (~14 deg
	                        apart, both within 0.3 rad) while holding lock; verifies clean target
	                        switch and server missile receiving Target B's RplId.
	  - vis_occlusion_sd  : VIS launcher locking & firing at BTR70; server teleports target behind
	                        shooter mid-flight (>120 deg off seeker axis) and verifies seeker
	                        m_fNoTargetDestructTime self-destruct well before TTL.
	  - plos_moving       : PLOS launcher tracking steady lateral (+8 deg/s) and vertical (+2 deg/s)
	                        pan; verifies client+server yawChange/pitchChange in degrees/sec and
	                        predicted lateral curve > 10m with clean detonation.
	  - saclos_steer      : SACLOS launcher firing down corridor then steering +15 deg right and
	                        diving to terrain; verifies rightward deflection > 15m (no X inversion,
	                        ownerAimDir retained across frames) and clean detonation.

	Uses existing BGONE_BotRun.Query(int queryId) plumbing (zero new SCR_PlayerController RPCs).
	Compile order note: Guidance.c compiles after Flight.c and before Launcher.c, so all
	SCR_PlayerController staging helpers here are self-contained.
*/
class BGONE_GuidanceBotConst
{
	static const string SCENARIO_VIS_TOP_STATIC = "vis_top_static";
	static const string SCENARIO_VIS_TARGET_SWITCH = "vis_target_switch";
	static const string SCENARIO_VIS_OCCLUSION_SD = "vis_occlusion_sd";
	static const string SCENARIO_PLOS_MOVING = "plos_moving";
	static const string SCENARIO_SACLOS_STEER = "saclos_steer";

	static const ResourceName PREFAB_LAUNCHER_VIS = "{82C0AFB409A3AE0B}Prefabs/Weapons/Launchers/BGONE_Launcher_VIS.et";
	static const ResourceName PREFAB_LAUNCHER_PLOS = "{E6E70B5FFAC5FEA4}Prefabs/Weapons/Launchers/BGONE_Launcher_PLOS.et";
	static const ResourceName PREFAB_LAUNCHER_SACLOS = "{6DEF77B8CDF7528D}Prefabs/Weapons/Launchers/BGONE_Launcher_SACLOS.et";
	static const ResourceName PREFAB_TARGET_BTR70 = "{C012BB3488BEA0C2}Prefabs/Vehicles/Wheeled/BTR70/BTR70.et";

	static const int QUERY_GUIDANCE_TARGET_INFO = 7301;
	static const int QUERY_GUIDANCE_MISSILE_STATE = 7302;
	static const int QUERY_GUIDANCE_DISRUPT_TARGET = 7303;
}

//------------------------------------------------------------------------------------------------
//! Client-side shared scenario context populated by QUERY_GUIDANCE_TARGET_INFO.
class BGONE_GuidanceBotContext
{
	static vector s_vTargetAPos;
	static vector s_vTargetBPos;
	static string s_sTargetARpl;
	static string s_sTargetBRpl;
	static int s_iStartEpoch;
	static vector s_vCorridorDir;
	static float s_fClientPlosYawChange;
	static float s_fClientPlosPitchChange;

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_vTargetAPos = vector.Zero;
		s_vTargetBPos = vector.Zero;
		s_sTargetARpl = "";
		s_sTargetBRpl = "";
		s_iStartEpoch = 0;
		s_vCorridorDir = Vector(-1, 0, 0);
		s_fClientPlosYawChange = 0.0;
		s_fClientPlosPitchChange = 0.0;
	}
}

//------------------------------------------------------------------------------------------------
//! Self-contained math & character aiming helpers for guidance QA beats.
class BGONE_GuidanceBotHelper
{
	//------------------------------------------------------------------------------------------------
	static bool IsGuidanceScenario(string scenarioName)
	{
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TOP_STATIC)
			return true;
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TARGET_SWITCH)
			return true;
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_OCCLUSION_SD)
			return true;
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_PLOS_MOVING)
			return true;
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_SACLOS_STEER)
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	static vector RightVectorXZ(vector fwd)
	{
		vector flat = Vector(fwd[0], 0.0, fwd[2]);
		if (flat.LengthSq() < 0.0001)
			return Vector(1, 0, 0);

		flat.Normalize();
		return Vector(flat[2], 0.0, -flat[0]);
	}

	//------------------------------------------------------------------------------------------------
	static void DriveCharacterAimAngles(
		ChimeraCharacter ch,
		SCR_CharacterControllerComponent ctrl,
		float desiredYawDeg,
		float desiredPitchDeg,
		float pitchDeadzoneDeg,
		out float outYawErr,
		out float outPitchErr)
	{
		outYawErr = 999.0;
		outPitchErr = 999.0;
		if (!ch || !ctrl)
			return;

		float bodyYaw = BGONE_BotMath.FacingYaw(ch);
		float yawErr = BGONE_BotMath.AngleDelta(bodyYaw, desiredYawDeg);
		ctrl.SetHeadingAngle(BGONE_BotMath.AngleDelta(0.0, bodyYaw + yawErr) * Math.DEG2RAD, true);

		float lookYaw;
		if (BGONE_BotMath.LookYaw(lookYaw))
			outYawErr = BGONE_BotMath.AngleDelta(lookYaw, desiredYawDeg);
		else
			outYawErr = yawErr;

		float curPitch = 0.0;
		bool gotCamPitch = false;
		CameraManager camMgr = GetGame().GetCameraManager();
		if (camMgr)
		{
			CameraBase cam = camMgr.CurrentCamera();
			if (cam)
			{
				vector camMat[4];
				cam.GetWorldTransform(camMat);
				if (camMat[2].LengthSq() > 0.0001)
				{
					curPitch = camMat[2].VectorToAngles()[1];
					gotCamPitch = true;
				}
			}
		}

		if (!gotCamPitch)
		{
			CharacterInputContext inputCtx = ctrl.GetInputContext();
			if (inputCtx)
				curPitch = inputCtx.GetAimingAngles()[1] * Math.RAD2DEG;
		}

		outPitchErr = desiredPitchDeg - curPitch;

		BGONE_BotInput.Release("CharacterAimUp");
		BGONE_BotInput.Release("CharacterAimDown");
		if (Math.AbsFloat(outPitchErr) > pitchDeadzoneDeg)
		{
			float aimVal = Math.Clamp(outPitchErr / 30.0, -1.0, 1.0);
			if (Math.AbsFloat(aimVal) < 0.15)
			{
				if (outPitchErr > 0.0)
					aimVal = 0.15;
				else
					aimVal = -0.15;
			}

			if (aimVal > 0.0)
				BGONE_BotInput.Hold("CharacterAimUp", aimVal);
			else
				BGONE_BotInput.Hold("CharacterAimDown", -aimVal);

			CharacterAimingComponent aiming = ctrl.GetAimingComponent();
			if (aiming)
			{
				vector rotDeg = aiming.GetAimingRotation();
				vector wantedRad = Vector(rotDeg[0] * Math.DEG2RAD, desiredPitchDeg * Math.DEG2RAD, 0.0);
				aiming.SetAimingRotationWanted(wantedRad);
			}

			CharacterHeadAimingComponent headAim = ctrl.GetHeadAimingComponent();
			if (headAim)
			{
				vector headRotDeg = headAim.GetAimingRotation();
				vector headWantedRad = Vector(headRotDeg[0] * Math.DEG2RAD, desiredPitchDeg * Math.DEG2RAD, 0.0);
				headAim.SetAimingRotationWanted(headWantedRad);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	static void DriveCharacterAimAtPoint(
		ChimeraCharacter ch,
		SCR_CharacterControllerComponent ctrl,
		vector worldTarget,
		float pitchDeadzoneDeg,
		out float outYawErr,
		out float outPitchErr)
	{
		outYawErr = 999.0;
		outPitchErr = 999.0;
		if (!ch || !ctrl)
			return;

		vector eye = ch.EyePosition();
		vector toTarget = worldTarget - eye;
		vector flat = Vector(toTarget[0], 0.0, toTarget[2]);
		float flatLen = flat.Length();
		if (flatLen < 0.01)
			return;

		float desiredYaw = flat.ToYaw();
		float desiredPitch = Math.Atan2(toTarget[1], flatLen) * Math.RAD2DEG;
		DriveCharacterAimAngles(ch, ctrl, desiredYaw, desiredPitch, pitchDeadzoneDeg, outYawErr, outPitchErr);
	}
}

//------------------------------------------------------------------------------------------------
//! Queries server for staged target positions, RplIds, corridor direction, and initial launch epoch.
class BGONE_BotBeat_FetchGuidanceTargets : BGONE_BotBeat
{
	protected int m_iSeq;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_FetchGuidanceTargets(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Failed to fetch staged guidance target metadata from server.");
		MarkSetup();
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		BGONE_GuidanceBotContext.Reset();
		m_iSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_TARGET_INFO);
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iSeq <= 0 || !run.HasAnswer(m_iSeq))
			return EBGONE_BotBeatState.RUNNING;

		bool ok = run.AnswerOk(m_iSeq);
		vector targetAPos = run.AnswerValue(m_iSeq);
		string detail = run.AnswerDetail(m_iSeq);
		if (!ok)
		{
			m_sDetail = string.Format("target info query failed: %1", detail);
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidanceBotContext.s_vTargetAPos = targetAPos;
		BGONE_GuidanceBotContext.s_sTargetARpl = BGONE_FlightBotHelper.ExtractToken(detail, "rplA");
		BGONE_GuidanceBotContext.s_sTargetBRpl = BGONE_FlightBotHelper.ExtractToken(detail, "rplB");
		BGONE_GuidanceBotContext.s_iStartEpoch = BGONE_FlightBotHelper.ExtractToken(detail, "epoch").ToInt();

		float bx = BGONE_FlightBotHelper.ExtractToken(detail, "bx").ToFloat();
		float by = BGONE_FlightBotHelper.ExtractToken(detail, "by").ToFloat();
		float bz = BGONE_FlightBotHelper.ExtractToken(detail, "bz").ToFloat();
		BGONE_GuidanceBotContext.s_vTargetBPos = Vector(bx, by, bz);

		float cx = BGONE_FlightBotHelper.ExtractToken(detail, "cx").ToFloat();
		float cz = BGONE_FlightBotHelper.ExtractToken(detail, "cz").ToFloat();
		vector cDir = Vector(cx, 0.0, cz);
		if (cDir.LengthSq() > 0.01)
			BGONE_GuidanceBotContext.s_vCorridorDir = cDir.Normalized();

		m_sDetail = string.Format(
			"fetched targets (rplA=%1 rplB=%2 epoch=%3 dir=%4)",
			BGONE_GuidanceBotContext.s_sTargetARpl, BGONE_GuidanceBotContext.s_sTargetBRpl,
			BGONE_GuidanceBotContext.s_iStartEpoch, BGONE_GuidanceBotContext.s_vCorridorDir);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Cycles the equipped launcher's attack profile to a requested index (e.g. 1 = Top Attack).
class BGONE_BotBeat_SelectAttackProfile : BGONE_BotBeat
{
	protected int m_iDesiredIdx;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_SelectAttackProfile(string beatName, int desiredIdx, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not select the requested missile attack profile.");
		m_iDesiredIdx = desiredIdx;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		IEntity launcherEnt = run.FixtureEntity();
		if (!launcherEnt)
			return EBGONE_BotBeatState.RUNNING;

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "launcher missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		int count = launcher.GetAvailableAttackProfileCount();
		if (count <= m_iDesiredIdx)
		{
			m_sDetail = string.Format("launcher only has %1 profile(s), need index %2", count, m_iDesiredIdx);
			return EBGONE_BotBeatState.FAIL;
		}

		int safety = 0;
		while (launcher.GetSelectedAttackProfileIndex() != m_iDesiredIdx && safety <= count)
		{
			launcher.CycleAttackMode();
			safety = safety + 1;
		}

		int curIdx = launcher.GetSelectedAttackProfileIndex();
		if (curIdx != m_iDesiredIdx)
		{
			m_sDetail = string.Format("failed to cycle to profile %1 (stuck at %2)", m_iDesiredIdx, curIdx);
			return EBGONE_BotBeatState.FAIL;
		}

		string profName = "";
		BGONE_AttackProfile_Base prof = launcher.GetCurrentAttackMode();
		if (prof)
			profName = prof.GetProfileName();

		m_sDetail = string.Format("selected attack profile %1 ('%2')", curIdx, profName);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Aims at staged Target A (or B), enters ADS, holds VIS lock until lockingProgress == 100
//! and locked target RplId matches the staged vehicle.
class BGONE_BotBeat_AimAndLockVisTarget : BGONE_BotBeat
{
	protected bool m_bUseTargetB;
	protected bool m_bLockStarted;
	protected float m_fLastProgress;
	protected string m_sLastLockedRpl;
	protected float m_fLastYawErr;
	protected float m_fLastPitchErr;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_AimAndLockVisTarget(string beatName, bool useTargetB, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("VIS launcher failed to acquire 100% lock on staged target vehicle.");
		m_bUseTargetB = useTargetB;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bLockStarted = false;
		m_fLastProgress = 0.0;
		m_sLastLockedRpl = "";
		m_fLastYawErr = 999.0;
		m_fLastPitchErr = 999.0;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ch || !ctrl || !launcherEnt)
		{
			m_sDetail = "missing character, controller, or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_LockType_VIS visLock = BGONE_LockType_VIS.Cast(launcher.GetLockTypeComponent());
		if (!visLock)
		{
			m_sDetail = "launcher lock component is not BGONE_LockType_VIS";
			return EBGONE_BotBeatState.FAIL;
		}

		vector targetPos = BGONE_GuidanceBotContext.s_vTargetAPos;
		string expectedRpl = BGONE_GuidanceBotContext.s_sTargetARpl;
		if (m_bUseTargetB)
		{
			targetPos = BGONE_GuidanceBotContext.s_vTargetBPos;
			expectedRpl = BGONE_GuidanceBotContext.s_sTargetBRpl;
		}

		vector aimPoint = targetPos + Vector(0.0, 1.35, 0.0);
		BGONE_GuidanceBotHelper.DriveCharacterAimAtPoint(ch, ctrl, aimPoint, 0.2, m_fLastYawErr, m_fLastPitchErr);

		ctrl.SetWeaponRaised(true);
		ctrl.SetWeaponADS(true);
		ctrl.SetWeaponADSInput(true);

		bool aimed = (Math.AbsFloat(m_fLastYawErr) <= 1.5 && Math.AbsFloat(m_fLastPitchErr) <= 1.5);
		if (!m_bLockStarted && ctrl.IsWeaponADS() && aimed)
		{
			launcher.SetLockInputState(true);
			m_bLockStarted = true;
		}

		m_fLastProgress = visLock.GetLockingProgress();
		RplId lockedRpl = visLock.GetLockedTargetRplId();
		if (lockedRpl.IsValid())
			m_sLastLockedRpl = lockedRpl.AsString();
		else
			m_sLastLockedRpl = "";

		if (m_bLockStarted && m_fLastProgress >= 100.0 && !m_sLastLockedRpl.IsEmpty() && m_sLastLockedRpl == expectedRpl)
		{
			run.LogTelemetry(string.Format("vis_lock_acquired=1 targetRpl=%1 progress=%2", m_sLastLockedRpl, m_fLastProgress));
			m_sDetail = string.Format("VIS locked target %1 in %2 ms (progress=%3)", m_sLastLockedRpl, elapsedMs, m_fLastProgress);
			return EBGONE_BotBeatState.PASS;
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"VIS lock timed out after %1 ms (started=%2 prog=%3 lockedRpl=%4 yawErr=%5 pitchErr=%6)",
			m_iTimeoutMs, m_bLockStarted, m_fLastProgress, m_sLastLockedRpl, m_fLastYawErr, m_fLastPitchErr);
	}
}

//------------------------------------------------------------------------------------------------
//! Acquires 100% VIS lock on Target A, then smoothly pans directly onto Target B while keeping
//! ADS and lock continuously held, verifying clean switch to Target B (progress reset + 100% on B).
class BGONE_BotBeat_VisTargetSwitchLock : BGONE_BotBeat
{
	protected int m_iPhase;
	protected bool m_bLockStarted;
	protected bool m_bObservedSwitchReset;
	protected float m_fLastProgress;
	protected string m_sLastLockedRpl;
	protected float m_fLastYawErr;
	protected float m_fLastPitchErr;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VisTargetSwitchLock(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("VIS launcher failed to cleanly switch lock from Target A to Target B.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPhase = 0;
		m_bLockStarted = false;
		m_bObservedSwitchReset = false;
		m_fLastProgress = 0.0;
		m_sLastLockedRpl = "";
		m_fLastYawErr = 999.0;
		m_fLastPitchErr = 999.0;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ch || !ctrl || !launcherEnt)
		{
			m_sDetail = "missing character, controller, or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_LockType_VIS visLock = BGONE_LockType_VIS.Cast(launcher.GetLockTypeComponent());
		if (!visLock)
		{
			m_sDetail = "launcher lock component is not BGONE_LockType_VIS";
			return EBGONE_BotBeatState.FAIL;
		}

		ctrl.SetWeaponRaised(true);
		ctrl.SetWeaponADS(true);
		ctrl.SetWeaponADSInput(true);

		m_fLastProgress = visLock.GetLockingProgress();
		RplId lockedRpl = visLock.GetLockedTargetRplId();
		if (lockedRpl.IsValid())
			m_sLastLockedRpl = lockedRpl.AsString();
		else
			m_sLastLockedRpl = "";

		if (m_iPhase == 0)
		{
			vector aimA = BGONE_GuidanceBotContext.s_vTargetAPos + Vector(0.0, 1.35, 0.0);
			BGONE_GuidanceBotHelper.DriveCharacterAimAtPoint(ch, ctrl, aimA, 0.2, m_fLastYawErr, m_fLastPitchErr);

			bool aimedA = (Math.AbsFloat(m_fLastYawErr) <= 1.5 && Math.AbsFloat(m_fLastPitchErr) <= 1.5);
			if (!m_bLockStarted && ctrl.IsWeaponADS() && aimedA)
			{
				launcher.SetLockInputState(true);
				m_bLockStarted = true;
			}

			if (m_bLockStarted && m_fLastProgress >= 100.0 && m_sLastLockedRpl == BGONE_GuidanceBotContext.s_sTargetARpl)
			{
				run.LogTelemetry(string.Format("vis_switch_phase=locked_a rplA=%1", m_sLastLockedRpl));
				m_iPhase = 1;
			}

			return EBGONE_BotBeatState.RUNNING;
		}

		// Phase 1: keep lock + ADS held and pan directly onto Target B.
		vector aimB = BGONE_GuidanceBotContext.s_vTargetBPos + Vector(0.0, 1.35, 0.0);
		BGONE_GuidanceBotHelper.DriveCharacterAimAtPoint(ch, ctrl, aimB, 0.2, m_fLastYawErr, m_fLastPitchErr);

		if (m_fLastProgress < 99.0 || m_sLastLockedRpl != BGONE_GuidanceBotContext.s_sTargetARpl)
			m_bObservedSwitchReset = true;

		if (m_bObservedSwitchReset
			&& m_fLastProgress >= 100.0
			&& !m_sLastLockedRpl.IsEmpty()
			&& m_sLastLockedRpl == BGONE_GuidanceBotContext.s_sTargetBRpl
			&& m_sLastLockedRpl != BGONE_GuidanceBotContext.s_sTargetARpl)
		{
			run.LogTelemetry(string.Format(
				"vis_switch_phase=locked_b rplA=%1 rplB=%2 resetObserved=1",
				BGONE_GuidanceBotContext.s_sTargetARpl, m_sLastLockedRpl));
			m_sDetail = string.Format(
				"switched VIS lock cleanly from %1 to %2 in %3 ms",
				BGONE_GuidanceBotContext.s_sTargetARpl, m_sLastLockedRpl, elapsedMs);
			return EBGONE_BotBeatState.PASS;
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"VIS target switch timed out (phase=%1 prog=%2 lockedRpl=%3 reset=%4 yawErr=%5 pitchErr=%6)",
			m_iPhase, m_fLastProgress, m_sLastLockedRpl, m_bObservedSwitchReset, m_fLastYawErr, m_fLastPitchErr);
	}
}

//------------------------------------------------------------------------------------------------
//! Fires the equipped guided launcher while maintaining ADS, then waits for server launch epoch.
class BGONE_BotBeat_FireGuidedLauncher : BGONE_BotBeat
{
	protected static const int SERVER_POLL_MS = 200;

	protected bool m_bKeepAds;
	protected bool m_bTriggerPulled;
	protected bool m_bFireReleased;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastServerDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_FireGuidedLauncher(string beatName, bool keepAdsAfterFire, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Failed to fire guided launcher and confirm server launch.");
		m_bKeepAds = keepAdsAfterFire;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
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
			m_sDetail = "missing character controller or launcher";
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
			ctrl.SetFireWeaponWanted(true);
			BGONE_BotInput.Hold("CharacterFire", 1.0);
			m_bTriggerPulled = true;
		}
		else if (!m_bFireReleased)
		{
			ctrl.SetFireWeaponWanted(false);
			BGONE_BotInput.Release("CharacterFire");
			if (!m_bKeepAds)
			{
				ctrl.SetWeaponADSInput(false);
				ctrl.SetWeaponADS(false);
			}
			m_bFireReleased = true;
		}

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			m_sLastServerDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				ctrl.SetFireWeaponWanted(false);
				BGONE_BotInput.Release("CharacterFire");
				if (!m_bKeepAds)
				{
					ctrl.SetWeaponADSInput(false);
					ctrl.SetWeaponADS(false);
				}

				m_sDetail = string.Format("server confirmed missile launch in %1 ms (%2)", elapsedMs, m_sLastServerDetail);
				return EBGONE_BotBeatState.PASS;
			}
		}

		if (m_bTriggerPulled && (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS))
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
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
			if (!m_bKeepAds)
			{
				ctrl.SetWeaponADSInput(false);
				ctrl.SetWeaponADS(false);
			}
		}
		BGONE_BotInput.Release("CharacterFire");

		return string.Format("guided launch timed out after %1 ms (trigger=%2 srv=%3)", m_iTimeoutMs, m_bTriggerPulled, m_sLastServerDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 1 verification: VIS Top Attack climbs > +12m, transitions to stage 3 terminal dive,
//! never self-destructs mid-climb, matches Target A RplId, and detonates near Target A.
class BGONE_BotBeat_VerifyVisTopFlight : BGONE_BotBeat
{
	protected static const int POLL_MS = 200;

	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyVisTopFlight(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("VIS Top Attack missile did not climb, dive to terminal stage, and detonate on target.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				int detCount = Math.Round(srvVal[2]);
				if (detCount >= 1)
				{
					string tgtRpl = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "tgtRpl");
					float maxAlt = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "maxAlt").ToFloat();
					float launchAlt = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "lAlt").ToFloat();
					int peakStage = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "stage").ToInt();
					int sdFlag = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "sd").ToInt();
					float distA = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "distA").ToFloat();

					float baseAlt = Math.Max(launchAlt, BGONE_GuidanceBotContext.s_vTargetAPos[1]);
					float climbGain = maxAlt - baseAlt;

					if (tgtRpl != BGONE_GuidanceBotContext.s_sTargetARpl)
					{
						m_sDetail = string.Format("targetRplId mismatch (expected=%1 got=%2)", BGONE_GuidanceBotContext.s_sTargetARpl, tgtRpl);
						return EBGONE_BotBeatState.FAIL;
					}

					if (sdFlag != 0)
					{
						m_sDetail = string.Format("VIS TOP missile self-destructed mid-flight (stage=%1 climb=%2m | %3)", peakStage, climbGain, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					if (climbGain < 12.0 || peakStage < 3)
					{
						m_sDetail = string.Format("VIS TOP did not loft & dive (climb=%1m < 12m or peakStage=%2 < 3 | %3)", climbGain, peakStage, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					if (distA > 25.0)
					{
						m_sDetail = string.Format("VIS TOP detonated too far from Target A (%1m > 25m | %2)", distA, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					run.LogTelemetry(string.Format(
						"vis_top_pass=1 tgtRpl=%1 climbGain=%2 peakStage=%3 distA=%4 sd=%5 det=%6",
						tgtRpl, climbGain, peakStage, distA, sdFlag, detCount));
					m_sDetail = string.Format(
						"VIS TOP climbed +%1m, reached stage %2, impacted %3m from Target A (det=%4)",
						climbGain, peakStage, distA, detCount);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("VIS TOP verification timed out after %1 ms (last=%2)", m_iTimeoutMs, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 2 verification: server missile launched with Target B RplId (not Target A) and detonated.
class BGONE_BotBeat_VerifyVisSwitchLaunch : BGONE_BotBeat
{
	protected static const int POLL_MS = 200;

	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyVisSwitchLaunch(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Server VIS missile did not carry switched Target B RplId or did not detonate cleanly.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				int detCount = Math.Round(srvVal[2]);
				if (detCount >= 1)
				{
					string tgtRpl = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "tgtRpl");
					float distB = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "distB").ToFloat();

					if (tgtRpl != BGONE_GuidanceBotContext.s_sTargetBRpl || tgtRpl == BGONE_GuidanceBotContext.s_sTargetARpl)
					{
						m_sDetail = string.Format(
							"server missile targetRplId wrong (got=%1 expectedB=%2 staleA=%3)",
							tgtRpl, BGONE_GuidanceBotContext.s_sTargetBRpl, BGONE_GuidanceBotContext.s_sTargetARpl);
						return EBGONE_BotBeatState.FAIL;
					}

					run.LogTelemetry(string.Format("vis_switch_pass=1 tgtRpl=%1 distB=%2 det=%3", tgtRpl, distB, detCount));
					m_sDetail = string.Format("server VIS missile locked Target B (%1) and detonated (distB=%2m)", tgtRpl, distB);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("VIS switch verification timed out after %1 ms (last=%2)", m_iTimeoutMs, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 3 verification: asks server to move Target A behind shooter once missile is airborne,
//! then verifies VIS seeker self-destructs via m_fNoTargetDestructTime well before TTL.
class BGONE_BotBeat_DisruptAndVerifyVisSelfDestruct : BGONE_BotBeat
{
	protected static const int POLL_MS = 150;

	protected bool m_bDisrupted;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_DisruptAndVerifyVisSelfDestruct(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("VIS missile did not self-destruct via seeker m_fNoTargetDestructTime after target loss.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bDisrupted = false;
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (!m_bDisrupted)
			{
				if (srvOk)
				{
					m_bDisrupted = true;
					run.LogTelemetry(string.Format("vis_target_disrupted=1 detail=%1", m_sLastDetail));
				}
			}
			else if (srvOk)
			{
				float flightTime = srvVal[0];
				int detCount = Math.Round(srvVal[2]);
				if (detCount >= 1)
				{
					int sdFlag = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "sd").ToInt();
					if (sdFlag != 1)
					{
						m_sDetail = string.Format("missile detonated without seeker self-destruct flag (sd=%1 flight=%2s | %3)", sdFlag, flightTime, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					if (flightTime < 1.5 || flightTime > 10.0)
					{
						m_sDetail = string.Format("seeker self-destruct flightTime out of expected [1.5, 10.0]s window (%1s)", flightTime);
						return EBGONE_BotBeatState.FAIL;
					}

					run.LogTelemetry(string.Format("vis_sd_pass=1 sd=%1 flightTime=%2 det=%3", sdFlag, flightTime, detCount));
					m_sDetail = string.Format("VIS seeker self-destructed cleanly at flightTime=%1s (sd=1 det=%2)", flightTime, detCount);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			if (!m_bDisrupted)
				m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_DISRUPT_TARGET);
			else
				m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("VIS self-destruct check timed out (disrupted=%1 last=%2)", m_bDisrupted, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 4 (PLOS): aims down clear corridor, holds PLOS lock while panning +8 deg/s right and
//! +2 deg/s up for 1.6s, verifies client yawChange/pitchChange in degrees/sec, then fires.
class BGONE_BotBeat_PlosTrackPanAndFire : BGONE_BotBeat
{
	protected static const int PAN_DURATION_MS = 1600;
	protected static const float PAN_YAW_RATE_DEG_S = 8.0;
	protected static const float PAN_PITCH_RATE_DEG_S = 2.0;
	protected static const int SERVER_POLL_MS = 200;

	protected int m_iPhase;
	protected int m_iPanStartMs;
	protected float m_fBaseYaw;
	protected float m_fBasePitch;
	protected bool m_bTriggerPulled;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_PlosTrackPanAndFire(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("PLOS launcher failed to track lateral+vertical pan in degrees/sec and fire.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPhase = 0;
		m_iPanStartMs = -1;
		m_fBaseYaw = BGONE_GuidanceBotContext.s_vCorridorDir.ToYaw();
		m_fBasePitch = 6.0;
		m_bTriggerPulled = false;
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ch || !ctrl || !launcherEnt)
		{
			m_sDetail = "missing character, controller, or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_LockType_PLOS plosLock = BGONE_LockType_PLOS.Cast(launcher.GetLockTypeComponent());
		if (!plosLock)
		{
			m_sDetail = "launcher lock component is not BGONE_LockType_PLOS";
			return EBGONE_BotBeatState.FAIL;
		}

		float yawErr;
		float pitchErr;

		// Phase 0: settle initial aim & enter ADS.
		if (m_iPhase == 0)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);
			BGONE_GuidanceBotHelper.DriveCharacterAimAngles(ch, ctrl, m_fBaseYaw, m_fBasePitch, 1.5, yawErr, pitchErr);

			if (ctrl.IsWeaponADS() && Math.AbsFloat(yawErr) <= 4.0 && Math.AbsFloat(pitchErr) <= 4.0)
			{
				launcher.SetLockInputState(true);
				m_iPanStartMs = elapsedMs;
				m_iPhase = 1;
			}

			return EBGONE_BotBeatState.RUNNING;
		}

		// Phase 1: steadily pan yaw (+8 deg/s) and pitch (+2 deg/s) while holding PLOS lock.
		if (m_iPhase == 1)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);

			float panSec = (elapsedMs - m_iPanStartMs) * 0.001;
			float curYaw = m_fBaseYaw + PAN_YAW_RATE_DEG_S * panSec;
			float curPitch = m_fBasePitch + PAN_PITCH_RATE_DEG_S * panSec;
			BGONE_GuidanceBotHelper.DriveCharacterAimAngles(ch, ctrl, curYaw, curPitch, 0.3, yawErr, pitchErr);

			if (elapsedMs - m_iPanStartMs < PAN_DURATION_MS)
				return EBGONE_BotBeatState.RUNNING;

			if (!plosLock.HasInitialSample())
			{
				m_sDetail = "PLOS HasInitialSample() still false after 1.6s tracking pan";
				return EBGONE_BotBeatState.FAIL;
			}

			float cliYawChg = plosLock.GetYawChange();
			float cliPitchChg = plosLock.GetPitchChange();
			BGONE_GuidanceBotContext.s_fClientPlosYawChange = cliYawChg;
			BGONE_GuidanceBotContext.s_fClientPlosPitchChange = cliPitchChg;

			if (cliYawChg <= 1.5)
			{
				m_sDetail = string.Format("PLOS client yawChange too small (%1 deg/s <= 1.5)", cliYawChg);
				return EBGONE_BotBeatState.FAIL;
			}

			if (cliPitchChg <= 0.3)
			{
				m_sDetail = string.Format("PLOS client pitchChange too small (%1 deg/s <= 0.3; sin(pitch) regression?)", cliPitchChg);
				return EBGONE_BotBeatState.FAIL;
			}

			run.LogTelemetry(string.Format("plos_cli_lock=1 cliYawChg=%1 cliPitchChg=%2", cliYawChg, cliPitchChg));
			m_iPhase = 2;
		}

		// Phase 2: pull trigger immediately while PLOS target data is hot, confirm server launch.
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
			ctrl.SetFireWeaponWanted(true);
			BGONE_BotInput.Hold("CharacterFire", 1.0);
			m_bTriggerPulled = true;
		}
		else
		{
			ctrl.SetFireWeaponWanted(false);
			ctrl.SetWeaponADSInput(false);
			ctrl.SetWeaponADS(false);
			BGONE_BotInput.Release("CharacterFire");
		}

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				ctrl.SetFireWeaponWanted(false);
				ctrl.SetWeaponADSInput(false);
				ctrl.SetWeaponADS(false);
				BGONE_BotInput.Release("CharacterFire");
				m_sDetail = string.Format(
					"PLOS tracked pan (cliYaw=%1 cliPitch=%2) and launched (%3)",
					BGONE_GuidanceBotContext.s_fClientPlosYawChange,
					BGONE_GuidanceBotContext.s_fClientPlosPitchChange, m_sLastDetail);
				return EBGONE_BotBeatState.PASS;
			}
		}

		if (m_bTriggerPulled && (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS))
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
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

		return string.Format("PLOS track/pan/fire timed out (phase=%1 trigger=%2 srv=%3)", m_iPhase, m_bTriggerPulled, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 4 verification: server missile received non-zero yawChange/pitchChange, curved > 10m
//! in the pan direction, and detonated cleanly.
class BGONE_BotBeat_VerifyPlosFlight : BGONE_BotBeat
{
	protected static const int POLL_MS = 200;

	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyPlosFlight(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Server PLOS missile did not fly the predicted lead curve and detonate cleanly.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				int detCount = Math.Round(srvVal[2]);
				if (detCount >= 1)
				{
					float srvYaw = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "yawC").ToFloat();
					float srvPitch = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "pitC").ToFloat();
					float latDisp = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "lat").ToFloat();

					if (srvYaw <= 1.0)
					{
						m_sDetail = string.Format("server PLOS yawChange too small (%1 <= 1.0 | %2)", srvYaw, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					if (srvPitch <= 0.3)
					{
						m_sDetail = string.Format("server PLOS pitchChange too small (%1 <= 0.3 | %2)", srvPitch, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					if (latDisp <= 10.0)
					{
						m_sDetail = string.Format("PLOS missile lateral lead curve too small (%1m <= 10m | %2)", latDisp, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					run.LogTelemetry(string.Format(
						"plos_pass=1 srvYaw=%1 srvPitch=%2 latDisp=%3 det=%4",
						srvYaw, srvPitch, latDisp, detCount));
					m_sDetail = string.Format(
						"PLOS missile curved +%1m laterally (srvYaw=%2 srvPitch=%3 det=%4)",
						latDisp, srvYaw, srvPitch, detCount);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("PLOS flight verification timed out after %1 ms (last=%2)", m_iTimeoutMs, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
//! Scenario 5 (SACLOS): aims down clear corridor, fires SACLOS missile, steers +15 deg right
//! after 0.3s flight, verifies > 15m rightward deflection (no X inversion, retained ownerAimDir),
//! then pitches down to impact terrain cleanly (det == 1).
class BGONE_BotBeat_SaclosFireSteerAndVerify : BGONE_BotBeat
{
	protected static const int POLL_MS = 150;
	protected static const float STEER_RIGHT_DEG = 15.0;

	protected int m_iPhase;
	protected float m_fLaunchYaw;
	protected float m_fLaunchPitch;
	protected float m_fPeakLatDisp;
	protected float m_fPeakFlightTime;
	protected float m_fPeakTravelDist;
	protected bool m_bPitchDownForImpact;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected string m_sLastDetail;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_SaclosFireSteerAndVerify(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("SACLOS missile failed to steer rightward onto the beam and detonate cleanly.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iPhase = 0;
		m_fLaunchYaw = BGONE_GuidanceBotContext.s_vCorridorDir.ToYaw();
		m_fLaunchPitch = 6.0;
		m_fPeakLatDisp = -999.0;
		m_fPeakFlightTime = 0.0;
		m_fPeakTravelDist = 0.0;
		m_bPitchDownForImpact = false;
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_sLastDetail = "no query";
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ch || !ctrl || !launcherEnt)
		{
			m_sDetail = "missing character, controller, or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		float yawErr;
		float pitchErr;

		// Phase 0: aim along launch corridor and raise weapon.
		if (m_iPhase == 0)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);
			BGONE_GuidanceBotHelper.DriveCharacterAimAngles(ch, ctrl, m_fLaunchYaw, m_fLaunchPitch, 1.5, yawErr, pitchErr);

			if (ctrl.IsWeaponADS() && Math.AbsFloat(yawErr) <= 4.0 && Math.AbsFloat(pitchErr) <= 4.0)
				m_iPhase = 1;

			return EBGONE_BotBeatState.RUNNING;
		}

		// Phase 1: fire SACLOS missile along launch corridor.
		if (m_iPhase == 1)
		{
			bool muzzleEmpty = false;
			BaseWeaponComponent wep = BaseWeaponComponent.Cast(launcherEnt.FindComponent(BaseWeaponComponent));
			if (wep)
			{
				BaseMuzzleComponent muzzle = wep.GetCurrentMuzzle();
				if (muzzle && muzzle.GetAmmoCount() <= 0)
					muzzleEmpty = true;
			}

			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);
			BGONE_GuidanceBotHelper.DriveCharacterAimAngles(ch, ctrl, m_fLaunchYaw, m_fLaunchPitch, 1.5, yawErr, pitchErr);

			if (!muzzleEmpty)
			{
				ctrl.SetFireWeaponWanted(true);
				BGONE_BotInput.Hold("CharacterFire", 1.0);
			}
			else
			{
				ctrl.SetFireWeaponWanted(false);
				BGONE_BotInput.Release("CharacterFire");
			}
		}
		else
		{
			// Phase 2: steer +15 deg right; once deflection > 20m (or flightTime >= 2.5s), pitch down -12 deg for clean terrain impact.
			ctrl.SetFireWeaponWanted(false);
			BGONE_BotInput.Release("CharacterFire");
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);

			float targetPitch = 2.0;
			if (m_bPitchDownForImpact)
				targetPitch = -12.0;

			BGONE_GuidanceBotHelper.DriveCharacterAimAngles(ch, ctrl, m_fLaunchYaw + STEER_RIGHT_DEG, targetPitch, 0.5, yawErr, pitchErr);
		}

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			m_sLastDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk)
			{
				float flightTime = srvVal[0];
				float travelDist = srvVal[1];
				int detCount = Math.Round(srvVal[2]);
				float latDisp = BGONE_FlightBotHelper.ExtractToken(m_sLastDetail, "lat").ToFloat();

				if (flightTime > m_fPeakFlightTime)
					m_fPeakFlightTime = flightTime;
				if (travelDist > m_fPeakTravelDist)
					m_fPeakTravelDist = travelDist;
				if (latDisp > m_fPeakLatDisp)
					m_fPeakLatDisp = latDisp;

				if (m_iPhase == 1 && flightTime >= 0.3)
				{
					ctrl.SetFireWeaponWanted(false);
					BGONE_BotInput.Release("CharacterFire");
					run.LogTelemetry(string.Format("saclos_launched=1 flightTime=%1", flightTime));
					m_iPhase = 2;
				}

				if (m_iPhase == 2 && !m_bPitchDownForImpact && (m_fPeakLatDisp >= 20.0 || flightTime >= 2.5))
					m_bPitchDownForImpact = true;

				if (detCount >= 1)
				{
					ctrl.SetFireWeaponWanted(false);
					ctrl.SetWeaponADSInput(false);
					ctrl.SetWeaponADS(false);
					BGONE_BotInput.Release("CharacterFire");

					if (m_fPeakLatDisp <= 15.0)
					{
						m_sDetail = string.Format(
							"SACLOS missile did not steer rightward (peakLat=%1m <= 15m flight=%2s dist=%3m | %4)",
							m_fPeakLatDisp, m_fPeakFlightTime, m_fPeakTravelDist, m_sLastDetail);
						return EBGONE_BotBeatState.FAIL;
					}

					run.LogTelemetry(string.Format(
						"saclos_pass=1 peakLat=%1 flightTime=%2 dist=%3 det=%4",
						m_fPeakLatDisp, m_fPeakFlightTime, m_fPeakTravelDist, detCount));
					m_sDetail = string.Format(
						"SACLOS steered +%1m right and detonated cleanly (flight=%2s dist=%3m det=%4)",
						m_fPeakLatDisp, m_fPeakFlightTime, m_fPeakTravelDist, detCount);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE);
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
			"SACLOS steer verification timed out (phase=%1 peakLat=%2 flight=%3 dist=%4 last=%5)",
			m_iPhase, m_fPeakLatDisp, m_fPeakFlightTime, m_fPeakTravelDist, m_sLastDetail);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	protected IEntity m_BGONE_GuidanceTargetA;
	protected IEntity m_BGONE_GuidanceTargetB;
	protected int m_iBGONE_GuidanceStartEpoch;
	protected vector m_vBGONE_GuidanceTargetAPos;
	protected vector m_vBGONE_GuidanceTargetBPos;
	protected bool m_bBGONE_GuidanceDisrupted;
	protected vector m_vBGONE_GuidanceCorridorDir;

	//------------------------------------------------------------------------------------------------
	override protected BGONE_BotRun BGONE_BotBuildScenario(string scenarioName)
	{
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TOP_STATIC)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "VIS Top Attack Static Target Loft, Terminal Dive & Impact";
			run.m_sSummary = "Verified VIS lock on BTR70, Top Attack loft (+12m climb, stage 3 dive without FOV self-destruct), and target impact.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_FetchGuidanceTargets("fetch_targets", 10000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_vis", 20000));
			run.Add(new BGONE_BotBeat_SelectAttackProfile("select_top_attack", 1, 10000));
			run.Add(new BGONE_BotBeat_AimAndLockVisTarget("aim_and_lock_btr", false, 20000));
			run.Add(new BGONE_BotBeat_FireGuidedLauncher("fire_vis_top", false, 15000));
			run.Add(new BGONE_BotBeat_VerifyVisTopFlight("verify_vis_top_flight", 35000));
			return run;
		}

		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TARGET_SWITCH)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "VIS Target Switching Without Clobbering or Stale RplId";
			run.m_sSummary = "Locked Target A, panned to Target B within 0.3 rad while holding lock, verified clean switch and server missile Target B RplId.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_FetchGuidanceTargets("fetch_targets", 10000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_vis", 20000));
			run.Add(new BGONE_BotBeat_VisTargetSwitchLock("lock_a_then_switch_b", 30000));
			run.Add(new BGONE_BotBeat_FireGuidedLauncher("fire_vis_switched", false, 15000));
			run.Add(new BGONE_BotBeat_VerifyVisSwitchLaunch("verify_vis_switch_launch", 30000));
			return run;
		}

		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_OCCLUSION_SD)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "VIS Seeker Loss-of-Track Self-Destruct Timer";
			run.m_sSummary = "Locked & fired VIS missile, displaced target >120 deg behind missile mid-flight, and verified m_fNoTargetDestructTime self-destruct.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_FetchGuidanceTargets("fetch_targets", 10000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_vis", 20000));
			run.Add(new BGONE_BotBeat_SelectAttackProfile("select_top_for_airspace", 1, 10000));
			run.Add(new BGONE_BotBeat_AimAndLockVisTarget("aim_and_lock_btr", false, 20000));
			run.Add(new BGONE_BotBeat_FireGuidedLauncher("fire_vis_occlusion", false, 15000));
			run.Add(new BGONE_BotBeat_DisruptAndVerifyVisSelfDestruct("disrupt_and_verify_sd", 25000));
			return run;
		}

		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_PLOS_MOVING)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "PLOS Angular-Rate Lead Prediction (Yaw + Pitch Degrees/Sec)";
			run.m_sSummary = "Tracked steady lateral+vertical pan with PLOS launcher, verified degrees/sec rates on client+server, >10m lead curve, and detonation.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_FetchGuidanceTargets("fetch_targets", 10000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_plos", 20000));
			run.Add(new BGONE_BotBeat_PlosTrackPanAndFire("plos_track_pan_and_fire", 20000));
			run.Add(new BGONE_BotBeat_VerifyPlosFlight("verify_plos_flight", 25000));
			return run;
		}

		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_SACLOS_STEER)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "SACLOS Beam-Riding Rightward Steering & Terrain Impact";
			run.m_sSummary = "Fired SACLOS missile, steered +15 deg right, verified >15m rightward deflection (no X inversion, retained owner aim), and detonated.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000));
			run.Add(new BGONE_BotBeat_FetchGuidanceTargets("fetch_targets", 10000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_saclos", 20000));
			run.Add(new BGONE_BotBeat_SaclosFireSteerAndVerify("saclos_fire_steer_verify", 35000));
			return run;
		}

		return super.BGONE_BotBuildScenario(scenarioName);
	}

	//------------------------------------------------------------------------------------------------
	protected bool BGONE_GuidanceProbeCorridor(BaseWorld world, IEntity character, vector dirXZ, float checkDist)
	{
		if (!world || !character)
			return false;

		vector origin = character.GetOrigin();
		vector samplePos = origin + dirXZ * checkDist;
		float surfY = world.GetSurfaceY(samplePos[0], samplePos[2]);
		if (surfY < origin[1] - 0.5)
			return false;

		TraceParam tp = new TraceParam();
		tp.Start = origin + Vector(0.0, 1.6, 0.0);
		tp.End = Vector(samplePos[0], surfY + 1.6, samplePos[2]);
		tp.LayerMask = EPhysicsLayerDefs.Projectile;
		tp.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = character;

		float pct = world.TraceMove(tp, null);
		return pct >= 0.98;
	}

	//------------------------------------------------------------------------------------------------
	protected vector BGONE_GuidanceResolveCorridorDir(BaseWorld world, IEntity character, float checkDist)
	{
		vector westDir = Vector(-1.0, 0.0, 0.0);
		if (BGONE_GuidanceProbeCorridor(world, character, westDir, checkDist))
			return westDir;

		vector northDir = Vector(0.0, 0.0, 1.0);
		if (BGONE_GuidanceProbeCorridor(world, character, northDir, checkDist))
			return northDir;

		vector southDir = Vector(0.0, 0.0, -1.0);
		if (BGONE_GuidanceProbeCorridor(world, character, southDir, checkDist))
			return southDir;

		vector eastSouthDir = Vector(0.95, 0.0, -0.31).Normalized();
		if (BGONE_GuidanceProbeCorridor(world, character, eastSouthDir, checkDist))
			return eastSouthDir;

		return westDir;
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity BGONE_GuidanceSpawnEntity(
		BaseWorld world,
		string scenarioName,
		ResourceName prefab,
		vector worldOrigin,
		float yawDeg)
	{
		if (!world)
			return null;

		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
			return null;

		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(yawDeg, 0.0, 0.0), spawnParams.Transform);
		spawnParams.Transform[3] = worldOrigin;

		IEntity spawned = GetGame().SpawnEntityPrefab(res, world, spawnParams);
		if (spawned)
			BGONE_BotSpawnLedger.Add(spawned, scenarioName);

		return spawned;
	}

	//------------------------------------------------------------------------------------------------
	override protected int BGONE_BotStageScenario(string scenarioName, IEntity character, out RplId fixtureId, out vector fixturePos, out string detail)
	{
		if (!BGONE_GuidanceBotHelper.IsGuidanceScenario(scenarioName))
			return super.BGONE_BotStageScenario(scenarioName, character, fixtureId, fixturePos, detail);

		fixtureId = RplId.Invalid();
		fixturePos = vector.Zero;
		m_BGONE_GuidanceTargetA = null;
		m_BGONE_GuidanceTargetB = null;
		m_vBGONE_GuidanceTargetAPos = vector.Zero;
		m_vBGONE_GuidanceTargetBPos = vector.Zero;
		m_bBGONE_GuidanceDisrupted = false;
		m_iBGONE_GuidanceStartEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();

		if (!character)
		{
			detail = "no character to stage guidance scenario near";
			return 20;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			detail = "no active world for guidance staging";
			return 21;
		}

		ResourceName launcherPrefab = BGONE_GuidanceBotConst.PREFAB_LAUNCHER_VIS;
		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_PLOS_MOVING)
			launcherPrefab = BGONE_GuidanceBotConst.PREFAB_LAUNCHER_PLOS;
		else if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_SACLOS_STEER)
			launcherPrefab = BGONE_GuidanceBotConst.PREFAB_LAUNCHER_SACLOS;

		// Spawn launcher 0.9m in front of shooter so pickup beat grabs it immediately.
		vector charFwd = character.GetWorldTransformAxis(2);
		charFwd[1] = 0.0;
		if (charFwd.LengthSq() < 0.001)
			charFwd = Vector(0.0, 0.0, 1.0);
		else
			charFwd.Normalize();

		vector launcherOrigin = character.GetOrigin() + charFwd * 0.9;
		launcherOrigin[1] = world.GetSurfaceY(launcherOrigin[0], launcherOrigin[2]) + 0.35;

		IEntity launcher = BGONE_GuidanceSpawnEntity(world, scenarioName, launcherPrefab, launcherOrigin, 0.0);
		if (!launcher)
		{
			detail = string.Format("failed to spawn launcher %1", launcherPrefab);
			return 22;
		}

		m_BGONE_ServerStagedEntityId = launcher.GetID();
		m_vBGONE_ServerStartPos = character.GetOrigin();
		fixturePos = launcher.GetOrigin();

		RplComponent launcherRpl = RplComponent.Cast(launcher.FindComponent(RplComponent));
		if (launcherRpl && launcherRpl.Id().IsValid())
			fixtureId = launcherRpl.Id();

		// Resolve clear solid-terrain firing corridor (prefers West -X; falls back to clear solid quadrant).
		float probeDist = 280.0;
		m_vBGONE_GuidanceCorridorDir = BGONE_GuidanceResolveCorridorDir(world, character, probeDist);
		float broadsideYaw = m_vBGONE_GuidanceCorridorDir.ToYaw() + 90.0;
		vector rightDir = BGONE_GuidanceBotHelper.RightVectorXZ(m_vBGONE_GuidanceCorridorDir);
		vector shooterOrigin = character.GetOrigin();

		if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TOP_STATIC)
		{
			vector posA = shooterOrigin + m_vBGONE_GuidanceCorridorDir * 220.0;
			posA[1] = world.GetSurfaceY(posA[0], posA[2]) + 0.2;
			m_BGONE_GuidanceTargetA = BGONE_GuidanceSpawnEntity(world, scenarioName, BGONE_GuidanceBotConst.PREFAB_TARGET_BTR70, posA, broadsideYaw);
			if (!m_BGONE_GuidanceTargetA)
			{
				detail = "failed to spawn BTR70 Target A for vis_top_static";
				return 23;
			}
			m_vBGONE_GuidanceTargetAPos = m_BGONE_GuidanceTargetA.GetOrigin();
		}
		else if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_TARGET_SWITCH)
		{
			// 200m forward, +/-25m lateral (~14.2 deg separation -> both inside 0.3 rad / 17.2 deg seeker cone).
			vector posA = shooterOrigin + m_vBGONE_GuidanceCorridorDir * 200.0 - rightDir * 25.0;
			posA[1] = world.GetSurfaceY(posA[0], posA[2]) + 0.2;
			vector posB = shooterOrigin + m_vBGONE_GuidanceCorridorDir * 200.0 + rightDir * 25.0;
			posB[1] = world.GetSurfaceY(posB[0], posB[2]) + 0.2;

			m_BGONE_GuidanceTargetA = BGONE_GuidanceSpawnEntity(world, scenarioName, BGONE_GuidanceBotConst.PREFAB_TARGET_BTR70, posA, broadsideYaw);
			m_BGONE_GuidanceTargetB = BGONE_GuidanceSpawnEntity(world, scenarioName, BGONE_GuidanceBotConst.PREFAB_TARGET_BTR70, posB, broadsideYaw);
			if (!m_BGONE_GuidanceTargetA || !m_BGONE_GuidanceTargetB)
			{
				detail = "failed to spawn Target A/B BTR70s for vis_target_switch";
				return 24;
			}
			m_vBGONE_GuidanceTargetAPos = m_BGONE_GuidanceTargetA.GetOrigin();
			m_vBGONE_GuidanceTargetBPos = m_BGONE_GuidanceTargetB.GetOrigin();
		}
		else if (scenarioName == BGONE_GuidanceBotConst.SCENARIO_VIS_OCCLUSION_SD)
		{
			vector posA = shooterOrigin + m_vBGONE_GuidanceCorridorDir * 280.0;
			posA[1] = world.GetSurfaceY(posA[0], posA[2]) + 0.2;
			m_BGONE_GuidanceTargetA = BGONE_GuidanceSpawnEntity(world, scenarioName, BGONE_GuidanceBotConst.PREFAB_TARGET_BTR70, posA, broadsideYaw);
			if (!m_BGONE_GuidanceTargetA)
			{
				detail = "failed to spawn BTR70 Target A for vis_occlusion_sd";
				return 25;
			}
			m_vBGONE_GuidanceTargetAPos = m_BGONE_GuidanceTargetA.GetOrigin();
		}

		detail = string.Format("staged %1 dir=%2 tgtA=%3", scenarioName, m_vBGONE_GuidanceCorridorDir, m_vBGONE_GuidanceTargetAPos);
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	override protected bool BGONE_BotAnswerQuery(int queryId, out int ok, out vector val, out string detail)
	{
		if (queryId == BGONE_GuidanceBotConst.QUERY_GUIDANCE_TARGET_INFO)
		{
			string rplAStr = "";
			if (m_BGONE_GuidanceTargetA)
			{
				RplId idA = BGONE_TargetData.ResolveEntityRplId(m_BGONE_GuidanceTargetA);
				if (idA.IsValid())
					rplAStr = idA.AsString();
			}

			string rplBStr = "";
			if (m_BGONE_GuidanceTargetB)
			{
				RplId idB = BGONE_TargetData.ResolveEntityRplId(m_BGONE_GuidanceTargetB);
				if (idB.IsValid())
					rplBStr = idB.AsString();
			}

			ok = 1;
			val = m_vBGONE_GuidanceTargetAPos;
			detail = string.Format(
				"rplA=%1 rplB=%2 epoch=%3 bx=%4 by=%5 bz=%6 cx=%7 cz=%8",
				rplAStr, rplBStr, m_iBGONE_GuidanceStartEpoch,
				m_vBGONE_GuidanceTargetBPos[0], m_vBGONE_GuidanceTargetBPos[1], m_vBGONE_GuidanceTargetBPos[2],
				m_vBGONE_GuidanceCorridorDir[0], m_vBGONE_GuidanceCorridorDir[2]);
			return true;
		}

		if (queryId == BGONE_GuidanceBotConst.QUERY_GUIDANCE_DISRUPT_TARGET)
		{
			int curEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
			float flightTime = BGONE_GuidedMissileComponent.GetLastTrackedFlightTime();
			if (curEpoch <= m_iBGONE_GuidanceStartEpoch || flightTime < 0.35)
			{
				ok = 0;
				val = Vector(flightTime, curEpoch, m_iBGONE_GuidanceStartEpoch);
				detail = string.Format("waiting for airborne missile (epoch=%1/%2 flight=%3)", curEpoch, m_iBGONE_GuidanceStartEpoch, flightTime);
				return true;
			}

			if (!m_bBGONE_GuidanceDisrupted && m_BGONE_GuidanceTargetA)
			{
				BaseWorld world = GetGame().GetWorld();
				vector behindPos = m_vBGONE_ServerStartPos - m_vBGONE_GuidanceCorridorDir * 120.0;
				if (world)
					behindPos[1] = world.GetSurfaceY(behindPos[0], behindPos[2]) + 0.5;

				m_BGONE_GuidanceTargetA.SetOrigin(behindPos);
				m_BGONE_GuidanceTargetA.Update();
				m_bBGONE_GuidanceDisrupted = true;
			}

			ok = 1;
			val = Vector(flightTime, 1.0, 0.0);
			detail = string.Format("disrupted Target A behind shooter at flightTime=%1s", flightTime);
			return true;
		}

		if (queryId == BGONE_GuidanceBotConst.QUERY_GUIDANCE_MISSILE_STATE)
		{
			int curEpoch = BGONE_GuidedMissileComponent.GetLastLaunchEpoch();
			if (curEpoch <= m_iBGONE_GuidanceStartEpoch)
			{
				ok = 0;
				val = vector.Zero;
				detail = string.Format("waiting for launch (epoch=%1 start=%2)", curEpoch, m_iBGONE_GuidanceStartEpoch);
				return true;
			}

			float flightTime = BGONE_GuidedMissileComponent.GetLastTrackedFlightTime();
			float travelDist = BGONE_GuidedMissileComponent.GetLastTrackedTravelDistance();
			int detCount = BGONE_GuidedMissileComponent.GetLastTrackedDetonationCount();
			float maxAlt = BGONE_GuidedMissileComponent.GetLastTrackedMaxAltitude();
			int peakStage = BGONE_GuidedMissileComponent.GetLastTrackedPeakAttackStage();
			float yawC = BGONE_GuidedMissileComponent.GetLastTrackedYawChange();
			float pitC = BGONE_GuidedMissileComponent.GetLastTrackedPitchChange();
			vector lastOrigin = BGONE_GuidedMissileComponent.GetLastTrackedOrigin();
			vector launchOrigin = BGONE_GuidedMissileComponent.GetLastTrackedLaunchOrigin();
			vector launchFwd = BGONE_GuidedMissileComponent.GetLastTrackedLaunchForward();

			int sdFlag = 0;
			if (BGONE_GuidedMissileComponent.WasLastTrackedSelfDestructed())
				sdFlag = 1;

			RplId tgtId = BGONE_GuidedMissileComponent.GetLastTrackedTargetRplId();
			string tgtRplStr = "invalid";
			if (tgtId.IsValid())
				tgtRplStr = tgtId.AsString();

			float distA = 9999.0;
			if (m_BGONE_GuidanceTargetA)
				distA = vector.Distance(lastOrigin, m_BGONE_GuidanceTargetA.GetOrigin());
			else if (m_vBGONE_GuidanceTargetAPos != vector.Zero)
				distA = vector.Distance(lastOrigin, m_vBGONE_GuidanceTargetAPos);

			float distB = 9999.0;
			if (m_BGONE_GuidanceTargetB)
				distB = vector.Distance(lastOrigin, m_BGONE_GuidanceTargetB.GetOrigin());
			else if (m_vBGONE_GuidanceTargetBPos != vector.Zero)
				distB = vector.Distance(lastOrigin, m_vBGONE_GuidanceTargetBPos);

			vector refFwd = launchFwd;
			if (refFwd.LengthSq() < 0.001)
				refFwd = m_vBGONE_GuidanceCorridorDir;
			vector rightVec = BGONE_GuidanceBotHelper.RightVectorXZ(refFwd);
			vector disp = lastOrigin - launchOrigin;
			disp[1] = 0.0;
			float latDisp = vector.Dot(disp, rightVec);

			string part1 = string.Format(
				"tgtRpl=%1 maxAlt=%2 lAlt=%3 stage=%4 sd=%5 distA=%6 distB=%7",
				tgtRplStr, maxAlt, launchOrigin[1], peakStage, sdFlag, distA, distB);
			string part2 = string.Format(
				"yawC=%1 pitC=%2 lat=%3",
				yawC, pitC, latDisp);

			ok = 1;
			val = Vector(flightTime, travelDist, detCount);
			detail = part1 + " " + part2;
			return true;
		}

		return super.BGONE_BotAnswerQuery(queryId, ok, val, detail);
	}
}
