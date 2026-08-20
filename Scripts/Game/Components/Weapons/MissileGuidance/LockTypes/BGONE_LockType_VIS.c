[BaseContainerProps()]
class BGONE_LockType_VIS : BGONE_LockType_Base
{
	[Attribute("2000", UIWidgets.EditBox, desc: "Max Distance In Meters Launcher Will Lock Onto Target", category: "BGONE")]
	protected int m_iMaxLockOnRange;
	
	[Attribute("5", UIWidgets.EditBox, desc: "Min Distance In Meters Launcher Will Lock Onto Target", category: "BGONE")]
	protected int m_iMinLockOnRange;

	[Attribute("1.5", UIWidgets.EditBox, desc: "Duration In Seconds Needed To Achieve Full Lock", category: "BGONE")]
	protected float m_fLockAcquiringDuration;

	[Attribute("0.5", UIWidgets.EditBox, desc: "Duration In Seconds Before Lock is Fully Lost When Target Leaves Seeker", category: "BGONE")]
	protected float m_fLockLossDuration;

	[Attribute("{BF22E0769628374D}UI/layouts/BGONE_VIS_SeekBox.layout", UIWidgets.ResourcePickerThumbnail, desc: "Layout displayed when locking onto a target", category: "BGONE")]
	protected ResourceName m_sLockOnLayout;

	[Attribute("0", UIWidgets.ComboBox, "Units launcher can lock onto (empty = ALL)", "", ParamEnumArray.FromEnum(EEditableEntityType) )]
	protected ref array<EEditableEntityType> m_eUnitTypesToLock;

	[Attribute("0", UIWidgets.CheckBox, "Maintain Lock After Launching A Missile", category: "BGONE")]
	protected bool m_bKeepLockAfterFired;

	protected IEntity m_eLauncher;
	protected ref BGONE_TargetData m_cTargetDataVIS;
	
	protected float m_fNextScanTime = 0;
	protected float m_fScanInterval = 50.0; // 50ms (20 Hz)
	protected IEntity lastTarget;
	protected IEntity lockingTarget;
	
	protected Widget m_wDisplay;
	protected Widget m_wTL;
	protected Widget m_wTR;
	protected Widget m_wBL;
	protected Widget m_wBR;
	protected SizeLayoutWidget m_wCross;
	
	protected AudioHandle m_eLockAudioHandle = AudioHandle.Invalid;
	protected SoundComponent m_eSoundComponent;
	
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;
	protected ref array<IEntity> m_aCandidateEntities;
	
