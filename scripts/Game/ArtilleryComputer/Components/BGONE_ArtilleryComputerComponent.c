class BGONE_ArtilleryComputerComponentClass : ScriptComponentClass
{
}

class BGONE_ArtilleryComputerComponent : ScriptComponent
{
	static const float INVALID_PITCH_DEG = -999.0;
	static const float DEFAULT_GRAVITY = 9.81;
	static const float MIN_HORIZONTAL_RANGE = 0.001;
	static const float MIN_INITIAL_SPEED = 0.001;
	static const int MAX_TURRET_SEARCH_DEPTH = 4;

	[Attribute("150", UIWidgets.EditBox, "Fallback initial projectile speed (m/s) when weapon speed cannot be read", "1 5000 1")]
	protected float m_fFallbackInitialSpeed;

	[Attribute("3.0", UIWidgets.EditBox, "Fallback muzzle height offset above entity origin (m) when muzzle transform is unavailable", "0 20 0.1")]
	protected float m_fFallbackMuzzleHeightOffset;

	protected bool m_bIsFirstTimeOpened = true;
	protected bool m_bMapListenersBound;
	protected SCR_MapEntity m_MapEntity;
	protected IEntity m_eOwner;
	protected TurretComponent m_Turret;
	protected TurretControllerComponent m_TurretController;
	protected vector m_vLimitsHoriz;
	protected vector m_vLimitsVert;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		m_eOwner = owner;
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		m_eOwner = owner;
		ResolveTurret();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		UnbindMapListeners();

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(CenterMapOnVehicle);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsFiniteFloat(float value)
	{
		return (value == value) && (value > -float.INFINITY) && (value < float.INFINITY);
	}

