
[BaseContainerProps()]
class BGONE_LockType_VIS : BGONE_LockType_Base
{
	
	[Attribute("1000", UIWidgets.Slider, "Max LockOn Range", "0 10000 1", category: "BGONE")]
	protected int m_iMaxLockOnRange;
	
	[Attribute("3", UIWidgets.Slider, "Time To Aquire LockOn", "0 10 1", category: "BGONE")]
	protected int m_iLockOnTime;
	
	[Attribute("0", UIWidgets.ComboBox, "Empty = ALL. Unit type(s) to lock (Unit type is set in the workbench in the PerceivableComponent)", "", ParamEnumArray.FromEnum(EAIUnitType), category: "BGONE")]
	protected ref array<EAIUnitType> m_eUnitTypesToLock;
	
	[Attribute("0", UIWidgets.Slider, "Maintain Lock After Launching A Missile", category: "BGONE")]
	protected bool m_bKeepLockAfterFired;
	
	//[Attribute(defvalue: "{8188A13CC566D497}UI/layouts/LockOnLayout_Default.layout", category: "BGONE")]
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
	
	protected ref array<ref Shape> m_aDbgCollisionShapes;

	void BGONE_LockType_VIS()
	{
		m_eLockAudioHandle = AudioHandle.Invalid;
	}
	
	override void InitLockType(IEntity launcher)
	{
		super.InitLockType(launcher);
		m_eLockAudioHandle = AudioHandle.Invalid;
		if (!m_eSoundComponent && m_eLauncher)
			m_eSoundComponent = WeaponSoundComponent.Cast(m_eLauncher.FindComponent(WeaponSoundComponent));
		
		m_aDbgCollisionShapes = new array<ref Shape>;
		return;
	}
	
	override void StartLock()
	{
		super.StartLock();
		
		m_cTargetDataVIS = new BGONE_TargetData();
		lastTarget = null;
		lockingTarget = null;
	}
	
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if (!super.UpdateLock(timeSlice))
			return null;
		
		BaseWorld world = GetGame().GetWorld();
		float worldTime = 0;
		if (world)
			worldTime = world.GetWorldTime();
		
		lockingTarget = null;
		if (worldTime > m_fNextScanTime)
		{
			m_fNextScanTime = worldTime + m_fScanInterval;
			lockingTarget = ScanForTarget();
		}
		else 
		{
			lockingTarget = lastTarget;
		}
		
		// Lost target or none found
		if (!lockingTarget)
		{
			if (lastTarget)
				LockLost();
		}
		else
		{
			// Still locking same target
			if (lockingTarget == lastTarget)
			{
				if (m_iLockOnTime > 0)
					m_eLockingData.lockingProgress = Math.Clamp(m_eLockingData.lockingProgress + (timeSlice / m_iLockOnTime) * 100, 0, 100);
				else
					m_eLockingData.lockingProgress = 100;

				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
			
				if (m_eLockingData.lockingProgress == 100 && m_cTargetDataVIS && !m_cTargetDataVIS.targetRplId.IsValid())
				{
					m_cTargetDataVIS.targetRplId = BGONE_TargetData.ResolveEntityRplId(lockingTarget);
					LockAquired(m_eLockingData);
				}
			}
			
			// Locking new target
			if (lastTarget != lockingTarget)
			{
				m_eLockingData.lockingProgress = 0;
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				LockStartAquire(m_eLockingData);
				return m_eLockingData;
			} 
		}
		
		lastTarget = lockingTarget;
		DisplayOrUpdateLockonWidget();
		
