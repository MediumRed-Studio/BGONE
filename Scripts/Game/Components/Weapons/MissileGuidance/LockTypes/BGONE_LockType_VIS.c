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
	
	[Attribute(defvalue: "{BF22E0769628374D}UI/layouts/BGONE_VIS_SeekBox.layout", category: "BGONE")]
	protected ResourceName m_sLockOnLayout;
	
	protected ref BGONE_TargetData m_cTargetDataVIS;
	
	protected float m_fNextScanTime = 0;
	protected float m_fScanInterval = 500;
	protected IEntity lastTarget;
	protected IEntity lockingTarget;
	protected float m_fLockAquireingDuration;
	protected SoundComponent m_eSoundComponent;
	protected AudioHandle m_eLockAudioHandle;
	protected ref Widget m_wDisplay;
	protected bool m_bLockAcquiredInvoked = false;
	protected bool m_bLockAcquiringInvoked = false;
	
	protected ref array<ref Shape> m_aDbgCollisionShapes;
	
	override void InitLockType(IEntity launcher)
	{
		super.InitLockType(launcher);
		if(!m_eSoundComponent && m_eLauncher)
		{
			m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(WeaponSoundComponent));
			if(!m_eSoundComponent)
				m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(SoundComponent));
		}
		
		m_aDbgCollisionShapes = new array<ref Shape>;
	}
	
	override void StartLock()
	{
		super.StartLock();
		
		m_cTargetDataVIS = new BGONE_TargetData();
		lastTarget = null;
		lockingTarget = null;
		m_bLockAcquiredInvoked = false;
		m_bLockAcquiringInvoked = false;
	}
	
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if(!super.UpdateLock(timeSlice))
			return null;
		
		lockingTarget = null;
		if(GetGame().GetWorld().GetWorldTime() > m_fNextScanTime)
		{
			m_fNextScanTime = GetGame().GetWorld().GetWorldTime() + m_fScanInterval;
			lockingTarget = ScanForTarget();
		}
		else 
		{
			lockingTarget = lastTarget;
		}
		
		// Lost target or none found
		if(!lockingTarget)
		{
			if(lastTarget)
				LockLost();
		}
		else 
		{
			// Still locking same target
			if(lockingTarget == lastTarget)
			{
				m_eLockingData.lockingProgress = Math.Clamp(m_eLockingData.lockingProgress + (timeSlice / Math.Max(m_iLockOnTime, 0.001)) * 100, 0, 100);
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
			
				if(m_eLockingData.lockingProgress == 100 && !m_bLockAcquiredInvoked)
				{
					RplComponent rpl = RplComponent.Cast(lockingTarget.FindComponent(RplComponent));
					if(rpl)
						m_cTargetDataVIS.targetRplId = rpl.Id();
					else
						m_cTargetDataVIS.targetRplId = Replication.FindId(lockingTarget);
					
					m_bLockAcquiredInvoked = true;
					LockAquired(m_eLockingData);
				}
			}
			
			// Locking new target
			if(lastTarget != lockingTarget)
			{
				m_eLockingData.lockingProgress = 0;
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				m_bLockAcquiredInvoked = false;
				if(!m_bLockAcquiringInvoked)
				{
					m_bLockAcquiringInvoked = true;
					LockStartAquire(m_eLockingData);
				}
				lastTarget = lockingTarget;
				DisplayOrUpdateLockonWidget();
				return m_eLockingData;
			} 
		}
		
		lastTarget = lockingTarget;
		DisplayOrUpdateLockonWidget();
		
		return m_eLockingData;
	}
	
	protected vector GetAimPoint(IEntity target)
	{
		if(!target)
			return vector.Zero;
			
		Physics phys = target.GetPhysics();
		if(!phys)
			return target.GetOrigin() + vector.Up;
			
		vector centerOfMass = phys.GetCenterOfMass();
		if(centerOfMass == vector.Zero)
			return target.GetOrigin() + vector.Up;
		
		return target.CoordToParent(centerOfMass);
	}
	
	protected IEntity ScanForTarget()
	{
		if(!m_eLauncher)
			return null;
			
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		vector aimFrom = GetAimDirAndPosOfLauncher(m_eLauncher)[1];
		
		if (lastTarget)
		{
			IEntity currentTarget = lastTarget;
	    	vector aimTo = GetAimPoint(currentTarget);
			
		    if (vector.Distance(aimFrom, aimTo) > m_iMaxLockOnRange) 
				return null;
			
			vector dirToTarget = vector.Direction(aimFrom, aimTo);
			if (dirToTarget.Length() > 0.001)
			{
				if (Math.Acos(vector.Dot(currentDir, dirToTarget.Normalized())) > 0.3) 	// ~35 degree limit for lock seeker
					return null;
			}
		
			float relAngle = dirToTarget.ToYaw();
		
		    for (float xOff = -2.5; xOff <= 2.5; xOff += 0.5){
		        for (float yOff = -1; yOff <= 2; yOff += 0.5){
		            vector testPos = currentTarget.CoordToParent(Vector(xOff * -Math.Cos(relAngle), yOff, xOff * Math.Sin(relAngle)));
					lastTarget = null;
					TraceLOS(aimFrom, testPos);
		            if (lastTarget)
						return lastTarget;
		        }
		    }
			return null;
		}
		
		// Check directly at target
		vector aimTo = aimFrom + currentDir * (float)m_iMaxLockOnRange;
		lastTarget = null;
		TraceLOS(aimFrom, aimTo);
		if(!lastTarget)
			TraceLOS(aimFrom, aimTo);
		if(lastTarget)
			return lastTarget;
		
		// Multi-ray scan
		for (float xOff = -4; xOff <= 4; xOff += 0.5){
		    for (float yOff = -2; yOff <= 2; yOff += 0.5){
				vector offsetVector = vector.FromYaw(xOff).VectorToAngles();
				offsetVector[1] = yOff;
				vector aimDir = currentDir.VectorToAngles() + offsetVector;
		        vector testPos = aimFrom + aimDir.AnglesToVector() * (float)m_iMaxLockOnRange;
		       	lastTarget = null;
				TraceLOS(aimFrom, testPos);
		        if (lastTarget)
					return lastTarget;
		    }
		}
		return null;
	}
	
	protected bool TraceLOS(vector from, vector to, bool excludeLockedTarget = false)
	{	
		if(!m_eLauncher)
			return false;
			
		IEntity rootLauncher = m_eLauncher.GetRootParent();
		if(!rootLauncher)
			rootLauncher = m_eLauncher;
					
		ref array<IEntity> exclude = {rootLauncher, lockingTarget };
		TraceParam param = new TraceParam;
		param.Start = from;
		param.End = to;
		param.LayerMask = EPhysicsLayerDefs.Projectile;
		param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS; 
		if(excludeLockedTarget)
			param.ExcludeArray = exclude;
		else
			param.Exclude = rootLauncher;
			
		World world = GetGame().GetWorld();
		if(!world)
			return false;
			
		float percent = world.TraceMove(param, null);
		
		if(param.TraceEnt)
			CheckUnitType(param.TraceEnt);
		
		if (percent == 1)
			return true;
				
		return false;
	}
	
	protected bool CheckUnitType(IEntity ent)
	{
		if(!ent)
			return false;
			
		IEntity rootEnt = ent.GetRootParent();
		if(!rootEnt)
			rootEnt = ent;
		
		PerceivableComponent perceivableComp = PerceivableComponent.Cast(rootEnt.FindComponent(PerceivableComponent));
		if (!perceivableComp)
			return false;

		if(m_eUnitTypesToLock && m_eUnitTypesToLock.Count() > 0 && !m_eUnitTypesToLock.Contains(perceivableComp.GetUnitType()))
			return false;
		
		lastTarget = rootEnt;
		return true;
	}
	
	override BGONE_TargetData GetCurrentTargetData() 
	{
		if(!m_bKeepLockAfterFired)
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
		if(m_wDisplay)
		{
			m_wDisplay.RemoveFromHierarchy();
			delete m_wDisplay;
			m_wDisplay = null;
		}
		
		lockingTarget = null;
		lastTarget = null;
		m_bLockAcquiredInvoked = false;
		m_bLockAcquiringInvoked = false;
		
		if(m_eLockingData)
		{
			m_eLockingData.lockingPos = Vector(0,0,0);
			m_eLockingData.lockingProgress = 0;
		}
		m_cTargetDataVIS = new BGONE_TargetData();
		super.LockLost();
	}
	
	override void PlayLockOnAudio(float lockingProgress)
	{
		if(!m_eSoundComponent && m_eLauncher)
		{
			m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(WeaponSoundComponent));
			if(!m_eSoundComponent)
				m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(SoundComponent));
		}
		
		if(!m_eSoundComponent)
			return;
		
		m_eSoundComponent.SetSignalValueStr("LockingState", lockingProgress);
		m_eSoundComponent.Terminate(m_eLockAudioHandle);
		m_eLockAudioHandle = m_eSoundComponent.SoundEvent("SOUND_LOCKON_DEFAULT");
	}
	
	override void PlayLockOnAuido(float lockingProgress)
	{
		PlayLockOnAudio(lockingProgress);
	}
	
	override void TerminateLockOnAudio()
	{
		if(!m_eSoundComponent)
			return;
		
		m_eSoundComponent.Terminate(m_eLockAudioHandle);
	}
	
	protected void DisplayOrUpdateLockonWidget()
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if(!workspace)
			return;
			
		if(!m_wDisplay)
			m_wDisplay = workspace.CreateWidgets(m_sLockOnLayout);
			
		if(!m_wDisplay)
			return;
		
		Widget gateTL = m_wDisplay.FindWidget("TL");
		Widget gateTR = m_wDisplay.FindWidget("TR");
		Widget gateBL = m_wDisplay.FindWidget("BL");
		Widget gateBR = m_wDisplay.FindWidget("BR");
		Widget seekBox = m_wDisplay.FindAnyWidget("SeekBox");
		SizeLayoutWidget lockCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));
		
		vector margins = Vector(0,0,0);
		vector offsets = Vector(0,0,0);
		if(lockingTarget)
			offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));
		vector boundsMin, boundsMax = vector.Zero;
		WorldToScreenBounds(boundsMin, boundsMax, m_wDisplay, margins, offsets);
		
		float offsetX = 0;
		float offsetY = 0;
		
		vector topLeftOffset, bottomRightOffset;
		topLeftOffset = vector.FromYaw(-4.5).VectorToAngles();
		topLeftOffset[1] = 2;
		bottomRightOffset = vector.FromYaw(4).VectorToAngles();
		bottomRightOffset[1] = -2;
		
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		vector aimFrom = GetAimDirAndPosOfLauncher(m_eLauncher)[1];
		
		vector tlPos, brPos;
		tlPos = aimFrom + (currentDir.VectorToAngles() + topLeftOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		brPos = aimFrom + (currentDir.VectorToAngles() + bottomRightOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		
		vector screenTL = workspace.ProjWorldToScreen(tlPos, m_eLauncher.GetWorld());
		vector screenBR = workspace.ProjWorldToScreen(brPos, m_eLauncher.GetWorld());
		
		float constraintLeft = workspace.DPIUnscale(screenTL[0]);
		float constraintTop = workspace.DPIUnscale(screenTL[1]);
		float constraintRight = workspace.DPIUnscale(screenBR[0]);
		float constraintBottom = workspace.DPIUnscale(screenBR[1]);
		
		float lerp = Math.Min(m_eLockingData.lockingProgress / 80, 1);
		float minX, minY, maxX, maxY;
		
		if(lerp > 0)
		{
			minX = Math.Lerp(boundsMin[0] - 500, boundsMin[0] + offsetX, lerp);
			minY = Math.Lerp(boundsMin[1] - 500, boundsMin[1] + offsetY, lerp);
			maxX = Math.Lerp(boundsMax[0] + 500, boundsMax[0] + offsetX, lerp);
			maxY = Math.Lerp(boundsMax[1] + 500, boundsMax[1] + offsetY, lerp);
		}
		else
		{
			minX = constraintLeft;
			minY = constraintTop;
			maxX = constraintRight;
			maxY = constraintBottom;
		}
		
		if(seekBox)
		{
			FrameSlot.SetPos(seekBox, minX, minY);
			FrameSlot.SetSize(seekBox, Math.Max(maxX - minX, 10), Math.Max(maxY - minY, 10));
		}
		else
		{
			if(gateTL) FrameSlot.SetPos(gateTL, minX, minY);
			if(gateTR) FrameSlot.SetPos(gateTR, maxX, minY);
			if(gateBL) FrameSlot.SetPos(gateBL, minX, maxY);
			if(gateBR) FrameSlot.SetPos(gateBR, maxX, maxY);
		}

		if(m_eLockingData.lockingProgress == 100)
		{
			if(seekBox)
				seekBox.SetColorInt(Color.GREEN);
			else
				m_wDisplay.SetColorInt(Color.GREEN);
				
			if(lockCross)
			{
				vector uiPos = workspace.ProjWorldToScreen(m_eLockingData.lockingPos, m_eLauncher.GetWorld());
				lockCross.SetWidthOverride(46);
				lockCross.SetHeightOverride(46);
				lockCross.SetColorInt(Color.GRAY);
			}
		}
		else
		{
			if(seekBox)
				seekBox.SetColorInt(Color.WHITE);
			else
				m_wDisplay.SetColorInt(Color.WHITE);
				
			if(lockCross)
			{
				lockCross.SetWidthOverride(1920);
				lockCross.SetHeightOverride(1080);
				lockCross.SetColorInt(Color.WHITE);
			}
		}
	}
	
	protected void WorldToScreenBounds(out vector boundsMin, out vector boundsMax, Widget widget, vector margins, vector offsets)
	{
		if(!lockingTarget || !widget)
			return;
		
		WorkspaceWidget workspace = widget.GetWorkspace();
		if(!workspace)
			return;
			
		float minX = 50000;
		float minY = 50000;
		float maxX = -50000;
		float maxY = -50000;
		
		vector objectBoundsMin, objectBoundsMax;
		lockingTarget.GetBounds(objectBoundsMin, objectBoundsMax);
		
		float boundsMinX = objectBoundsMin[0];
		float boundsMinY = objectBoundsMin[1];
		float boundsMinZ = objectBoundsMin[2];
		float boundsMaxX = objectBoundsMax[0];
		float boundsMaxY = objectBoundsMax[1];
		float boundsMaxZ = objectBoundsMax[2];
		
		float offsetsX = offsets[0];
		float offsetsY = offsets[1];
		float offsetsZ = offsets[2];
		float marginsX = margins[0];
		float marginsY = margins[1];
		float marginsZ = margins[2];
		
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
		
		bool validCorner = false;
		foreach(vector corner : boundsCorners) 
		{
		    vector screenPos = workspace.ProjWorldToScreen(lockingTarget.CoordToParent(corner), m_eLauncher.GetWorld());
		    if (screenPos[2] > 0) 
			{
				float screenPosX = workspace.DPIUnscale(screenPos[0]);
				float screenPosY = workspace.DPIUnscale(screenPos[1]);
				
		        if (screenPosX < minX)
					minX = screenPosX;
		        if (screenPosX > maxX)
					maxX = screenPosX;
		        if (screenPosY < minY)
					minY = screenPosY;
		        if (screenPosY > maxY)
					maxY = screenPosY;
				validCorner = true;
		    }
		}
		
		if(!validCorner)
		{
			minX = 0;
			minY = 0;
			maxX = 0;
			maxY = 0;
		}
		
		boundsMin[0] = minX;
		boundsMin[1] = minY;
		boundsMax[0] = maxX;
		boundsMax[1] = maxY;
	}
	
	private void Debug_DrawLineSimple(vector start, vector end, array<ref Shape> dbgShapes, int color = ARGBF(1, 1, 1, 1))
	{
		vector p[2];
		p[0] = start;
		p[1] = end;

		int shapeFlags = ShapeFlags.NOOUTLINE;
		Shape s = Shape.CreateLines(color, shapeFlags, p, 2);
		dbgShapes.Insert(s);	
	}
};
