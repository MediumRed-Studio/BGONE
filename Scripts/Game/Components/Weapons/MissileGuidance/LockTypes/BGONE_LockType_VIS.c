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
	protected WeaponSoundComponent m_eSoundComponent;
	protected AudioHandle m_eLockAudioHandle;
	protected ref Widget m_wDisplay;
	
	protected ref array<ref Shape> m_aDbgCollisionShapes;
	
	override void InitLockType(IEntity launcher)
	{
		super.InitLockType(launcher);
		if(!m_eSoundComponent && m_eLauncher)
			m_eSoundComponent = WeaponSoundComponent.Cast(m_eLauncher.FindComponent(WeaponSoundComponent));
		
		m_aDbgCollisionShapes = new array<ref Shape>;
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
		if(!super.UpdateLock(timeSlice))
			return null;
		
		lockingTarget = null;
		float currentTime = GetGame().GetWorld().GetWorldTime();
		if(currentTime > m_fNextScanTime)
		{
			m_fNextScanTime = currentTime + m_fScanInterval;
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
				m_eLockingData.lockingProgress = Math.Clamp(m_eLockingData.lockingProgress + (timeSlice / m_iLockOnTime) * 100.0, 0.0, 100.0);
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
			
				if(m_eLockingData.lockingProgress >= 100.0 && m_cTargetDataVIS.targetRplId == 0)
				{
					RplComponent rpl = RplComponent.Cast(lockingTarget.FindComponent(RplComponent));
					if(rpl)
						m_cTargetDataVIS.targetRplId = rpl.Id();
					else
						m_cTargetDataVIS.targetRplId = Replication.FindItemId(lockingTarget);
						
					m_cTargetDataVIS.targetPosition = m_eLockingData.lockingPos;
					m_eLockingData.targetData = m_cTargetDataVIS;
					LockAcquired(m_eLockingData);
				}
			}
			
			// Locking new target
			if(lastTarget != lockingTarget)
			{
				m_eLockingData.lockingProgress = 0;
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				m_cTargetDataVIS = new BGONE_TargetData();
				m_cTargetDataVIS.targetPosition = m_eLockingData.lockingPos;
				m_eLockingData.targetData = m_cTargetDataVIS;
				LockStartAcquire(m_eLockingData);
				return m_eLockingData;
			} 
		}
		
		lastTarget = lockingTarget;
		DisplayOrUpdateLockonWidget();
		
		return m_eLockingData;
	}
	
	protected vector GetAimPoint(IEntity target)
	{
		Physics phys = target.GetPhysics();
		vector centerOfMass = Vector(0,0,0);
		if(phys)
			centerOfMass = phys.GetCenterOfMass();
			
		vector aimPoint;
		if(centerOfMass == vector.Zero)
			aimPoint = target.GetOrigin() + vector.Up;
		else
			aimPoint = target.GetOrigin() + centerOfMass;
		
		return aimPoint;
	}
	
	protected IEntity ScanForTarget()
	{
		vector currentDir, aimFrom;
		GetAimDirAndPosOfLauncher(m_eLauncher, currentDir, aimFrom);
		
		if (lastTarget)
		{
			IEntity currentTarget = lastTarget;
	    	vector aimTo = GetAimPoint(currentTarget);
			
		    if (vector.Distance(aimFrom, aimTo) > m_iMaxLockOnRange) 
				return null;
			
		    if (Math.Acos(vector.Dot(currentDir, vector.Direction(aimFrom, aimTo).Normalized())) > 0.3) 	// ~35 degree limit for lock seeker
				return null;
		
			float relAngle = vector.Direction(aimFrom, aimTo).ToYaw() * Math.DEG2RAD;
		
		    for (float xOff = -2.5; xOff <= 2.5; xOff += 0.5)
			{
		        for (float yOff = -1.0; yOff <= 2.0; yOff += 0.5)
				{
		            vector testPos = currentTarget.CoordToParent(Vector(xOff * -Math.Cos(relAngle), yOff, xOff * Math.Sin(relAngle)));
					lastTarget = null;
					TraceLOS(aimFrom, testPos);
		            if (lastTarget)
						return lastTarget;
		        }
		    }
			return null;
		}
		
		// Check twice if we're aiming directly at a target before we go scannin'
		vector aimTo = aimFrom + currentDir * (float)m_iMaxLockOnRange;
		lastTarget = null;
		TraceLOS(aimFrom, aimTo);
		if(!lastTarget)
			TraceLOS(aimFrom, aimTo);
		if(lastTarget)
			return lastTarget;
		
		// Attempt to scan using multiple raycasts matching upstream grid
		for (float xOff = -4.0; xOff <= 4.0; xOff += 0.5)
		{
		    for (float yOff = -2.0; yOff <= 2.0; yOff += 0.5)
			{
				vector offsetVector = vector.FromYaw(xOff).VectorToAngles();
				offsetVector[1] = yOff;
				vector aimDir = (currentDir.VectorToAngles() + offsetVector).AnglesToVector();
		        vector testPos = aimFrom + aimDir * (float)m_iMaxLockOnRange;
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
		ref array<IEntity> exclude = {m_eLauncher.GetRootParent(), lockingTarget };
		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.LayerMask = EPhysicsLayerDefs.Projectile;
		param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS; 
		if(excludeLockedTarget)
			param.ExcludeArray = exclude;
		else
			param.Exclude = m_eLauncher.GetRootParent();
			
		float percent = GetGame().GetWorld().TraceMove(param, null);
		
		if(param.TraceEnt)
			CheckUnitType(param.TraceEnt);
		
		if (percent == 1.0)
			return true;
				
		return (lastTarget != null);
	}
	
	protected bool CheckUnitType(IEntity ent)
	{
		PerceivableComponent perceivableComp = PerceivableComponent.Cast(ent.FindComponent(PerceivableComponent));
		if (!perceivableComp)
		{
			if(ent.GetRootParent())
				perceivableComp = PerceivableComponent.Cast(ent.GetRootParent().FindComponent(PerceivableComponent));
		}
		
		if (!perceivableComp)
			return false;

		if(m_eUnitTypesToLock.Count() > 0 && !m_eUnitTypesToLock.Contains(perceivableComp.GetUnitType()))
			return false;
		
		IEntity root = ent.GetRootParent();
		if(root)
			lastTarget = root;
		else
			lastTarget = ent;
			
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
	
	override protected void LockLost() 
	{
		if(m_wDisplay)
		{
			m_wDisplay.RemoveFromHierarchy();
			m_wDisplay = null;
		}
		
		lockingTarget = null;
		lastTarget = null;
		if(m_eLockingData)
		{
			m_eLockingData.lockingPos = Vector(0,0,0);
			m_eLockingData.lockingProgress = 0;
		}
		m_cTargetDataVIS = new BGONE_TargetData();
		super.LockLost();
	}
	
	override void PlayLockOnAudio(float currentLockProgress)
	{
		if(!m_eSoundComponent)
			return;
		
		m_eSoundComponent.SetSignalValueStr("LockingState", currentLockProgress * 100.0);
		m_eSoundComponent.Terminate(m_eLockAudioHandle);
		m_eLockAudioHandle = m_eSoundComponent.SoundEvent("SOUND_LOCKON_DEFAULT");
	}
	
	override void TerminateLockOnAudio()
	{
		if(!m_eSoundComponent)
			return;
		
		m_eSoundComponent.Terminate(m_eLockAudioHandle);
	}
	
	protected void DisplayOrUpdateLockonWidget()
	{
		if(!m_wDisplay)
			m_wDisplay = GetGame().GetWorkspace().CreateWidgets(m_sLockOnLayout);
			
		if(!m_wDisplay)
			return;
		
		Widget gateTL = m_wDisplay.FindWidget("TL");
		Widget gateTR = m_wDisplay.FindWidget("TR");
		Widget gateBL = m_wDisplay.FindWidget("BL");
		Widget gateBR = m_wDisplay.FindWidget("BR");
		SizeLayoutWidget lockCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));
		
		vector margins = Vector(0,0,0);
		vector offsets = Vector(0,0,0);
		if(lockingTarget)
			offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));
		vector boundsMin = Vector(0,0,0);
		vector boundsMax = Vector(0,0,0);
		WorldToScreenBounds(boundsMin, boundsMax, m_wDisplay, margins, offsets);
		
		float offsetX = 0;
		float offsetY = 0;
		
		vector topLeftOffset, bottomRightOffset;
		topLeftOffset = vector.FromYaw(-4.5).VectorToAngles();
		topLeftOffset[1] = 2;
		bottomRightOffset = vector.FromYaw(4.0).VectorToAngles();
		bottomRightOffset[1] = -2;
		
		vector currentDir, aimFrom;
		GetAimDirAndPosOfLauncher(m_eLauncher, currentDir, aimFrom);
		
		vector tlPos = aimFrom + (currentDir.VectorToAngles() + topLeftOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		vector brPos = aimFrom + (currentDir.VectorToAngles() + bottomRightOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		
		WorkspaceWidget workspace = m_wDisplay.GetWorkspace();
		BaseWorld world = m_eLauncher.GetWorld();
		
		vector tlScreen = workspace.ProjWorldToScreen(tlPos, world);
		vector brScreen = workspace.ProjWorldToScreen(brPos, world);
		
		float constraintLeft = workspace.DPIUnscale(tlScreen[0]);
		float constraintTop = workspace.DPIUnscale(tlScreen[1]);
		float constraintRight = workspace.DPIUnscale(brScreen[0]);
		float constraintBottom = workspace.DPIUnscale(brScreen[1]);
		
		float lerp = Math.Min(m_eLockingData.lockingProgress / 80.0, 1.0);
		float minX, minY, maxX, maxY;
		
		if(lerp > 0 && lockingTarget)
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
		
		if(gateTL) FrameSlot.SetPos(gateTL, minX, minY);
	    if(gateTR) FrameSlot.SetPos(gateTR, maxX, minY);
	    if(gateBL) FrameSlot.SetPos(gateBL, minX, maxY);
	    if(gateBR) FrameSlot.SetPos(gateBR, maxX, maxY);

		if(m_eLockingData.lockingProgress >= 100.0 && lockingTarget)
		{
			m_wDisplay.SetColorInt(Color.GREEN);
			if(lockCross)
			{
				vector uiPos = workspace.ProjWorldToScreen(m_eLockingData.lockingPos, world);
				float crossX = workspace.DPIUnscale(uiPos[0]);
				float crossY = workspace.DPIUnscale(uiPos[1]);
				lockCross.SetWidthOverride(46);
				lockCross.SetHeightOverride(46);
				lockCross.SetColorInt(Color.GRAY);
				FrameSlot.SetPos(lockCross, crossX - 23.0, crossY - 23.0);
				lockCross.SetVisible(true);
			}
		}
		else 
		{
			m_wDisplay.SetColorInt(Color.GRAY);
			if(lockCross)
				lockCross.SetVisible(false);
		}
	}
	
	protected void WorldToScreenBounds(out vector boundsMin, out vector boundsMax, Widget widget, vector margins, vector offsets)
	{
		if(!lockingTarget)
			return;
		
		WorkspaceWidget workspace = widget.GetWorkspace();
		BaseWorld world = m_eLauncher.GetWorld();
		
		float minX = 5000;
		float minY = 5000;
		float maxX = -5000;
		float maxY = -5000;
		
		vector objectBoundsMin, objectBoundsMax;
		lockingTarget.GetBounds(objectBoundsMin, objectBoundsMax);
		
		float boundsMinX = Math.Min(objectBoundsMin[0] - margins[0], 0) + offsets[0];
		float boundsMinY = Math.Min(objectBoundsMin[1] - margins[1], 0) + offsets[1];
		float boundsMinZ = Math.Min(objectBoundsMin[2] - margins[2], 0) + offsets[2];
		
		float boundsMaxX = Math.Max(objectBoundsMax[0] + margins[0], 0) + offsets[0];
		float boundsMaxY = Math.Max(objectBoundsMax[1] + margins[1], 0) + offsets[1];
		float boundsMaxZ = Math.Max(objectBoundsMax[2] + margins[2], 0) + offsets[2];
		
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
		    vector screenPos = workspace.ProjWorldToScreen(lockingTarget.CoordToParent(corner), world);
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
		    }
		}
		
		if(minX > maxX || minY > maxY)
		{
			boundsMin[0] = 0;
			boundsMin[1] = 0;
			boundsMax[0] = workspace.DPIUnscale(workspace.GetWidth());
			boundsMax[1] = workspace.DPIUnscale(workspace.GetHeight());
		}
		else
		{
			boundsMin[0] = minX;
			boundsMin[1] = minY;
			boundsMax[0] = maxX;
			boundsMax[1] = maxY;
		}
	}

	override void PlayLockOnAudio(float currentLockProgress)
	{
		if(!m_eSoundComponent && m_eLauncher)
		{
			m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(SoundComponent));
			if(!m_eSoundComponent && m_eLauncher.GetRootParent())
				m_eSoundComponent = SoundComponent.Cast(m_eLauncher.GetRootParent().FindComponent(SoundComponent));
		}

		if(!m_eSoundComponent)
			return;

		m_eSoundComponent.SetSignalValueStr("LockingState", currentLockProgress * 100.0);

		if(m_eLockAudioHandle == AudioHandle.Invalid)
		{
			m_eLockAudioHandle = m_eSoundComponent.SoundEvent("SOUND_LOCKON_DEFAULT");
		}
	}

	override void TerminateLockOnAudio()
	{
		if(m_eSoundComponent)
		{
			m_eSoundComponent.SetSignalValueStr("LockingState", 0.0);
			if(m_eLockAudioHandle != AudioHandle.Invalid)
			{
				m_eSoundComponent.Terminate(m_eLockAudioHandle);
				m_eLockAudioHandle = AudioHandle.Invalid;
			}
		}
	}

	override BGONE_TargetData GetCurrentTargetData()
	{
		if(!m_bKeepLockAfterFired)
			GetGame().GetCallqueue().CallLater(StopLock, 10, false);
		return m_cTargetDataVIS;
	}
}
