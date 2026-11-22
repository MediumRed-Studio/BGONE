
[BaseContainerProps()]
class BGONE_LockType_VIS : BGONE_LockType_Base
{
	[Attribute("1000", UIWidgets.Slider, "Max LockOn Range", "0 10000 1", category: "BGONE")]
	protected int m_iMaxLockOnRange;

	[Attribute("3", UIWidgets.Slider, "Time To Aquire LockOn", "0 10 1", category: "BGONE")]
	protected int m_iLockOnTime;

	[Attribute(uiwidget: UIWidgets.ComboBox, desc: "Empty = ALL. Unit type(s) to lock (Unit type is set in the workbench in the PerceivableComponent)", enums: ParamEnumArray.FromEnum(EAIUnitType), category: "BGONE")]
	protected ref array<EAIUnitType> m_eUnitTypesToLock;

	[Attribute("0", UIWidgets.Slider, "Maintain Lock After Launching A Missile", category: "BGONE")]
	protected bool m_bKeepLockAfterFired;

	[Attribute(defvalue: "{BF22E0769628374D}UI/layouts/BGONE_VIS_SeekBox.layout", category: "BGONE")]
	protected ResourceName m_sLockOnLayout;

	protected ref BGONE_TargetData m_cTargetDataVIS;

	protected float m_fNextScanTime = 0;
	protected float m_fScanInterval = 500;
	protected IEntity lastTarget;
	protected IEntity lockingTarget;
	protected float m_fLockAquireingDuration;
	protected WeaponSoundComponent m_eSoundComponent;
	protected AudioHandle m_eLockAudioHandle;
	protected Widget m_wDisplay;

	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aTraceExclude;
	protected ref array<ref Shape> m_aDbgCollisionShapes;

	//------------------------------------------------------------------------------------------------
	void BGONE_LockType_VIS()
	{
		m_eLockAudioHandle = AudioHandle.Invalid;
		m_TraceParam = new TraceParam();
		m_aTraceExclude = {};
	}

	//------------------------------------------------------------------------------------------------
	override void InitLockType(IEntity launcher)
	{
		super.InitLockType(launcher);
		m_eLockAudioHandle = AudioHandle.Invalid;
		if (!m_eSoundComponent && m_eLauncher)
			m_eSoundComponent = WeaponSoundComponent.Cast(m_eLauncher.FindComponent(WeaponSoundComponent));

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};

