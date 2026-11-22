/*!
	\brief Headless MP bot scenario `artillery_calc` verifying BGONE_ArtilleryComputerComponent
	ballistic solver, turret limit clamping, MP client+server turret aim sync, and out-of-range refusal.
*/
class BGONE_ArtilleryBotConst
{
	static const string SCENARIO_NAME = "artillery_calc";
	static const ResourceName PREFAB_ARTILLERY_VEHICLE = "{62A47E1B8C3D9F04}Prefabs/QA/BGONE_QA_ArtilleryVehicle.et";
	static const int QUERY_SERVER_TURRET_AIM = 7001;
}

//------------------------------------------------------------------------------------------------
//! Mounts the staged BGONE_QA_ArtilleryVehicle gunner turret compartment and verifies server sees occupancy.
class BGONE_BotBeat_MountArtilleryTurret : BGONE_BotBeat
{
	protected static const int SERVER_POLL_MS = 250;

	protected bool m_bMountRequested;
	protected int m_iLastQueryMs;
	protected int m_iPendingSeq;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_MountArtilleryTurret(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not mount the artillery vehicle gunner turret compartment.");
		m_bMountRequested = false;
		m_iLastQueryMs = -1000;
		m_iPendingSeq = 0;
	}

	//------------------------------------------------------------------------------------------------
	protected BaseCompartmentSlot FindTurretSlotRecursive(IEntity entity, int depth)
	{
		if (!entity || depth > 4)
			return null;

		BaseCompartmentManagerComponent compMgr = BaseCompartmentManagerComponent.Cast(entity.FindComponent(BaseCompartmentManagerComponent));
		if (compMgr)
		{
			array<BaseCompartmentSlot> slots = {};
			compMgr.GetCompartments(slots);
			foreach (BaseCompartmentSlot slot : slots)
			{
				if (!slot)
					continue;

				if (slot.GetType() == ECompartmentType.TURRET && !slot.GetOccupant())
					return slot;
			}
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			BaseCompartmentSlot childSlot = FindTurretSlotRecursive(child, depth + 1);
			if (childSlot)
				return childSlot;

			child = child.GetSibling();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		if (!ch)
		{
			m_sDetail = "no controlled character";
			return EBGONE_BotBeatState.FAIL;
		}

		IEntity veh = run.FixtureEntity();
		if (!veh)
			return EBGONE_BotBeatState.RUNNING;

		BGONE_ArtilleryComputerComponent artComp = BGONE_ArtilleryComputerComponent.Cast(veh.FindComponent(BGONE_ArtilleryComputerComponent));
		if (!artComp)
		{
			m_sDetail = "staged vehicle missing BGONE_ArtilleryComputerComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		CompartmentAccessComponent compAccess = ch.GetCompartmentAccessComponent();
		if (!compAccess)
		{
			m_sDetail = "character missing CompartmentAccessComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (!compAccess.IsInCompartment())
		{
			if (!m_bMountRequested)
			{
				BaseCompartmentSlot turretSlot;
				SCR_CompartmentAccessComponent scrAccess = SCR_CompartmentAccessComponent.Cast(compAccess);
				if (scrAccess)
					turretSlot = scrAccess.FindFreeAndAccessibleCompartment(veh, ECompartmentType.TURRET);

				if (!turretSlot)
					turretSlot = FindTurretSlotRecursive(veh, 0);

				if (!turretSlot)
					return EBGONE_BotBeatState.RUNNING;

				IEntity slotOwner = turretSlot.GetOwner();
				if (!slotOwner)
					slotOwner = veh;

				m_bMountRequested = compAccess.GetInVehicle(slotOwner, turretSlot, true, -1, ECloseDoorAfterActions.INVALID, false);
			}

			return EBGONE_BotBeatState.RUNNING;
		}

		BaseCompartmentSlot activeSlot = compAccess.GetCompartment();
		if (!activeSlot)
			return EBGONE_BotBeatState.RUNNING;

		TurretControllerComponent turretCtrl = TurretControllerComponent.Cast(activeSlot.GetController());
		if (!turretCtrl)
			turretCtrl = activeSlot.GetAttachedTurret();

		if (!turretCtrl || !turretCtrl.GetTurretComponent())
		{
			m_sDetail = "mounted compartment has no TurretControllerComponent/TurretComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			string srvDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk && srvVal[2] >= 0.5)
			{
				TurretComponent turretComp = artComp.GetResolvedTurret();
				vector limH, limV;
				vector curAim = vector.Zero;
				if (turretComp)
				{
					turretComp.GetAimingLimits(limH, limV);
					curAim = turretComp.GetAimingRotation();
				}

				run.LogTelemetry(string.Format(
					"artillery_mounted speed=%1 initYaw=%2 initPitch=%3 limH=%4 limV=%5",
					artComp.GetEffectiveInitialSpeed(), curAim[0], curAim[1], limH, limV));
				m_sDetail = string.Format("mounted turret on client+server (%1)", srvDetail);
				return EBGONE_BotBeatState.PASS;
			}
		}

		if (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_ArtilleryBotConst.QUERY_SERVER_TURRET_AIM);
		}

		return EBGONE_BotBeatState.RUNNING;
	}
}

//------------------------------------------------------------------------------------------------
//! Exercises valid in-range ballistic calculation + MP turret aiming sync on client and server.
class BGONE_BotBeat_ArtilleryAimValid : BGONE_BotBeat
{
	protected static const int SETTLE_MS = 1000;
	protected static const int SERVER_POLL_MS = 250;
	protected static const float AIM_TOLERANCE_DEG = 5.0;

	protected bool m_bStartFailed;
	protected vector m_vInitAimDeg;
	protected float m_fSolYawDeg;
	protected float m_fSolPitchDeg;
	protected float m_fReqYawDeg;
	protected float m_fReqPitchDeg;
	protected int m_iPendingSeq;
	protected int m_iLastQueryMs;
	protected float m_fLastClientYawErr;
	protected float m_fLastClientPitchErr;
	protected float m_fLastServerYawErr;
	protected float m_fLastServerPitchErr;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_ArtilleryAimValid(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Artillery computer failed to compute or sync valid in-range turret aim.");
		m_bStartFailed = false;
		m_iPendingSeq = 0;
		m_iLastQueryMs = -1000;
		m_fLastClientYawErr = 999.0;
		m_fLastClientPitchErr = 999.0;
		m_fLastServerYawErr = 999.0;
		m_fLastServerPitchErr = 999.0;
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		IEntity veh = run.FixtureEntity();
		if (!veh)
		{
			m_bStartFailed = true;
			m_sDetail = "staged artillery vehicle missing on client";
			return;
		}

		BGONE_ArtilleryComputerComponent artComp = BGONE_ArtilleryComputerComponent.Cast(veh.FindComponent(BGONE_ArtilleryComputerComponent));
		if (!artComp)
		{
			m_bStartFailed = true;
			m_sDetail = "BGONE_ArtilleryComputerComponent missing on vehicle";
			return;
		}

		TurretComponent turretComp = artComp.GetResolvedTurret();
		if (!turretComp)
		{
			m_bStartFailed = true;
			m_sDetail = "could not resolve TurretComponent";
			return;
		}

		m_vInitAimDeg = turretComp.GetAimingRotation();

		vector limitsHoriz, limitsVert;
		turretComp.GetAimingLimits(limitsHoriz, limitsVert);

		vector vehPos = veh.GetOrigin();
		vector targetPos = Vector(vehPos[0], vehPos[1] + 5.0, vehPos[2] + 400.0);

		bool okCompute = artComp.ComputeSolutionForWorldPos(targetPos, m_fSolYawDeg, m_fSolPitchDeg);
		if (!okCompute)
		{
			m_bStartFailed = true;
			m_sDetail = string.Format(
				"ComputeSolutionForWorldPos failed for valid 400m north +5m target (speed=%1 limV=%2)",
				artComp.GetEffectiveInitialSpeed(), limitsVert);
			return;
		}

		if (!BGONE_ArtilleryComputerComponent.IsFiniteFloat(m_fSolYawDeg)
			|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(m_fSolPitchDeg))
		{
			m_bStartFailed = true;
			m_sDetail = string.Format("non-finite solution yaw=%1 pitch=%2", m_fSolYawDeg, m_fSolPitchDeg);
			return;
		}

		if (limitsVert[1] > limitsVert[0])
		{
			if (m_fSolPitchDeg < limitsVert[0] - 0.01 || m_fSolPitchDeg > limitsVert[1] + 0.01)
			{
				m_bStartFailed = true;
				m_sDetail = string.Format("solution pitch=%1 outside limitsVert=%2", m_fSolPitchDeg, limitsVert);
				return;
			}
		}

		bool okReq = artComp.RequestAimAtWorldPos(targetPos, m_fReqYawDeg, m_fReqPitchDeg);
		if (!okReq
			|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(m_fReqYawDeg)
			|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(m_fReqPitchDeg))
		{
			m_bStartFailed = true;
			m_sDetail = string.Format(
				"RequestAimAtWorldPos failed (ok=%1 yaw=%2 pitch=%3)", okReq, m_fReqYawDeg, m_fReqPitchDeg);
			return;
		}

		run.LogTelemetry(string.Format(
			"artillery_valid_req speed=%1 solYaw=%2 solPitch=%3 initYaw=%4 initPitch=%5",
			artComp.GetEffectiveInitialSpeed(), m_fReqYawDeg, m_fReqPitchDeg, m_vInitAimDeg[0], m_vInitAimDeg[1]));
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		if (m_bStartFailed)
			return EBGONE_BotBeatState.FAIL;

		if (elapsedMs < SETTLE_MS)
			return EBGONE_BotBeatState.RUNNING;

		IEntity veh = run.FixtureEntity();
		if (!veh)
		{
			m_sDetail = "vehicle vanished during aim verification";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_ArtilleryComputerComponent artComp = BGONE_ArtilleryComputerComponent.Cast(veh.FindComponent(BGONE_ArtilleryComputerComponent));
		if (!artComp)
		{
			m_sDetail = "artillery component vanished";
			return EBGONE_BotBeatState.FAIL;
		}

		TurretComponent turretComp = artComp.GetResolvedTurret();
		if (!turretComp)
		{
			m_sDetail = "turret component vanished";
			return EBGONE_BotBeatState.FAIL;
		}

		vector curAim = turretComp.GetAimingRotation();
		if (!BGONE_ArtilleryComputerComponent.IsFiniteFloat(curAim[0])
			|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(curAim[1]))
		{
			m_sDetail = string.Format("client turret aiming rotation became non-finite: %1", curAim);
			return EBGONE_BotBeatState.FAIL;
		}

		m_fLastClientYawErr = Math.AbsFloat(BGONE_BotMath.AngleDelta(curAim[0], m_fReqYawDeg));
		m_fLastClientPitchErr = Math.AbsFloat(curAim[1] - m_fReqPitchDeg);

		if (m_fLastClientYawErr > AIM_TOLERANCE_DEG || m_fLastClientPitchErr > AIM_TOLERANCE_DEG)
			return EBGONE_BotBeatState.RUNNING;

		if (m_iPendingSeq > 0)
		{
			if (!run.HasAnswer(m_iPendingSeq))
				return EBGONE_BotBeatState.RUNNING;

			bool srvOk = run.AnswerOk(m_iPendingSeq);
			vector srvVal = run.AnswerValue(m_iPendingSeq);
			string srvDetail = run.AnswerDetail(m_iPendingSeq);
			m_iPendingSeq = 0;

			if (srvOk
				&& BGONE_ArtilleryComputerComponent.IsFiniteFloat(srvVal[0])
				&& BGONE_ArtilleryComputerComponent.IsFiniteFloat(srvVal[1]))
			{
				m_fLastServerYawErr = Math.AbsFloat(BGONE_BotMath.AngleDelta(srvVal[0], m_fReqYawDeg));
				m_fLastServerPitchErr = Math.AbsFloat(srvVal[1] - m_fReqPitchDeg);

				if (m_fLastServerYawErr <= AIM_TOLERANCE_DEG && m_fLastServerPitchErr <= AIM_TOLERANCE_DEG)
				{
					run.LogTelemetry(string.Format(
						"artillery_valid_pass cliYaw=%1 cliPitch=%2 srvYaw=%3 srvPitch=%4 reqYaw=%5 reqPitch=%6",
						curAim[0], curAim[1], srvVal[0], srvVal[1], m_fReqYawDeg, m_fReqPitchDeg));
					m_sDetail = string.Format(
						"client(%1,%2) and server(%3,%4) converged to target(%5,%6) | %7",
						curAim[0], curAim[1], srvVal[0], srvVal[1], m_fReqYawDeg, m_fReqPitchDeg, srvDetail);
					return EBGONE_BotBeatState.PASS;
				}
			}
		}

		if (elapsedMs - m_iLastQueryMs >= SERVER_POLL_MS)
		{
			m_iLastQueryMs = elapsedMs;
			m_iPendingSeq = run.Query(BGONE_ArtilleryBotConst.QUERY_SERVER_TURRET_AIM);
		}

		return EBGONE_BotBeatState.RUNNING;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"aim sync timed out after %1 ms (reqYaw=%2 reqPitch=%3 cliErr=(%4,%5) srvErr=(%6,%7))",
			m_iTimeoutMs, m_fReqYawDeg, m_fReqPitchDeg,
			m_fLastClientYawErr, m_fLastClientPitchErr,
			m_fLastServerYawErr, m_fLastServerPitchErr);
	}
}

//------------------------------------------------------------------------------------------------
//! Verifies out-of-range and degenerate targets are rejected cleanly with false and zero NaN corruption.
class BGONE_BotBeat_ArtilleryOutOfRange : BGONE_BotBeat
{
	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_ArtilleryOutOfRange(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Artillery computer did not cleanly refuse an out-of-range target.");
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		IEntity veh = run.FixtureEntity();
		if (!veh)
		{
			m_sDetail = "staged artillery vehicle missing";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_ArtilleryComputerComponent artComp = BGONE_ArtilleryComputerComponent.Cast(veh.FindComponent(BGONE_ArtilleryComputerComponent));
		if (!artComp)
		{
			m_sDetail = "BGONE_ArtilleryComputerComponent missing";
			return EBGONE_BotBeatState.FAIL;
		}

		float effSpeed = artComp.GetEffectiveInitialSpeed();
		float maxVacuumRange = (effSpeed * effSpeed) / BGONE_ArtilleryComputerComponent.DEFAULT_GRAVITY;
		float oorDist = 50000.0;
		if (maxVacuumRange + 5000.0 > oorDist)
			oorDist = (maxVacuumRange * 1.5) + 5000.0;

		vector vehPos = veh.GetOrigin();
		vector oorTarget = Vector(vehPos[0], vehPos[1], vehPos[2] + oorDist);

		float yawDeg;
		float pitchDeg;
		bool okCompute = artComp.ComputeSolutionForWorldPos(oorTarget, yawDeg, pitchDeg);
		if (okCompute)
		{
			m_sDetail = string.Format(
				"ComputeSolutionForWorldPos unexpectedly accepted out-of-range dist=%1 (speed=%2 yaw=%3 pitch=%4)",
				oorDist, effSpeed, yawDeg, pitchDeg);
			return EBGONE_BotBeatState.FAIL;
		}

		if (!BGONE_ArtilleryComputerComponent.IsFiniteFloat(yawDeg)
			|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(pitchDeg))
		{
			m_sDetail = string.Format("out-of-range compute produced NaN (yaw=%1 pitch=%2)", yawDeg, pitchDeg);
			return EBGONE_BotBeatState.FAIL;
		}

		bool okReq = artComp.RequestAimAtWorldPos(oorTarget, yawDeg, pitchDeg);
		if (okReq)
		{
			m_sDetail = string.Format("RequestAimAtWorldPos unexpectedly accepted out-of-range dist=%1", oorDist);
			return EBGONE_BotBeatState.FAIL;
		}

		bool okZero = artComp.ComputeSolutionForWorldPos(vehPos, yawDeg, pitchDeg);
		if (okZero)
		{
			m_sDetail = "ComputeSolutionForWorldPos unexpectedly accepted zero-distance target";
			return EBGONE_BotBeatState.FAIL;
		}

		TurretComponent turretComp = artComp.GetResolvedTurret();
		if (turretComp)
		{
			vector curAim = turretComp.GetAimingRotation();
			if (!BGONE_ArtilleryComputerComponent.IsFiniteFloat(curAim[0])
				|| !BGONE_ArtilleryComputerComponent.IsFiniteFloat(curAim[1]))
			{
				m_sDetail = string.Format("turret aiming rotation corrupted after OOR rejection: %1", curAim);
				return EBGONE_BotBeatState.FAIL;
			}
		}

		run.LogTelemetry(string.Format("artillery_oor_pass dist=%1 speed=%2 maxRange=%3", oorDist, effSpeed, maxVacuumRange));
		m_sDetail = string.Format("cleanly refused out-of-range (%1 m) and zero-range targets (no NaN)", oorDist);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	//------------------------------------------------------------------------------------------------
	override protected BGONE_BotRun BGONE_BotBuildScenario(string scenarioName)
	{
		if (scenarioName != BGONE_ArtilleryBotConst.SCENARIO_NAME)
			return super.BGONE_BotBuildScenario(scenarioName);

		BGONE_BotRun run = new BGONE_BotRun(scenarioName);
		run.m_sTitle = "BGONE Artillery Computer Ballistics & MP Turret Sync";
		run.m_sSummary = "Verified ballistic solver, turret limits, MP client+server turret aim sync, and out-of-range refusal.";

		BGONE_BotBeat_WaitDeployed beatDeploy = new BGONE_BotBeat_WaitDeployed("wait_deployed", 30000);
		run.Add(beatDeploy);

		BGONE_BotBeat_MountArtilleryTurret beatMount = new BGONE_BotBeat_MountArtilleryTurret("mount_gunner_turret", 20000);
		run.Add(beatMount);

		BGONE_BotBeat_ArtilleryAimValid beatValid = new BGONE_BotBeat_ArtilleryAimValid("artillery_valid_target", 15000);
		run.Add(beatValid);

		BGONE_BotBeat_ArtilleryOutOfRange beatOor = new BGONE_BotBeat_ArtilleryOutOfRange("artillery_out_of_range", 10000);
		run.Add(beatOor);

		return run;
	}

	//------------------------------------------------------------------------------------------------
	override protected int BGONE_BotStageScenario(string scenarioName, IEntity character, out RplId fixtureId, out vector fixturePos, out string detail)
	{
		if (scenarioName != BGONE_ArtilleryBotConst.SCENARIO_NAME)
			return super.BGONE_BotStageScenario(scenarioName, character, fixtureId, fixturePos, detail);

		fixtureId = RplId.Invalid();
		fixturePos = vector.Zero;

		BaseWorld world = GetGame().GetWorld();
		if (!world || !character)
		{
			detail = "world or character missing while staging artillery vehicle";
			return 1;
		}

		Resource vehRes = Resource.Load(BGONE_ArtilleryBotConst.PREFAB_ARTILLERY_VEHICLE);
		if (!vehRes || !vehRes.IsValid())
		{
			detail = string.Format("failed to load %1", BGONE_ArtilleryBotConst.PREFAB_ARTILLERY_VEHICLE);
			return 2;
		}

		vector charPos = character.GetOrigin();
		float spawnX = charPos[0] + 15.0;
		float spawnZ = charPos[2] - 15.0;
		float surfaceY = world.GetSurfaceY(spawnX, spawnZ);
		vector spawnPos = Vector(spawnX, surfaceY + 0.25, spawnZ);

		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		// Face hull East (+X) so a 400m North (+Z) target forces a clear -90 deg turret yaw traverse.
		Math3D.AnglesToMatrix(Vector(90.0, 0.0, 0.0), spawnParams.Transform);
		spawnParams.Transform[3] = spawnPos;

		IEntity vehicle = GetGame().SpawnEntityPrefab(vehRes, world, spawnParams);
		if (!vehicle)
		{
			detail = string.Format("SpawnEntityPrefab returned null for %1", BGONE_ArtilleryBotConst.PREFAB_ARTILLERY_VEHICLE);
			return 3;
		}

		BGONE_BotSpawnLedger.Add(vehicle, scenarioName);

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (rpl)
			fixtureId = rpl.Id();

		m_BGONE_ServerStagedEntityId = vehicle.GetID();
		fixturePos = vehicle.GetOrigin();
		detail = string.Format("spawned BGONE_QA_ArtilleryVehicle at %1", fixturePos);
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	override protected bool BGONE_BotAnswerQuery(int queryId, out int ok, out vector val, out string detail)
	{
		if (queryId != BGONE_ArtilleryBotConst.QUERY_SERVER_TURRET_AIM)
			return super.BGONE_BotAnswerQuery(queryId, ok, val, detail);

		ok = 0;
		val = vector.Zero;

		if (!m_BGONE_ServerFixture)
		{
			detail = "no staged artillery vehicle on server";
			return true;
		}

		BGONE_ArtilleryComputerComponent artComp = BGONE_ArtilleryComputerComponent.Cast(
			m_BGONE_ServerFixture.FindComponent(BGONE_ArtilleryComputerComponent));
		if (!artComp)
		{
			detail = "staged vehicle missing BGONE_ArtilleryComputerComponent on server";
			return true;
		}

		TurretComponent turretComp = artComp.GetResolvedTurret();
		if (!turretComp)
		{
			detail = "could not resolve TurretComponent on server";
			return true;
		}

		float hasOccupant = 0.0;
		IEntity ch = BGONE_BotControlledEntityServer(GetPlayerId());
		ChimeraCharacter chimera = ChimeraCharacter.Cast(ch);
		if (chimera)
		{
			CompartmentAccessComponent compAccess = chimera.GetCompartmentAccessComponent();
			if (compAccess && compAccess.IsInCompartment())
			{
				BaseCompartmentSlot slot = compAccess.GetCompartment();
				if (slot)
				{
					TurretControllerComponent slotCtrl = TurretControllerComponent.Cast(slot.GetController());
					if (!slotCtrl)
						slotCtrl = slot.GetAttachedTurret();

					if (slotCtrl && slotCtrl.GetTurretComponent())
						hasOccupant = 1.0;
				}
			}
		}

		vector srvAimDeg = turretComp.GetAimingRotation();
		ok = 1;
		val = Vector(srvAimDeg[0], srvAimDeg[1], hasOccupant);
		detail = string.Format("srvYaw=%1 srvPitch=%2 occupant=%3", srvAimDeg[0], srvAimDeg[1], hasOccupant);
		return true;
	}
}
