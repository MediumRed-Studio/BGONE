/*!
	\brief Reusable headless MP bot beats for BGONE (no OS mouse/keyboard required).

	Completely decoupled from BGONE weapon/missile internals: uses only vanilla Reforger 1.9.0.37
	character, aiming, camera, and input APIs.
*/
class BGONE_BotMath
{
	//------------------------------------------------------------------------------------------------
	//! Signed angular delta (toDeg - fromDeg) wrapped into (-180, 180].
	static float AngleDelta(float fromDeg, float toDeg)
	{
		float delta = toDeg - fromDeg;
		while (delta > 180)
			delta = delta - 360;

		while (delta <= -180)
			delta = delta + 360;

		return delta;
	}

	//------------------------------------------------------------------------------------------------
	static float FacingYaw(IEntity ent)
	{
		vector fwd = ent.GetWorldTransformAxis(2);
		fwd[1] = 0;
		return fwd.ToYaw();
	}

	//------------------------------------------------------------------------------------------------
	static bool LookYaw(out float yawDeg)
	{
		CameraManager camMgr = GetGame().GetCameraManager();
		if (!camMgr)
			return false;

		CameraBase cam = camMgr.CurrentCamera();
		if (!cam)
			return false;

		vector mat[4];
		cam.GetWorldTransform(mat);
		vector fwd = mat[2];
		fwd[1] = 0;
		if (fwd.LengthSq() < 0.0001)
			return false;

		yawDeg = fwd.ToYaw();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static float FlatDistance(vector a, vector b)
	{
		a[1] = 0;
		b[1] = 0;
		return vector.Distance(a, b);
	}
}

//------------------------------------------------------------------------------------------------
//! Wait until the local player controls a live character.
class BGONE_BotBeat_WaitDeployed : BGONE_BotBeat
{
	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_WaitDeployed(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("The player did not deploy into a live character.");
		MarkSetup();
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (!ch || !ctrl || ctrl.IsDead())
			return EBGONE_BotBeatState.RUNNING;

		m_sDetail = string.Format("deployed at %1 yaw=%2", ch.GetOrigin(), BGONE_BotMath.FacingYaw(ch));
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Hold still for a fixed duration in milliseconds.
class BGONE_BotBeat_WaitDuration : BGONE_BotBeat
{
	protected int m_iWaitMs;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_WaitDuration(string beatName, int waitMs)
	{
		Setup(beatName, waitMs + 5000);
		m_iWaitMs = waitMs;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (elapsedMs < m_iWaitMs)
			return EBGONE_BotBeatState.RUNNING;

		m_sDetail = string.Format("waited %1 ms", m_iWaitMs);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Headless aim toward a world point (or the staged fixture's position): yaw via SetHeadingAngle,
//! pitch via BGONE_BotInput (CharacterAimUp/Down) plus CharacterAimingComponent.SetAimingRotationWanted.
class BGONE_BotBeat_AimAtPoint : BGONE_BotBeat
{
	protected static const int STABLE_MS = 250;

	protected vector m_vTargetPoint;
	protected bool m_bUseFixture;
	protected float m_fToleranceDeg;
	protected int m_iOnTargetSinceMs;
	protected float m_fLastYawErr;
	protected float m_fLastPitchErr;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_AimAtPoint(string beatName, vector targetPoint, bool useFixture, float toleranceDeg, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not aim at the target point.");
		m_vTargetPoint = targetPoint;
		m_bUseFixture = useFixture;
		if (toleranceDeg <= 0)
			m_fToleranceDeg = 3.0;
		else
			m_fToleranceDeg = toleranceDeg;

		m_iOnTargetSinceMs = -1;
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

		vector aimPoint = m_vTargetPoint;
		if (m_bUseFixture)
		{
			IEntity fix = run.FixtureEntity();
			if (fix)
				aimPoint = fix.GetOrigin();
			else
				aimPoint = run.m_vFixturePos;
		}

		vector eye = ch.EyePosition();
		vector toTarget = aimPoint - eye;
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
		float curPitch = 0;
		if (inputCtx)
			curPitch = inputCtx.GetAimingAngles()[1] * Math.RAD2DEG;

		m_fLastPitchErr = desiredPitch - curPitch;

		// Apply proportional pitch via BGONE_BotInput and also nudge CharacterAimingComponent directly.
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

		bool onTarget = (Math.AbsFloat(m_fLastYawErr) <= m_fToleranceDeg) && (Math.AbsFloat(m_fLastPitchErr) <= m_fToleranceDeg);
		if (!onTarget)
		{
			m_iOnTargetSinceMs = -1;
			return EBGONE_BotBeatState.RUNNING;
		}

		if (m_iOnTargetSinceMs < 0)
			m_iOnTargetSinceMs = elapsedMs;

		if (elapsedMs - m_iOnTargetSinceMs < STABLE_MS)
			return EBGONE_BotBeatState.RUNNING;

		m_sDetail = string.Format("aimed at %1 (yawErr=%2 deg, pitchErr=%3 deg, tol=%4 deg)",
			aimPoint, m_fLastYawErr, m_fLastPitchErr, m_fToleranceDeg);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format("aim timed out after %1 ms (yawErr=%2 deg, pitchErr=%3 deg, tol=%4 deg)",
			m_iTimeoutMs, m_fLastYawErr, m_fLastPitchErr, m_fToleranceDeg);
	}
}

//------------------------------------------------------------------------------------------------
//! Raise weapon and enter (or exit) ADS headlessly via CharacterControllerComponent.
class BGONE_BotBeat_EnterADS : BGONE_BotBeat
{
	protected bool m_bWantAds;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_EnterADS(string beatName, bool wantAds, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Weapon ADS state did not reach the requested state.");
		m_bWantAds = wantAds;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (!ctrl)
		{
			m_sDetail = "no character controller";
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_bWantAds)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetWeaponADS(true);
			ctrl.SetWeaponADSInput(true);
			if (ctrl.IsWeaponADS())
			{
				m_sDetail = string.Format("entered ADS in %1 ms", elapsedMs);
				return EBGONE_BotBeatState.PASS;
			}
		}
		else
		{
			ctrl.SetWeaponADSInput(false);
			ctrl.SetWeaponADS(false);
			if (!ctrl.IsWeaponADS())
			{
				m_sDetail = string.Format("exited ADS in %1 ms", elapsedMs);
				return EBGONE_BotBeatState.PASS;
			}
		}

		return EBGONE_BotBeatState.RUNNING;
	}
}

//------------------------------------------------------------------------------------------------
//! Hold weapon fire wanted for a configurable duration then release.
class BGONE_BotBeat_FireWeapon : BGONE_BotBeat
{
	protected int m_iHoldMs;
	protected bool m_bFired;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_FireWeapon(string beatName, int holdMs, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not fire weapon.");
		if (holdMs <= 0)
			m_iHoldMs = 250;
		else
			m_iHoldMs = holdMs;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bFired = false;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (!ctrl)
		{
			m_sDetail = "no character controller";
			return EBGONE_BotBeatState.FAIL;
		}

		if (elapsedMs < m_iHoldMs)
		{
			ctrl.SetWeaponRaised(true);
			ctrl.SetFireWeaponWanted(true);
			m_bFired = true;
			return EBGONE_BotBeatState.RUNNING;
		}

		ctrl.SetFireWeaponWanted(false);
		m_sDetail = string.Format("fired weapon (held %1 ms, canFire=%2)", m_iHoldMs, ctrl.CanFire());
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Send a server query RPC and assert the server's reply matches expectedOk.
class BGONE_BotBeat_ServerQuery : BGONE_BotBeat
{
	protected int m_iQueryId;
	protected bool m_bExpectOk;
	protected int m_iSeq;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_ServerQuery(string beatName, int queryId, bool expectOk, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Server check did not pass.");
		m_iQueryId = queryId;
		m_bExpectOk = expectOk;
		m_iSeq = 0;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iSeq = run.Query(m_iQueryId);
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_iSeq <= 0 || !run.HasAnswer(m_iSeq))
			return EBGONE_BotBeatState.RUNNING;

		bool gotOk = run.AnswerOk(m_iSeq);
		string detail = run.AnswerDetail(m_iSeq);
		vector val = run.AnswerValue(m_iSeq);
		m_sDetail = string.Format("query %1 ok=%2 val=%3 | %4", m_iQueryId, gotOk, val, detail);

		if (gotOk == m_bExpectOk)
			return EBGONE_BotBeatState.PASS;

		return EBGONE_BotBeatState.FAIL;
	}
}