		m_aDbgCollisionShapes = new array<ref Shape>();
	}

	//------------------------------------------------------------------------------------------------
	override void StartLock()
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(StopLock);

		super.StartLock();

		m_cTargetDataVIS = new BGONE_TargetData();
		lastTarget = null;
		lockingTarget = null;
		m_fNextScanTime = 0;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if (!super.UpdateLock(timeSlice))
			return null;

		BaseWorld world = GetGame().GetWorld();
		float worldTime = 0;
		if (world)
			worldTime = world.GetWorldTime();

		IEntity previousTarget = lastTarget;
		lockingTarget = null;

		if (worldTime >= m_fNextScanTime)
		{
			m_fNextScanTime = worldTime + m_fScanInterval;
			lockingTarget = ScanForTarget(previousTarget);
		}
		else
		{
			lockingTarget = previousTarget;
		}

		// Lost target or none found.
		if (!lockingTarget)
		{
			if (previousTarget)
				LockLost();
		}
		else
		{
			if (lockingTarget == previousTarget)
			{
				if (m_iLockOnTime > 0)
					m_eLockingData.lockingProgress = Math.Clamp(m_eLockingData.lockingProgress + (timeSlice / m_iLockOnTime) * 100.0, 0.0, 100.0);
				else
					m_eLockingData.lockingProgress = 100.0;

				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);

				if (m_eLockingData.lockingProgress >= 100.0 && m_cTargetDataVIS && !m_cTargetDataVIS.targetRplId.IsValid())
				{
					m_cTargetDataVIS.targetRplId = BGONE_TargetData.ResolveEntityRplId(lockingTarget);
					LockAquired(m_eLockingData);
				}
			}
			else
			{
				// Newly acquired or switched target: clear any stale RplId from the previous target.
				m_cTargetDataVIS = new BGONE_TargetData();
				m_eLockingData.lockingProgress = 0.0;
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				LockStartAquire(m_eLockingData);
			}
		}

		lastTarget = lockingTarget;
		DisplayOrUpdateLockonWidget();

		return m_eLockingData;
	}

	//------------------------------------------------------------------------------------------------
	protected vector GetAimPoint(IEntity target)
	{
		if (!target)
			return vector.Zero;

		Physics phys = target.GetPhysics();
		vector centerOfMass = vector.Zero;
		if (phys)
			centerOfMass = phys.GetCenterOfMass();

		if (centerOfMass == vector.Zero)
			return target.GetOrigin() + vector.Up;

		return target.CoordToParent(centerOfMass);
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity ScanForTarget(IEntity previousTarget)
	{
		array<vector> dirAndPos = GetAimDirAndPosOfLauncher(m_eLauncher);
		if (!dirAndPos || dirAndPos.Count() < 2)
			return null;

		vector currentDir = dirAndPos[0];
		vector aimFrom = dirAndPos[1];
		if (currentDir.LengthSq() < 0.0001)
			return null;

		currentDir.Normalize();

		// 1. Direct centerline trace first (fast 1-trace path and clean target switching).
		vector aimTo = aimFrom + currentDir * (float)m_iMaxLockOnRange;
		IEntity directHit = TraceLOS(aimFrom, aimTo);
		if (directHit)
			return directHit;

		// 1b. Tight 0.5 deg micro-cross around centerline so near-center targets switch cleanly
		// even if previousTarget is still within the 0.3 rad tracking cone.
		vector baseAngles = currentDir.VectorToAngles();
		float microYaw[4] = {-0.5, 0.5, 0.0, 0.0};
		float microPitch[4] = {0.0, 0.0, -0.5, 0.5};
		for (int m = 0; m < 4; m++)
		{
			vector microAngles = Vector(baseAngles[0] + microYaw[m], Math.Clamp(baseAngles[1] + microPitch[m], -89.0, 89.0), 0.0);
			vector microTo = aimFrom + microAngles.AnglesToVector() * (float)m_iMaxLockOnRange;
			IEntity microHit = TraceLOS(aimFrom, microTo);
			if (microHit)
				return microHit;
		}

		// 2. Track previousTarget within range and seeker cone (~17 deg / 0.3 rad).
		if (previousTarget)
		{
			vector prevAimTo = GetAimPoint(previousTarget);
			float distToPrev = vector.Distance(aimFrom, prevAimTo);
			if (distToPrev <= m_iMaxLockOnRange && distToPrev > 0.01)
			{
				vector toPrevDir = vector.Direction(aimFrom, prevAimTo).Normalized();
				float dot = Math.Clamp(vector.Dot(currentDir, toPrevDir), -1.0, 1.0);
				if (Math.Acos(dot) <= 0.3)
				{
					float relAngleRad = toPrevDir.ToYaw() * Math.DEG2RAD;
					float cosAngle = Math.Cos(relAngleRad);
					float sinAngle = Math.Sin(relAngleRad);

					for (float xOff = -2.5; xOff <= 2.5; xOff += 0.5)
					{
						for (float yOff = -1.0; yOff <= 2.0; yOff += 0.5)
						{
							vector testPos = previousTarget.CoordToParent(Vector(xOff * -cosAngle, yOff, xOff * sinAngle));
							IEntity prevHit = TraceLOS(aimFrom, testPos);
							if (prevHit)
								return prevHit;
						}
					}

					return null;
				}
			}
		}

		// 3. Fallback grid scan around currentDir.
		for (float gx = -4.0; gx <= 4.0; gx += 0.5)
		{
			for (float gy = -2.0; gy <= 2.0; gy += 0.5)
			{
				vector scanAngles = Vector(baseAngles[0] + gx, Math.Clamp(baseAngles[1] + gy, -89.0, 89.0), 0.0);
				vector testPos = aimFrom + scanAngles.AnglesToVector() * (float)m_iMaxLockOnRange;
				IEntity gridHit = TraceLOS(aimFrom, testPos);
				if (gridHit)
					return gridHit;
			}
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity TraceLOS(vector from, vector to)
	{
		if (!m_eLauncher)
			return null;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};

		m_aTraceExclude.Clear();
		m_aTraceExclude.Insert(m_eLauncher);
		IEntity rootParent = m_eLauncher.GetRootParent();
		if (rootParent && rootParent != m_eLauncher)
			m_aTraceExclude.Insert(rootParent);

		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		m_TraceParam.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.Exclude = null;
		m_TraceParam.ExcludeArray = m_aTraceExclude;
		m_TraceParam.TraceEnt = null;

		world.TraceMove(m_TraceParam, null);
		if (!m_TraceParam.TraceEnt)
			return null;

		return CheckUnitType(m_TraceParam.TraceEnt);
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity CheckUnitType(IEntity ent)
	{
		if (!ent)
			return null;

		// Walk up parent hierarchy so hitting a vehicle wheel/turret/door sub-entity resolves the vehicle.
		IEntity cur = ent;
		while (cur)
		{
			PerceivableComponent perceivableComp = PerceivableComponent.Cast(cur.FindComponent(PerceivableComponent));
			if (perceivableComp)
			{
				bool typeAllowed = (!m_eUnitTypesToLock || m_eUnitTypesToLock.IsEmpty() || m_eUnitTypesToLock.Contains(perceivableComp.GetUnitType()));
				if (typeAllowed && BGONE_TargetData.ResolveEntityRplId(cur).IsValid())
					return cur;
			}

			cur = cur.GetParent();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData GetCurrentTargetData()
	{
		// Launcher fired
		if (!m_bKeepLockAfterFired)
		{
			ScriptCallQueue queue = GetGame().GetCallqueue();
			if (queue)
			{
				queue.Remove(StopLock);
				queue.CallLater(StopLock, 10, false);
			}
		}

		return m_cTargetDataVIS;
	}

	//------------------------------------------------------------------------------------------------
	override void StopLock()
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(StopLock);

		super.StopLock();
		LockLost();
	}

	//------------------------------------------------------------------------------------------------
	override void OnCleanup()
	{
		StopLock();
		TerminateLockOnAudio();
	}

	//------------------------------------------------------------------------------------------------
	override IEntity GetLockedTargetEntity()
	{
		return lastTarget;
	}

	//------------------------------------------------------------------------------------------------
	RplId GetLockedTargetRplId()
	{
		if (!m_cTargetDataVIS)
			return RplId.Invalid();

		return m_cTargetDataVIS.targetRplId;
	}

	//------------------------------------------------------------------------------------------------
	override protected void LockStartAquire(BGONE_LockingData_BASE lockingData)
	{
		super.LockStartAquire(lockingData);
	}

	//------------------------------------------------------------------------------------------------
	override protected void LockAquired(BGONE_LockingData_BASE lockingData)
	{
		super.LockAquired(lockingData);
	}

	//------------------------------------------------------------------------------------------------
	override protected void LockLost()
	{
		if (m_wDisplay)
		{
			m_wDisplay.RemoveFromHierarchy();
			m_wDisplay = null;
		}
		lockingTarget = null;
		lastTarget = null;
		if (m_eLockingData)
		{
			m_eLockingData.lockingPos = vector.Zero;
			m_eLockingData.lockingProgress = 0.0;
		}
		m_cTargetDataVIS = new BGONE_TargetData();
		super.LockLost();
	}

	//------------------------------------------------------------------------------------------------
	override void PlayLockOnAuido(float lockingProgress)
	{
		if (!m_eSoundComponent)
			return;

		m_eSoundComponent.SetSignalValueStr("LockingState", lockingProgress);
		if (m_eLockAudioHandle != AudioHandle.Invalid)
			m_eSoundComponent.Terminate(m_eLockAudioHandle);
		m_eLockAudioHandle = m_eSoundComponent.SoundEvent("SOUND_LOCKON_DEFAULT");
	}

	//------------------------------------------------------------------------------------------------
	override void TerminateLockOnAudio()
	{
		if (!m_eSoundComponent)
			return;

		if (m_eLockAudioHandle != AudioHandle.Invalid)
		{
			m_eSoundComponent.Terminate(m_eLockAudioHandle);
			m_eLockAudioHandle = AudioHandle.Invalid;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void DisplayOrUpdateLockonWidget()
	{
		if (!m_eLauncher || !m_eLockingData)
			return;

		BaseWorld launcherWorld = m_eLauncher.GetWorld();
		if (!launcherWorld)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		if (!m_wDisplay)
			m_wDisplay = workspace.CreateWidgets(m_sLockOnLayout);
		if (!m_wDisplay)
			return;

		WorkspaceWidget displayWorkspace = m_wDisplay.GetWorkspace();
		if (!displayWorkspace)
			return;

		Widget gateTL = m_wDisplay.FindWidget("TL");
		Widget gateTR = m_wDisplay.FindWidget("TR");
		Widget gateBL = m_wDisplay.FindWidget("BL");
		Widget gateBR = m_wDisplay.FindWidget("BR");
		SizeLayoutWidget lockCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));
		if (!gateTL || !gateTR || !gateBL || !gateBR || !lockCross)
			return;

		vector margins = vector.Zero;
		vector offsets = vector.Zero;
		if (lockingTarget)
			offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));

		vector boundsMin = vector.Zero;
		vector boundsMax = vector.Zero;
		bool hasTargetBounds = false;
		if (lockingTarget)
			hasTargetBounds = WorldToScreenBounds(boundsMin, boundsMax, displayWorkspace, launcherWorld, margins, offsets);

		// Compute search-box corner positions in reference resolution (ProjWorldToScreen already returns reference space).
		array<vector> dirAndPos = GetAimDirAndPosOfLauncher(m_eLauncher);
		bool hasConstraints = false;
		vector tlScreen = vector.Zero;
		vector brScreen = vector.Zero;
		if (dirAndPos && dirAndPos.Count() >= 2 && dirAndPos[0].LengthSq() > 0.0001)
		{
			vector currentDir = dirAndPos[0].Normalized();
			vector aimFrom = dirAndPos[1];
			vector baseAngles = currentDir.VectorToAngles();

			vector tlAngles = Vector(baseAngles[0] - 4.5, Math.Clamp(baseAngles[1] + 2.0, -89.0, 89.0), 0.0);
			vector brAngles = Vector(baseAngles[0] + 4.0, Math.Clamp(baseAngles[1] - 2.0, -89.0, 89.0), 0.0);

			vector tlPos = aimFrom + tlAngles.AnglesToVector() * (float)m_iMaxLockOnRange;
			vector brPos = aimFrom + brAngles.AnglesToVector() * (float)m_iMaxLockOnRange;

			tlScreen = displayWorkspace.ProjWorldToScreen(tlPos, launcherWorld);
			brScreen = displayWorkspace.ProjWorldToScreen(brPos, launcherWorld);
			hasConstraints = (tlScreen[2] > 0.0 && brScreen[2] > 0.0);
		}

		float lerp = Math.Min(m_eLockingData.lockingProgress / 80.0, 1.0);
		bool showGates = false;
		float minX = 0.0;
		float minY = 0.0;
		float maxX = 0.0;
		float maxY = 0.0;

		if (lerp > 0.0 && hasTargetBounds)
		{
			minX = Math.Lerp(boundsMin[0] - 500.0, boundsMin[0], lerp);
			minY = Math.Lerp(boundsMin[1] - 500.0, boundsMin[1], lerp);
			maxX = Math.Lerp(boundsMax[0] + 500.0, boundsMax[0], lerp);
			maxY = Math.Lerp(boundsMax[1] + 500.0, boundsMax[1], lerp);
			showGates = true;
		}
		else if (hasConstraints)
		{
			minX = tlScreen[0];
			minY = tlScreen[1];
			maxX = brScreen[0];
			maxY = brScreen[1];
			showGates = true;
		}

		gateTL.SetVisible(showGates);
		gateTR.SetVisible(showGates);
		gateBL.SetVisible(showGates);
		gateBR.SetVisible(showGates);

		if (showGates)
		{
			FrameSlot.SetPos(gateTL, minX, minY);
			FrameSlot.SetPos(gateTR, maxX, minY);
			FrameSlot.SetPos(gateBL, minX, maxY);
			FrameSlot.SetPos(gateBR, maxX, maxY);
		}

		if (m_eLockingData.lockingProgress >= 100.0)
		{
			m_wDisplay.SetColorInt(COLOR_GREEN);
			vector uiPos = displayWorkspace.ProjWorldToScreen(m_eLockingData.lockingPos, launcherWorld);
			if (uiPos[2] > 0.0)
			{
				lockCross.SetWidthOverride(46);
				lockCross.SetHeightOverride(46);
				FrameSlot.SetSize(lockCross, 46, 46);
				lockCross.SetColorInt(Color.GRAY);
				FrameSlot.SetPos(lockCross, uiPos[0] - 23.0, uiPos[1] - 23.0);
				lockCross.SetVisible(true);
			}
			else
			{
				lockCross.SetVisible(false);
			}
		}
		else
		{
			m_wDisplay.SetColorInt(Color.GRAY);
			lockCross.SetVisible(false);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool WorldToScreenBounds(
		out vector boundsMin,
		out vector boundsMax,
		WorkspaceWidget displayWorkspace,
		BaseWorld launcherWorld,
		vector margins,
		vector offsets)
	{
		boundsMin = vector.Zero;
		boundsMax = vector.Zero;

		if (!lockingTarget || !displayWorkspace || !launcherWorld)
			return false;

		vector objectBoundsMin;
		vector objectBoundsMax;
		lockingTarget.GetBounds(objectBoundsMin, objectBoundsMax);

		float offsetsX = offsets[0];
		float offsetsY = offsets[1];
		float offsetsZ = offsets[2];
		float marginsX = margins[0];
		float marginsY = margins[1];
		float marginsZ = margins[2];

		float boundsMinX = Math.Min(objectBoundsMin[0] - marginsX, 0.0) + offsetsX;
		float boundsMinY = Math.Min(objectBoundsMin[1] - marginsY, 0.0) + offsetsY;
		float boundsMinZ = Math.Min(objectBoundsMin[2] - marginsZ, 0.0) + offsetsZ;

		float boundsMaxX = Math.Max(objectBoundsMax[0] + marginsX, 0.0) + offsetsX;
		float boundsMaxY = Math.Max(objectBoundsMax[1] + marginsY, 0.0) + offsetsY;
		float boundsMaxZ = Math.Max(objectBoundsMax[2] + marginsZ, 0.0) + offsetsZ;

		vector boundsCorners[8] = {
			Vector(boundsMinX, boundsMinY, boundsMinZ),
			Vector(boundsMinX, boundsMinY, boundsMaxZ),
			Vector(boundsMinX, boundsMaxY, boundsMinZ),
			Vector(boundsMinX, boundsMaxY, boundsMaxZ),
			Vector(boundsMaxX, boundsMinY, boundsMinZ),
			Vector(boundsMaxX, boundsMinY, boundsMaxZ),
			Vector(boundsMaxX, boundsMaxY, boundsMinZ),
			Vector(boundsMaxX, boundsMaxY, boundsMaxZ)
		};

		bool anyVisible = false;
		float minX = 0.0;
		float minY = 0.0;
		float maxX = 0.0;
		float maxY = 0.0;

		foreach (vector corner : boundsCorners)
		{
			vector screenPos = displayWorkspace.ProjWorldToScreen(lockingTarget.CoordToParent(corner), launcherWorld);
			if (screenPos[2] > 0.0)
			{
				float screenPosX = screenPos[0];
				float screenPosY = screenPos[1];

				if (!anyVisible)
				{
					minX = screenPosX;
					maxX = screenPosX;
					minY = screenPosY;
					maxY = screenPosY;
					anyVisible = true;
				}
				else
				{
					if (screenPosX < minX)
						minX = screenPosX;
					if (screenPosX > maxX)
						maxX = screenPosX;
					if (screenPosY < minY)
						minY = screenPosY;
					if (screenPosY > maxY)
						maxY = screenPosY;
				}
			}
		}

		if (!anyVisible)
			return false;

		boundsMin[0] = minX;
		boundsMin[1] = minY;
		boundsMax[0] = maxX;
		boundsMax[1] = maxY;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	private void Debug_DrawLineSimple(vector start, vector end, array<ref Shape> dbgShapes, int color = 0xFFFFFFFF)
	{
		if (!dbgShapes)
			return;

		vector p[2];
		p[0] = start;
		p[1] = end;

		int shapeFlags = ShapeFlags.NOOUTLINE;
		Shape s = Shape.CreateLines(color, shapeFlags, p, 2);
		dbgShapes.Insert(s);
	}
};