		return m_eLockingData;
	}
	
	protected vector GetAimPoint(IEntity target)
	{
		if (!target)
			return vector.Zero;

		Physics phys = target.GetPhysics();
		vector centerOfMass = vector.Zero;
		if (phys)
			centerOfMass = phys.GetCenterOfMass();

		vector aimPoint;
		if (centerOfMass == vector.Zero)
			aimPoint = target.GetOrigin() + vector.Up;
		else
			aimPoint = target.GetOrigin() + centerOfMass;
		
		return aimPoint;
	}
	
	protected IEntity ScanForTarget()
	{
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		vector aimFrom = GetAimDirAndPosOfLauncher(m_eLauncher)[1];
		
		if (lastTarget)
		{
			IEntity currentTarget = lastTarget;
	    	vector aimTo = GetAimPoint(currentTarget);
			
		    if (vector.Distance(aimFrom, aimTo) > m_iMaxLockOnRange) 
				return null;
			
		    if (Math.Acos(vector.Dot(currentDir, vector.Direction(aimFrom, aimTo).Normalized())) > 0.3) 	// ~35 degree limit for lock seeker
				return null;
		
			float relAngle = vector.Direction(aimFrom, aimTo).ToYaw();
		
		    for (float xOff = -2.5; xOff <= 2.5; xOff += 0.5){
		        for (float yOff = -1; yOff <= 2; yOff += 0.5){
		            vector testPos = currentTarget.CoordToParent(Vector(xOff * -Math.Cos(relAngle), yOff, xOff * Math.Sin(relAngle)));
		            //Debug_DrawLineSimple(aimFrom,testPos,m_aDbgCollisionShapes);
					lastTarget = null;
					TraceLOS(aimFrom, testPos);
		            if (lastTarget)
						return lastTarget;
		        };
		    };
			return null;
		};
		
		// Check twice if we're aiming directly at a target before we go scannin'
		vector aimTo = aimFrom + currentDir * (float)m_iMaxLockOnRange;
		lastTarget = null;
		TraceLOS(aimFrom, aimTo);
		if(!lastTarget)
			TraceLOS(aimFrom, aimTo);
		if(lastTarget)
			return lastTarget;
		
		// Attempt to scan using multiple rayscasts - This is expensive (n^2) and care should be given to balance accuracy vs speed
		 for (float xOff = -4; xOff <= 4; xOff += 0.5){
		    for (float yOff = -2; yOff <= 2; yOff += 0.5){
				vector offsetVector = vector.FromYaw(xOff).VectorToAngles();
				offsetVector[1] = yOff;
				vector aimDir = currentDir.VectorToAngles() + offsetVector;
		        vector testPos = aimFrom + aimDir.AnglesToVector() * (float)m_iMaxLockOnRange;
				//Debug_DrawLineSimple(aimFrom,testPos,m_aDbgCollisionShapes);
		       	lastTarget = null;
				TraceLOS(aimFrom, testPos);
		        if (lastTarget)
					return lastTarget;
		    };
		};
		return null;
	}
	
	protected bool TraceLOS(vector from, vector to, bool excludeLockedTarget = false)
	{
		if (!m_eLauncher)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		ref array<IEntity> exclude = {m_eLauncher.GetRootParent(), lockingTarget};
		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.LayerMask = EPhysicsLayerDefs.Projectile;
		param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS; 
		if (excludeLockedTarget)
			param.ExcludeArray = exclude;
		else
			param.Exclude = m_eLauncher.GetRootParent();
			
		float percent = world.TraceMove(param, null);
		
		if (param.TraceEnt)
			CheckUnitType(param.TraceEnt);
		
		if (percent == 1)	// If trace travels the entire path, return true
			return true;
				
		return false;
	}
	
	protected bool CheckUnitType(IEntity ent)
	{
		if (!ent)
			return false;

		PerceivableComponent perceivableComp = PerceivableComponent.Cast(ent.FindComponent(PerceivableComponent));
		if (!perceivableComp)
			return false;

		if (m_eUnitTypesToLock && m_eUnitTypesToLock.Count() > 0 && !m_eUnitTypesToLock.Contains(perceivableComp.GetUnitType()))
			return false;
		
		lastTarget = ent;
		return true;
	}
	
	
	override BGONE_TargetData GetCurrentTargetData() 
	{
		// Launcher fired
		if (!m_bKeepLockAfterFired)
			GetGame().GetCallqueue().CallLater(StopLock, 10, false);
		return m_cTargetDataVIS;
	}
	
	override void StopLock()
	{
		super.StopLock();
		
		LockLost();
	}
	
	override protected void LockStartAquire(BGONE_LockingData_BASE lockingData) 
	{
		super.LockStartAquire(lockingData);
	}
	override protected void LockAquired(BGONE_LockingData_BASE lockingData) 
	{
		super.LockAquired(lockingData);
	}
	
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
			m_eLockingData.lockingPos = Vector(0,0,0);
			m_eLockingData.lockingProgress = 0;
		}
		m_cTargetDataVIS = new BGONE_TargetData();
		super.LockLost();
	}
	override void PlayLockOnAuido(float lockingProgress)
	{
		if (!m_eSoundComponent)
			return;
		
		m_eSoundComponent.SetSignalValueStr("LockingState", lockingProgress);
		if (m_eLockAudioHandle != AudioHandle.Invalid)
			m_eSoundComponent.Terminate(m_eLockAudioHandle);
		m_eLockAudioHandle = m_eSoundComponent.SoundEvent("SOUND_LOCKON_DEFAULT");
	}
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
	
	protected void DisplayOrUpdateLockonWidget()
	{
		if (!m_eLauncher || !m_eLockingData)
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
		
		//vector margins = Vector(-1,-1,-1);
		vector margins = Vector(0,0,0);
		vector offsets = Vector(0,0,0);
		if (lockingTarget)
			offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));
		vector boundsMin, boundsMax = vector.Zero;
		WorldToScreenBounds(boundsMin, boundsMax, m_wDisplay, margins, offsets);
		
		//float offsetX = 0.5 * m_wDisplay.GetWorkspace().GetWidth() - 0.5;
		//float offsetY = 0.5 * m_wDisplay.GetWorkspace().GetHeight() - 0.5;
		float offsetX = 0;
		float offsetY = 0;
		
		// Find ui positions for the search box corners. Should be done with FOV and screen res from Camera component.
		vector topLeftOffset, topRightOffset, bottomLeftOffset, bottomRightOffset;
		topLeftOffset = vector.FromYaw(-4.5).VectorToAngles();
		topLeftOffset[1] = 2;
		bottomRightOffset = vector.FromYaw(4).VectorToAngles();
		bottomRightOffset[1] = -2;
		
		array<vector> dirAndPos = GetAimDirAndPosOfLauncher(m_eLauncher);
		vector currentDir = dirAndPos[0];
		vector aimFrom = dirAndPos[1];
		
		vector tlPos, brPos;
		tlPos = aimFrom + (currentDir.VectorToAngles() + topLeftOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		brPos = aimFrom + (currentDir.VectorToAngles() + bottomRightOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		
		BaseWorld launcherWorld = m_eLauncher.GetWorld();
		int constraintLeft = displayWorkspace.ProjWorldToScreen(tlPos, launcherWorld)[0];
		int constraintTop = displayWorkspace.ProjWorldToScreen(tlPos, launcherWorld)[1];
		int constraintRight = displayWorkspace.ProjWorldToScreen(brPos, launcherWorld)[0];
		int constraintBottom = displayWorkspace.ProjWorldToScreen(brPos, launcherWorld)[1];
		
		/*
		int minX = Math.Max(Math.Lerp(0.5 - 0.075*m_wDisplay.GetWorkspace().GetWidth(), boundsMin[0] + offsetX, m_eLockingData.lockingProgress / 100), constraintLeft);
		int minY = Math.Max(Math.Lerp(0.5 - 0.075*m_wDisplay.GetWorkspace().GetHeight(), boundsMin[1] + offsetY, m_eLockingData.lockingProgress / 100), constraintTop);
		int maxX = Math.Min(Math.Lerp(0.5 - 0.075*m_wDisplay.GetWorkspace().GetWidth(), boundsMax[0] + offsetX, m_eLockingData.lockingProgress / 100), constraintRight) - (0.025 * (3 / 4) * m_wDisplay.GetWorkspace().GetHeight());
		int maxY = Math.Min(Math.Lerp(0.5 - 0.075*m_wDisplay.GetWorkspace().GetHeight(), boundsMax[1] + offsetY, m_eLockingData.lockingProgress / 100), constraintBottom) - (0.025 * m_wDisplay.GetWorkspace().GetHeight());
		*/
		
		float lerp = Math.Min(m_eLockingData.lockingProgress / 80, 1);
		float minX;
		float minY;
		float maxX;
		float maxY;
		if (lerp > 0)
		{
			minX = Math.Lerp(boundsMin[0] - 500, boundsMin[0] + offsetX, lerp);
			minY = Math.Lerp(boundsMin[1] - 500, boundsMin[1] + offsetY, lerp);
			maxX = Math.Lerp(boundsMax[0] + 500, boundsMax[0] + offsetX, lerp); // - (0.025 * (3 / 4) * m_wDisplay.GetWorkspace().GetHeight());
			maxY = Math.Lerp(boundsMax[1] + 500, boundsMax[1] + offsetY, lerp); // - (0.025 * m_wDisplay.GetWorkspace().GetHeight());
		}
		else
		{
			minX = constraintLeft;
			minY = constraintTop;
			maxX = constraintRight;
			maxY = constraintBottom;
		}
		
		
		FrameSlot.SetPos(gateTL, minX, minY);
	    FrameSlot.SetPos(gateTR, maxX, minY);
	    FrameSlot.SetPos(gateBL, minX, maxY);
	    FrameSlot.SetPos(gateBR, maxX, maxY);

		if (m_eLockingData.lockingProgress == 100)
		{
			m_wDisplay.SetColorInt(COLOR_GREEN);
			vector uiPos = displayWorkspace.ProjWorldToScreen(m_eLockingData.lockingPos, launcherWorld);
			lockCross.SetWidthOverride(46);
			lockCross.SetHeightOverride(46);
			lockCross.SetColorInt(Color.GRAY);
			vector size = FrameSlot.GetSize(lockCross);
			FrameSlot.SetPos(lockCross, uiPos[0] - size[0] / 2, uiPos[1] - size[1] / 2);
			lockCross.SetVisible(true);
		}
		else 
		{
			
			m_wDisplay.SetColorInt(Color.GRAY);
			lockCross.SetVisible(false);
		}
	}
	
	protected void WorldToScreenBounds(out vector boundsMin, out vector boundsMax, Widget widget, vector margins, vector offsets)
	{
		if(!lockingTarget)
			return;
		
		int minX = 5000;
		int minY = 5000;
		int maxX = -5000;
		int maxY = -5000;
		
		vector objectBoundsMin, objectBoundsMax;
		lockingTarget.GetBounds(objectBoundsMin, objectBoundsMax);
		
		float boundsMinX, boundsMinY, boundsMinZ, boundsMaxX, boundsMaxY, boundsMaxZ;
		boundsMinX = objectBoundsMin[0];
		boundsMinY = objectBoundsMin[1];
		boundsMinZ = objectBoundsMin[2];
		boundsMaxX = objectBoundsMax[0];
		boundsMaxY = objectBoundsMax[1];
		boundsMaxZ = objectBoundsMax[2];
		
		int offsetsX, offsetsY, offsetsZ, marginsX, marginsY, marginsZ;
		offsetsX = offsets[0];
		offsetsY = offsets[1];
		offsetsZ = offsets[2];
		marginsX = margins[0];
		marginsY = margins[1];
		marginsZ = margins[2];
		
		boundsMinX = Math.Min(boundsMinX - marginsX, 0) + offsetsX;
		boundsMinY = Math.Min(boundsMinY - marginsY, 0) + offsetsY;
		boundsMinZ = Math.Min(boundsMinZ - marginsZ, 0) + offsetsZ;
		
		boundsMaxX = Math.Max(boundsMaxX + marginsX, 0) + offsetsX;
		boundsMaxY = Math.Max(boundsMaxY + marginsY, 0) + offsetsY;
		boundsMaxZ = Math.Max(boundsMaxZ + marginsZ, 0) + offsetsZ;
		
		
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
		
		foreach(vector corner : boundsCorners) 
		{
		    vector screenPos = widget.GetWorkspace().ProjWorldToScreen(lockingTarget.CoordToParent(corner), m_eLauncher.GetWorld());
		    if (screenPos[2] > 0) 
			{
				int screenPosX = screenPos[0];
				int screenPosY = screenPos[1];
				
		        if (screenPosX < minX)
					minX = screenPosX;
		        if (screenPosX > maxX)
					maxX = screenPosX;
		        if (screenPosY < minY)
					minY = screenPosY;
		        if (screenPosY > maxY)
					maxY = screenPosY;
		    };
		}
		
		boundsMin[0] = minX;
		boundsMin[1] = minY;
		boundsMax[0] = maxX;
		boundsMax[1] = maxY;
		
	}
	
	private void Debug_DrawLineSimple(vector start, vector end, array<ref Shape> dbgShapes, int color = 0xFFFFFFFF)
	{
		vector p[2];
		p[0] = start;
		p[1] = end;

		int shapeFlags = ShapeFlags.NOOUTLINE;
		Shape s = Shape.CreateLines(color, shapeFlags, p, 2);
		dbgShapes.Insert(s);	
	}
};