	override void InitLockType(IEntity owner)
	{
		super.InitLockType(owner);
		m_eLauncher = owner;
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
		m_aCandidateEntities = new array<IEntity>();
		m_eLockingData = new BGONE_LockingData_BASE();
		
		if(!m_eSoundComponent && m_eLauncher)
		{
			m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(SoundComponent));
			if(!m_eSoundComponent && m_eLauncher.GetRootParent())
				m_eSoundComponent = SoundComponent.Cast(m_eLauncher.GetRootParent().FindComponent(SoundComponent));
		}
	}

	override void StartLock()
	{
		super.StartLock();
		
		m_cTargetDataVIS = new BGONE_TargetData();
		lastTarget = null;
		lockingTarget = null;
		
		if(!m_wDisplay && !m_sLockOnLayout.IsEmpty())
		{
			ArmaReforgerScripted game = GetGame();
			if(game && game.GetWorkspace())
			{
				m_wDisplay = game.GetWorkspace().CreateWidgets(m_sLockOnLayout);
				if(m_wDisplay)
				{
					m_wTL = m_wDisplay.FindAnyWidget("TL");
					m_wTR = m_wDisplay.FindAnyWidget("TR");
					m_wBL = m_wDisplay.FindAnyWidget("BL");
					m_wBR = m_wDisplay.FindAnyWidget("BR");
					m_wCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));
					
					if(m_wCross)
						m_wCross.SetVisible(false);
						
					m_wDisplay.SetColorInt(Color.GRAY);
				}
			}
		}
	}

	override void StopLock()
	{
		super.StopLock();
		LockLost();
	}

	override void UpdateLock(float timeSlice)
	{
		if(!m_bIsLocking)
			return;
			
		if(!m_wDisplay)
			StartLock();
			
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
				m_eLockingData.lockingProgress = Math.Clamp(m_eLockingData.lockingProgress + (timeSlice / Math.Max(m_fLockAcquiringDuration, 0.05)) * 100.0, 0.0, 100.0);
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				
				PlayLockOnAudio(m_eLockingData.lockingProgress / 100.0);
				
				if(!m_cTargetDataVIS)
					m_cTargetDataVIS = new BGONE_TargetData();
					
				RplId rplId = Replication.FindItemId(lockingTarget);
				if(!rplId.IsValid() && lockingTarget.GetRootParent())
					rplId = Replication.FindItemId(lockingTarget.GetRootParent());
					
				m_cTargetDataVIS.targetRplId = rplId;
				m_cTargetDataVIS.targetPosition = m_eLockingData.lockingPos;
				m_eLockingData.targetData = m_cTargetDataVIS;
				
				if(m_eLockingData.lockingProgress >= 100.0)
				{
					LockAcquired(m_eLockingData);
				}
			}
			
			// Locking new target
			if(lastTarget != lockingTarget)
			{
				m_eLockingData.lockingProgress = 0;
				m_eLockingData.lockingPos = GetAimPoint(lockingTarget);
				m_cTargetDataVIS = new BGONE_TargetData();
				
				RplId rplId = Replication.FindItemId(lockingTarget);
				if(!rplId.IsValid() && lockingTarget.GetRootParent())
					rplId = Replication.FindItemId(lockingTarget.GetRootParent());
					
				m_cTargetDataVIS.targetRplId = rplId;
				m_cTargetDataVIS.targetPosition = m_eLockingData.lockingPos;
				m_eLockingData.targetData = m_cTargetDataVIS;
				LockStartAcquire(m_eLockingData);
			}
		}
		
		lastTarget = lockingTarget;
		DisplayOrUpdateLockonWidget();
	}

	override protected void LockLost()
	{
		lockingTarget = null;
		lastTarget = null;
		
		if(m_eLockingData)
		{
			m_eLockingData.lockingPos = Vector(0,0,0);
			m_eLockingData.lockingProgress = 0;
			m_eLockingData.targetData = null;
		}
		
		m_cTargetDataVIS = new BGONE_TargetData();
		TerminateLockOnAudio();
		
		if(!m_bIsLocking && m_wDisplay)
		{
			m_wDisplay.RemoveFromHierarchy();
			m_wDisplay = null;
			m_wTL = null;
			m_wTR = null;
			m_wBL = null;
			m_wBR = null;
			m_wCross = null;
		}
		
		super.LockLost();
	}

	protected vector GetAimPoint(IEntity target)
	{
		if(!target)
			return Vector(0,0,0);
			
		Physics phys = target.GetPhysics();
		if(phys)
		{
			vector com = phys.GetCenterOfMass();
			if(com != vector.Zero)
				return target.GetOrigin() + com;
		}
			
		return target.GetOrigin() + Vector(0, 1, 0);
	}

	protected IEntity ScanForTarget()
	{
		vector currentDir, aimFrom;
		GetAimDirAndPosOfLauncher(m_eLauncher, currentDir, aimFrom);
		
		if(lastTarget)
		{
			IEntity currentTarget = lastTarget;
			vector aimTo = GetAimPoint(currentTarget);
			vector toTarget = aimTo - aimFrom;
			float dist = toTarget.Length();
			
			if(dist > m_iMaxLockOnRange || dist < m_iMinLockOnRange)
				return null;
				
			float dot = Math.Clamp(vector.Dot(currentDir, toTarget.Normalized()), -1.0, 1.0);
			if(Math.Acos(dot) > 0.45) // ~26 degree cone
				return null;
				
			float relAngle = toTarget.ToYaw() * Math.DEG2RAD;
			
			for(float xOff = -2.5; xOff <= 2.5; xOff += 0.5)
			{
				for(float yOff = -1.0; yOff <= 2.0; yOff += 0.5)
				{
					vector testPos = currentTarget.CoordToParent(Vector(xOff * -Math.Cos(relAngle), yOff, xOff * Math.Sin(relAngle)));
					lastTarget = null;
					TraceLOS(aimFrom, testPos);
					if(lastTarget)
						return lastTarget;
				}
			}
			return null;
		}

		// Step 1: Direct Center-Aim Raycast
		vector centerAimTo = aimFrom + currentDir * (float)m_iMaxLockOnRange;
		lastTarget = null;
		TraceLOS(aimFrom, centerAimTo);
		if(!lastTarget)
			TraceLOS(aimFrom, centerAimTo);
		if(lastTarget)
			return lastTarget;

		// Step 2: Conical Multi-Ray Grid Scan
		for(float xOff = -4.0; xOff <= 4.0; xOff += 0.5)
		{
			for(float yOff = -2.0; yOff <= 2.0; yOff += 0.5)
			{
				vector offsetVector = vector.FromYaw(xOff).VectorToAngles();
				offsetVector[1] = yOff;
				vector aimDir = (currentDir.VectorToAngles() + offsetVector).AnglesToVector();
				vector testPos = aimFrom + aimDir * (float)m_iMaxLockOnRange;
				
				lastTarget = null;
				TraceLOS(aimFrom, testPos);
				if(lastTarget)
					return lastTarget;
			}
		}

		return null;
	}

	protected bool TraceLOS(vector from, vector to, bool excludeLockedTarget = false)
	{
		m_aExcludeEntities.Clear();
		if(m_eLauncher.GetRootParent())
			m_aExcludeEntities.Insert(m_eLauncher.GetRootParent());
		else if(m_eLauncher)
			m_aExcludeEntities.Insert(m_eLauncher);
			
		if(excludeLockedTarget && lockingTarget)
			m_aExcludeEntities.Insert(lockingTarget);

		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;

		float percent = GetGame().GetWorld().TraceMove(m_TraceParam, null);

		if(m_TraceParam.TraceEnt)
			CheckUnitType(m_TraceParam.TraceEnt);

		return (lastTarget != null);
	}

	protected bool CheckUnitType(IEntity ent)
	{
		if(!ent)
			return false;
			
		IEntity root = ent.GetRootParent();
		if(!root)
			root = ent;
			
		if(root == m_eLauncher || root == m_eLauncher.GetRootParent())
			return false;
			
		if(Vehicle.Cast(root) || Vehicle.Cast(ent) || SCR_ChimeraCharacter.Cast(root) || SCR_ChimeraCharacter.Cast(ent))
		{
			lastTarget = root;
			return true;
		}
		
		return false;
	}

	protected void DisplayOrUpdateLockonWidget()
	{
		if(!m_wDisplay)
			m_wDisplay = GetGame().GetWorkspace().CreateWidgets(m_sLockOnLayout);
			
		if(!m_wDisplay)
			return;

		Widget gateTL = m_wDisplay.FindAnyWidget("TL");
		Widget gateTR = m_wDisplay.FindAnyWidget("TR");
		Widget gateBL = m_wDisplay.FindAnyWidget("BL");
		Widget gateBR = m_wDisplay.FindAnyWidget("BR");
		SizeLayoutWidget lockCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));

		vector margins = Vector(0,0,0);
		vector offsets = Vector(0,0,0);
		if(lockingTarget)
			offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));

		vector boundsMin = Vector(0,0,0);
		vector boundsMax = Vector(0,0,0);
		WorldToScreenBounds(boundsMin, boundsMax, m_wDisplay, margins, offsets);

		vector currentDir, aimFrom;
		GetAimDirAndPosOfLauncher(m_eLauncher, currentDir, aimFrom);

		vector topLeftOffset = vector.FromYaw(-4.5).VectorToAngles();
		topLeftOffset[1] = 2;
		vector bottomRightOffset = vector.FromYaw(4.0).VectorToAngles();
		bottomRightOffset[1] = -2;

		vector tlPos = aimFrom + (currentDir.VectorToAngles() + topLeftOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		vector brPos = aimFrom + (currentDir.VectorToAngles() + bottomRightOffset).AnglesToVector() * (float)m_iMaxLockOnRange;

		WorkspaceWidget workspace = m_wDisplay.GetWorkspace();
		BaseWorld world = GetGame().GetWorld();

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
			minX = Math.Lerp(boundsMin[0] - 500, boundsMin[0], lerp);
			minY = Math.Lerp(boundsMin[1] - 500, boundsMin[1], lerp);
			maxX = Math.Lerp(boundsMax[0] + 500, boundsMax[0], lerp);
			maxY = Math.Lerp(boundsMax[1] + 500, boundsMax[1], lerp);
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
		BaseWorld world = GetGame().GetWorld();
		if(!workspace || !world)
			return;

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
		    if(screenPos[2] > 0)
			{
				float screenPosX = workspace.DPIUnscale(screenPos[0]);
				float screenPosY = workspace.DPIUnscale(screenPos[1]);

		        if(screenPosX < minX)
					minX = screenPosX;
		        if(screenPosX > maxX)
					maxX = screenPosX;
		        if(screenPosY < minY)
					minY = screenPosY;
		        if(screenPosY > maxY)
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