	//------------------------------------------------------------------------------------------------
	static float NormalizeAngleDeg(float angleDeg)
	{
		if (!IsFiniteFloat(angleDeg) || Math.AbsFloat(angleDeg) > 1000000.0)
			return 0.0;

		float wrapped = angleDeg - (Math.Floor((angleDeg + 180.0) / 360.0) * 360.0);
		if (wrapped <= -180.0)
			wrapped = wrapped + 360.0;
		else if (wrapped > 180.0)
			wrapped = wrapped - 360.0;

		return wrapped;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetWorldGravity()
	{
		if (GetGame())
		{
			IEntity worldEntity = GetGame().GetWorldEntity();
			if (worldEntity)
			{
				float g = -PhysicsWorld.GetGravity(worldEntity)[1];
				if (IsFiniteFloat(g) && g > 0.001)
					return g;
			}
		}

		return DEFAULT_GRAVITY;
	}

	//------------------------------------------------------------------------------------------------
	protected TurretControllerComponent FindTurretControllerRecursive(IEntity entity, int depth)
	{
		if (!entity || depth > MAX_TURRET_SEARCH_DEPTH)
			return null;

		TurretControllerComponent directCtrl = TurretControllerComponent.Cast(entity.FindComponent(TurretControllerComponent));
		if (directCtrl && directCtrl.GetTurretComponent())
			return directCtrl;

		BaseCompartmentManagerComponent compMgr = BaseCompartmentManagerComponent.Cast(entity.FindComponent(BaseCompartmentManagerComponent));
		if (compMgr)
		{
			array<BaseCompartmentSlot> slots = {};
			compMgr.GetCompartments(slots);
			foreach (BaseCompartmentSlot slot : slots)
			{
				if (!slot)
					continue;

				TurretControllerComponent slotCtrl = TurretControllerComponent.Cast(slot.GetController());
				if (!slotCtrl)
					slotCtrl = slot.GetAttachedTurret();

				if (slotCtrl && slotCtrl.GetTurretComponent())
					return slotCtrl;
			}
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			TurretControllerComponent childCtrl = FindTurretControllerRecursive(child, depth + 1);
			if (childCtrl)
				return childCtrl;

			child = child.GetSibling();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool ResolveTurret()
	{
		if (!m_eOwner)
			m_eOwner = GetOwner();

		if (m_TurretController)
		{
			m_Turret = m_TurretController.GetTurretComponent();
			if (m_Turret)
			{
				m_Turret.GetAimingLimits(m_vLimitsHoriz, m_vLimitsVert);
				return true;
			}
		}

		if (!m_eOwner)
			return false;

		m_TurretController = FindTurretControllerRecursive(m_eOwner, 0);
		if (!m_TurretController)
			return false;

		m_Turret = m_TurretController.GetTurretComponent();
		if (!m_Turret)
			return false;

		m_Turret.GetAimingLimits(m_vLimitsHoriz, m_vLimitsVert);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	TurretComponent GetResolvedTurret()
	{
		if (!ResolveTurret())
			return null;

		return m_Turret;
	}

	//------------------------------------------------------------------------------------------------
	TurretControllerComponent GetResolvedTurretController()
	{
		if (!ResolveTurret())
			return null;

		return m_TurretController;
	}

	//------------------------------------------------------------------------------------------------
	float GetEffectiveInitialSpeed()
	{
		if (ResolveTurret() && m_TurretController)
		{
			BaseWeaponManagerComponent weaponMgr = m_TurretController.GetWeaponManager();
			if (weaponMgr)
			{
				BaseWeaponComponent curWeapon = weaponMgr.GetCurrentWeapon();
				if (curWeapon)
				{
					float curSpeed = curWeapon.GetInitialProjectileSpeed();
					if (IsFiniteFloat(curSpeed) && curSpeed > MIN_INITIAL_SPEED)
						return curSpeed;
				}

				WeaponSlotComponent curSlot = weaponMgr.GetCurrentSlot();
				if (curSlot)
				{
					float slotSpeed = curSlot.GetInitialProjectileSpeed();
					if (IsFiniteFloat(slotSpeed) && slotSpeed > MIN_INITIAL_SPEED)
						return slotSpeed;

					IEntity weaponEnt = curSlot.GetWeaponEntity();
					if (weaponEnt)
					{
						BaseWeaponComponent entWeapon = BaseWeaponComponent.Cast(weaponEnt.FindComponent(BaseWeaponComponent));
						if (entWeapon)
						{
							float entSpeed = entWeapon.GetInitialProjectileSpeed();
							if (IsFiniteFloat(entSpeed) && entSpeed > MIN_INITIAL_SPEED)
								return entSpeed;
						}
					}
				}

				array<BaseWeaponComponent> weapons = {};
				weaponMgr.GetWeapons(weapons);
				foreach (BaseWeaponComponent weapon : weapons)
				{
					if (!weapon)
						continue;

					float wSpeed = weapon.GetInitialProjectileSpeed();
					if (IsFiniteFloat(wSpeed) && wSpeed > MIN_INITIAL_SPEED)
						return wSpeed;
				}
			}
		}

		if (IsFiniteFloat(m_fFallbackInitialSpeed) && m_fFallbackInitialSpeed > MIN_INITIAL_SPEED)
			return m_fFallbackInitialSpeed;

		return 150.0;
	}

	//------------------------------------------------------------------------------------------------
	protected vector GetLaunchPosition()
	{
		if (m_TurretController)
		{
			BaseWeaponManagerComponent weaponMgr = m_TurretController.GetWeaponManager();
			if (weaponMgr)
			{
				vector muzzleMat[4];
				if (weaponMgr.GetCurrentMuzzleTransform(muzzleMat))
					return muzzleMat[3];
			}
		}

		IEntity refEntity = m_eOwner;
		if (!refEntity)
			refEntity = GetOwner();

		if (!refEntity)
			return vector.Zero;

		return refEntity.GetOrigin() + Vector(0, m_fFallbackMuzzleHeightOffset, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindMapListeners()
	{
		if (!m_bMapListenersBound)
			return;

		m_bMapListenersBound = false;

		SCR_MapEntity mapEnt = m_MapEntity;
		if (!mapEnt)
			mapEnt = SCR_MapEntity.GetMapInstance();

		if (mapEnt)
		{
			mapEnt.GetOnMapOpen().Remove(OnMapOpen);
			mapEnt.GetOnMapClose().Remove(OnMapClose);
			mapEnt.GetOnSelection().Remove(OnMapSelection);
		}

		m_MapEntity = null;
	}

	//------------------------------------------------------------------------------------------------
	void OpenComputer(SCR_MapEntity mapEntity, IEntity user)
	{
		if (!mapEntity || !user)
			return;

		SCR_ChimeraCharacter userCharacter = SCR_ChimeraCharacter.Cast(user);
		if (!userCharacter)
			return;

		CompartmentAccessComponent compAccess = userCharacter.GetCompartmentAccessComponent();
		if (!compAccess)
			return;

		BaseCompartmentSlot compartment = compAccess.GetCompartment();
		if (!compartment)
			return;

		TurretControllerComponent turretCtrl = TurretControllerComponent.Cast(compartment.GetController());
		if (!turretCtrl)
			turretCtrl = compartment.GetAttachedTurret();

		if (!turretCtrl)
			return;

		TurretComponent turretComp = turretCtrl.GetTurretComponent();
		if (!turretComp)
			return;

		m_TurretController = turretCtrl;
		m_Turret = turretComp;
		m_Turret.GetAimingLimits(m_vLimitsHoriz, m_vLimitsVert);

		UnbindMapListeners();

		m_MapEntity = mapEntity;
		m_MapEntity.GetOnMapOpen().Insert(OnMapOpen);
		m_MapEntity.GetOnMapClose().Insert(OnMapClose);
		m_MapEntity.GetOnSelection().Insert(OnMapSelection);
		m_bMapListenersBound = true;

		if (GetGame())
		{
			MenuManager menuMgr = GetGame().GetMenuManager();
			if (menuMgr)
				menuMgr.OpenMenu(ChimeraMenuPreset.MB_ArtilleryComputer);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Exact projectile motion ballistic solver for horizontal range R > 0, elevation delta
	//! dy = targetY - launchY, and initial velocity v > 0 under gravity g.
	//! Prefers the high-arc solution and falls back to the low-arc solution if high-arc exceeds
	//! turret vertical limits. Returns false when out of range or outside vertical limits.
	bool CalculateBallisticSolution(float range, float elevationDelta, float initialVelocity, float minPitchDeg, float maxPitchDeg, out float outPitchDeg)
	{
		outPitchDeg = INVALID_PITCH_DEG;

		if (!IsFiniteFloat(range) || !IsFiniteFloat(elevationDelta) || !IsFiniteFloat(initialVelocity))
			return false;

		if (range <= MIN_HORIZONTAL_RANGE || initialVelocity <= MIN_INITIAL_SPEED)
			return false;

		float gravity = GetWorldGravity();
		float vSq = initialVelocity * initialVelocity;
		if (!IsFiniteFloat(vSq) || vSq <= 0.000001)
			return false;

		// Dimensionless formulation dividing the quadratic by v^2 to avoid float32 v^4 overflow
		// at high muzzle velocities (>= 178 m/s) while remaining algebraically exact.
		float k = (gravity * range) / vSq;
		float elevTerm = (2.0 * gravity * elevationDelta) / vSq;
		float discNorm = 1.0 - (k * k) - elevTerm;

		if (!IsFiniteFloat(k) || !IsFiniteFloat(discNorm) || k <= 0.0 || discNorm < 0.0)
			return false;

		float sqrtDiscNorm = Math.Sqrt(discNorm);
		if (!IsFiniteFloat(sqrtDiscNorm))
			return false;

		float highPitchDeg = Math.Atan2(1.0 + sqrtDiscNorm, k) * Math.RAD2DEG;

		// Algebraically identical rationalized low-arc form:
		// (1 - sqrt(discNorm)) / k = (k + 2 * dy / R) / (1 + sqrt(discNorm)),
		// avoiding catastrophic float32 cancellation when k << 1.
		float lowPitchDeg = Math.Atan2(k + (2.0 * (elevationDelta / range)), 1.0 + sqrtDiscNorm) * Math.RAD2DEG;

		bool hasVertLimits = IsFiniteFloat(minPitchDeg) && IsFiniteFloat(maxPitchDeg) && (maxPitchDeg > minPitchDeg);
		if (hasVertLimits)
		{
			if (IsFiniteFloat(highPitchDeg) && highPitchDeg >= minPitchDeg && highPitchDeg <= maxPitchDeg)
			{
				outPitchDeg = highPitchDeg;
				return true;
			}

			if (IsFiniteFloat(lowPitchDeg) && lowPitchDeg >= minPitchDeg && lowPitchDeg <= maxPitchDeg)
			{
				outPitchDeg = lowPitchDeg;
				return true;
			}

			return false;
		}

		if (IsFiniteFloat(highPitchDeg))
		{
			outPitchDeg = Math.Clamp(highPitchDeg, -89.0, 89.0);
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Computes the ballistic pitch angle in degrees (or INVALID_PITCH_DEG if out of range).
	protected float CalculatePitchAngle(float range, float elevationDelta, float initialVelocity)
	{
		float pitchDeg;
		if (!CalculateBallisticSolution(range, elevationDelta, initialVelocity, m_vLimitsVert[0], m_vLimitsVert[1], pitchDeg))
			return INVALID_PITCH_DEG;

		return pitchDeg;
	}

	//------------------------------------------------------------------------------------------------
	//! Computes clamped local turret yaw and ballistic pitch (degrees) for a world target position.
	//! Returns false when the target is out of range, degenerate, or outside vertical limits.
	bool ComputeSolutionForWorldPos(vector worldTarget, out float outYawDeg, out float outPitchDeg)
	{
		outYawDeg = 0.0;
		outPitchDeg = INVALID_PITCH_DEG;

		if (!IsFiniteFloat(worldTarget[0]) || !IsFiniteFloat(worldTarget[1]) || !IsFiniteFloat(worldTarget[2]))
			return false;

		if (!ResolveTurret() || !m_Turret)
			return false;

		vector launchPos = GetLaunchPosition();
		float range = vector.DistanceXZ(launchPos, worldTarget);
		if (!IsFiniteFloat(range) || range <= MIN_HORIZONTAL_RANGE)
			return false;

		float elevationDelta = worldTarget[1] - launchPos[1];
		float speed = GetEffectiveInitialSpeed();

		m_Turret.GetAimingLimits(m_vLimitsHoriz, m_vLimitsVert);

		if (!CalculateBallisticSolution(range, elevationDelta, speed, m_vLimitsVert[0], m_vLimitsVert[1], outPitchDeg))
			return false;

		vector aimDir = worldTarget - launchPos;
		aimDir[1] = 0.0;
		if (aimDir.LengthSq() <= 0.000001)
			return false;

		float wantedYawDeg = aimDir.ToYaw();
		IEntity baseEnt = m_eOwner;
		IEntity turretEnt = m_Turret.GetOwner();
		if (turretEnt && turretEnt.GetParent())
			baseEnt = turretEnt.GetParent();

		float baseYawDeg = 0.0;
		if (baseEnt)
			baseYawDeg = baseEnt.GetYawPitchRoll()[0];

		float yawDiffDeg = NormalizeAngleDeg(wantedYawDeg - baseYawDeg);

		bool hasHorizLimits = !m_Turret.HasMoveableBase()
			&& IsFiniteFloat(m_vLimitsHoriz[0])
			&& IsFiniteFloat(m_vLimitsHoriz[1])
			&& (m_vLimitsHoriz[1] > m_vLimitsHoriz[0])
			&& ((m_vLimitsHoriz[1] - m_vLimitsHoriz[0]) < 359.9);

		if (hasHorizLimits)
			yawDiffDeg = Math.Clamp(yawDiffDeg, m_vLimitsHoriz[0], m_vLimitsHoriz[1]);

		if (!IsFiniteFloat(yawDiffDeg) || !IsFiniteFloat(outPitchDeg))
			return false;

		outYawDeg = yawDiffDeg;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAimAnglesLocal(float yawDeg, float pitchDeg)
	{
		if (!IsFiniteFloat(yawDeg) || !IsFiniteFloat(pitchDeg))
			return;

		if (!ResolveTurret() || !m_Turret)
			return;

		m_Turret.GetAimingLimits(m_vLimitsHoriz, m_vLimitsVert);

		float clampedYawDeg = NormalizeAngleDeg(yawDeg);
		bool hasHorizLimits = !m_Turret.HasMoveableBase()
			&& IsFiniteFloat(m_vLimitsHoriz[0])
			&& IsFiniteFloat(m_vLimitsHoriz[1])
			&& (m_vLimitsHoriz[1] > m_vLimitsHoriz[0])
			&& ((m_vLimitsHoriz[1] - m_vLimitsHoriz[0]) < 359.9);

		if (hasHorizLimits)
			clampedYawDeg = Math.Clamp(clampedYawDeg, m_vLimitsHoriz[0], m_vLimitsHoriz[1]);

		float clampedPitchDeg = pitchDeg;
		if (IsFiniteFloat(m_vLimitsVert[0]) && IsFiniteFloat(m_vLimitsVert[1]) && m_vLimitsVert[1] > m_vLimitsVert[0])
			clampedPitchDeg = Math.Clamp(clampedPitchDeg, m_vLimitsVert[0], m_vLimitsVert[1]);

		vector targetRad = Vector(clampedYawDeg * Math.DEG2RAD, clampedPitchDeg * Math.DEG2RAD, 0.0);
		m_Turret.SetAimingRotationWanted(targetRad);
		m_Turret.SetAimingRotation(targetRad);
	}

	//------------------------------------------------------------------------------------------------
	//! Computes a ballistic solution for `worldTarget`, applies it locally, and syncs to authority in MP.
	bool RequestAimAtWorldPos(vector worldTarget, out float outYawDeg, out float outPitchDeg)
	{
		if (!ComputeSolutionForWorldPos(worldTarget, outYawDeg, outPitchDeg))
			return false;

		ApplyAimAnglesLocal(outYawDeg, outPitchDeg);

		if (Replication.IsServer())
		{
			Rpc(RpcDo_ApplyArtilleryAim, outYawDeg, outPitchDeg);
			return true;
		}

		if (GetGame())
		{
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (pc)
				pc.BGONE_SendArtilleryAimRequest(worldTarget);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server-only entrypoint invoked after SCR_PlayerController validates the acting turret occupant.
	void ApplyAimForServerOccupant(TurretControllerComponent occupantTurretCtrl, vector worldTarget)
	{
		if (!Replication.IsServer() || !occupantTurretCtrl)
			return;

		IEntity occupantOwner = occupantTurretCtrl.GetOwner();
		if (!occupantOwner)
			return;

		if (!m_eOwner)
			m_eOwner = GetOwner();

		if (!m_eOwner || occupantOwner.GetRootParent() != m_eOwner.GetRootParent())
			return;

		TurretComponent occupantTurret = occupantTurretCtrl.GetTurretComponent();
		if (!occupantTurret)
			return;

		m_TurretController = occupantTurretCtrl;
		m_Turret = occupantTurret;

		float srvYawDeg;
		float srvPitchDeg;
		if (!ComputeSolutionForWorldPos(worldTarget, srvYawDeg, srvPitchDeg))
			return;

		ApplyAimAnglesLocal(srvYawDeg, srvPitchDeg);
		Rpc(RpcDo_ApplyArtilleryAim, srvYawDeg, srvPitchDeg);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ApplyArtilleryAim(float yawDeg, float pitchDeg)
	{
		ApplyAimAnglesLocal(yawDeg, pitchDeg);
	}

	//------------------------------------------------------------------------------------------------
	protected void CenterMapOnVehicle()
	{
		if (!m_eOwner || !m_MapEntity)
			return;

		vector origin = m_eOwner.GetOrigin();
		int screenX, screenY;
		m_MapEntity.WorldToScreen(origin[0], origin[2], screenX, screenY);
		m_MapEntity.PanSmooth(screenX, screenY);
	}

	//------------------------------------------------------------------------------------------------
	void OnMapSelection(vector selectedPos)
	{
		if (!m_MapEntity || !m_eOwner)
			return;

		BaseWorld world = m_MapEntity.GetWorld();
		if (!world)
			return;

		float worldX, worldZ;
		m_MapEntity.ScreenToWorld(selectedPos[0], selectedPos[2], worldX, worldZ);
		float heightAtPos = world.GetSurfaceY(worldX, worldZ);
		vector worldTarget = Vector(worldX, heightAtPos, worldZ);

		float yawDeg;
		float pitchDeg;
		RequestAimAtWorldPos(worldTarget, yawDeg, pitchDeg);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapOpen(MapConfiguration config)
	{
		if (!m_bIsFirstTimeOpened)
			return;

		m_bIsFirstTimeOpened = false;

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(CenterMapOnVehicle);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnMapClose(MapConfiguration config)
	{
		UnbindMapListeners();
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	//------------------------------------------------------------------------------------------------
	void BGONE_SendArtilleryAimRequest(vector worldTarget)
	{
		if (Replication.IsServer())
		{
			RpcAsk_BGONE_ArtilleryAim(worldTarget);
			return;
		}

		Rpc(RpcAsk_BGONE_ArtilleryAim, worldTarget);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_BGONE_ArtilleryAim(vector worldTarget)
	{
		if (!Replication.IsServer() || !GetGame())
			return;

		int actingPlayerId = GetPlayerId();
		if (actingPlayerId <= 0)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		ChimeraCharacter character = ChimeraCharacter.Cast(pm.GetPlayerControlledEntity(actingPlayerId));
		if (!character)
			return;

		CompartmentAccessComponent compAccess = character.GetCompartmentAccessComponent();
		if (!compAccess || !compAccess.IsInCompartment())
			return;

		BaseCompartmentSlot compartment = compAccess.GetCompartment();
		if (!compartment)
			return;

		TurretControllerComponent turretCtrl = TurretControllerComponent.Cast(compartment.GetController());
		if (!turretCtrl)
			turretCtrl = compartment.GetAttachedTurret();

		if (!turretCtrl)
			return;

		BGONE_ArtilleryComputerComponent artComp;
		IEntity slotOwner = compartment.GetOwner();
		if (slotOwner)
			artComp = BGONE_ArtilleryComputerComponent.Cast(slotOwner.FindComponent(BGONE_ArtilleryComputerComponent));

		if (!artComp)
		{
			IEntity vehicle = compartment.GetVehicle();
			if (vehicle)
				artComp = BGONE_ArtilleryComputerComponent.Cast(vehicle.FindComponent(BGONE_ArtilleryComputerComponent));
		}

		if (!artComp && slotOwner)
		{
			IEntity rootEnt = slotOwner.GetRootParent();
			if (rootEnt)
				artComp = BGONE_ArtilleryComputerComponent.Cast(rootEnt.FindComponent(BGONE_ArtilleryComputerComponent));
		}

		if (!artComp)
			return;

		artComp.ApplyAimForServerOccupant(turretCtrl, worldTarget);
	}
}
