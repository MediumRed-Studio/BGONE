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

	[Attribute("1", UIWidgets.ComboBox, "Units launcher can lock onto", "", ParamEnumArray.FromEnum(EEditableEntityType) )]
	protected ref array<EEditableEntityType> m_eUnitTypesToLock;

	protected IEntity m_eLauncher;
	protected ref BGONE_TargetData m_eTargetData;
	protected float m_fCurrentLockProgress = 0;
	protected float m_fCurrentLockDuration = 0;
	protected float m_fCurrentLockLossDuration = 0;

	protected float m_fNextScanTime = 0;
	protected float m_fScanInterval = 50.0; // 50ms (20 Hz)
	
	protected Widget m_wDisplay;
	protected Widget m_wTL;
	protected Widget m_wTR;
	protected Widget m_wBL;
	protected Widget m_wBR;
	protected SizeLayoutWidget m_wCross;
	
	protected AudioHandle m_eLockAudioHandle = AudioHandle.Invalid;
	protected SoundComponent m_eSoundComponent;
	protected bool m_bLockEventFired = false;
	
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;
	protected ref BGONE_LockingData_BASE m_LockingData;
	
	override void InitLockType(IEntity owner)
	{
		m_eLauncher = owner;
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
		m_LockingData = new BGONE_LockingData_BASE();
	}

	override void StartLock()
	{
		if(!m_wDisplay && !m_sLockOnLayout.IsEmpty())
		{
			ArmaReforgerScripted game = GetGame();
			if(game)
			{
				WorkspaceWidget workspace = game.GetWorkspace();
				if(workspace)
				{
					m_wDisplay = workspace.CreateWidgets(m_sLockOnLayout);
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
	}

	override void StopLock()
	{
		m_fCurrentLockProgress = 0;
		m_fCurrentLockDuration = 0;
		m_fCurrentLockLossDuration = 0;
		m_eTargetData = null;
		m_bLockEventFired = false;
		
		TerminateLockOnAudio();
		
		if(m_wDisplay)
		{
			m_wDisplay.RemoveFromHierarchy();
			m_wDisplay = null;
			m_wTL = null;
			m_wTR = null;
			m_wBL = null;
			m_wBR = null;
			m_wCross = null;
		}
	}

	override void UpdateLock(float timeSlice)
	{
		if(!m_wDisplay)
			StartLock();
			
		float currentTime = GetGame().GetWorld().GetWorldTime();
		if(currentTime > m_fNextScanTime)
		{
			m_fNextScanTime = currentTime + m_fScanInterval;
			BGONE_TargetData scannedData = ScanForTarget();
			
			if(scannedData && m_eTargetData)
			{
				if(scannedData.targetRplId != m_eTargetData.targetRplId)
				{
					m_fCurrentLockProgress = 0;
					m_fCurrentLockDuration = 0;
					m_bLockEventFired = false;
					TerminateLockOnAudio();
				}
			}
			m_eTargetData = scannedData;
		}
		
		if(m_eTargetData && m_eTargetData.GetTargetEntity())
		{
			m_fCurrentLockLossDuration = 0;
			m_fCurrentLockDuration += timeSlice;
			m_fCurrentLockProgress = Math.Clamp(m_fCurrentLockDuration / Math.Max(m_fLockAcquiringDuration, 0.05), 0.0, 1.0);
			
			if(!m_LockingData)
				m_LockingData = new BGONE_LockingData_BASE();
				
			m_LockingData.lockingProgress = m_fCurrentLockProgress * 100.0;
			m_LockingData.targetData = m_eTargetData;
			
			if(m_fCurrentLockProgress >= 1.0)
			{
				if(!m_bLockEventFired)
				{
					m_bLockEventFired = true;
					if(m_OnLockAcquired)
						m_OnLockAcquired.Invoke(m_LockingData);
				}
			}
			else
			{
				m_bLockEventFired = false;
				if(m_OnLockStartAcquire)
					m_OnLockStartAcquire.Invoke(m_LockingData);
			}
		}
		else
		{
			if(m_bLockEventFired || m_fCurrentLockProgress > 0)
			{
				m_fCurrentLockLossDuration += timeSlice;
				if(m_fCurrentLockLossDuration >= m_fLockLossDuration)
				{
					if(m_OnLockLost)
						m_OnLockLost.Invoke();
						
					m_fCurrentLockProgress = 0;
					m_fCurrentLockDuration = 0;
					m_fCurrentLockLossDuration = 0;
					m_bLockEventFired = false;
					m_eTargetData = null;
					TerminateLockOnAudio();
				}
			}
			else
			{
				m_fCurrentLockProgress = 0;
				m_fCurrentLockDuration = 0;
				m_eTargetData = null;
			}
		}
		
		DisplayOrUpdateLockonWidget();
	}

	protected vector GetAimPoint(IEntity target)
	{
		if(!target)
			return Vector(0,0,0);
			
		Physics phys = target.GetPhysics();
		if(phys)
			return target.CoordToParent(phys.GetCenterOfMass());
			
		return target.GetOrigin() + Vector(0, 1, 0);
	}

	protected BGONE_TargetData ScanForTarget()
	{
		vector aimDir, aimPos;
		GetAimDirAndPosOfLauncher(m_eLauncher, aimDir, aimPos);
		
		IEntity currentLockedEnt = null;
		if(m_eTargetData)
			currentLockedEnt = m_eTargetData.GetTargetEntity();

		// If current target is still in cone and has LOS, check it
		if(currentLockedEnt)
		{
			vector currentAimPoint = GetAimPoint(currentLockedEnt);
			vector toCurrent = currentAimPoint - aimPos;
			float currentDist = toCurrent.Length();
			if(currentDist >= m_iMinLockOnRange && currentDist <= m_iMaxLockOnRange)
			{
				float dot = Math.Clamp(vector.Dot(aimDir, toCurrent.Normalized()), -1.0, 1.0);
				if(dot >= 0.90) // ~25 degree cone
				{
					if(TraceLOS(aimPos, currentAimPoint, currentLockedEnt))
					{
						// Retain lock if player hasn't aimed at a different target
						// (will be overridden below if center ray directly hits another target)
					}
				}
			}
		}

		// Step 1: Direct Center-Aim Raycast (exact crosshair hit)
		IEntity directHit = TraceRay(aimPos, aimPos + (aimDir * (float)m_iMaxLockOnRange));
		if(directHit)
		{
			if(currentLockedEnt == directHit)
				return m_eTargetData;
				
			BGONE_TargetData directData = new BGONE_TargetData();
			RplComponent directRpl = RplComponent.Cast(directHit.FindComponent(RplComponent));
			if(directRpl)
				directData.targetRplId = directRpl.Id();
			return directData;
		}

		// Step 2: Radial conical raycast scan expanding outward from center
		float offsetsX[16] = {-0.5, 0.5, 0, 0, -1.0, 1.0, 0, 0, -2.0, 2.0, -2.0, 2.0, -3.5, 3.5, -3.5, 3.5};
		float offsetsY[16] = {0, 0, -0.5, 0.5, 0, 0, -1.0, 1.0, -1.0, -1.0, 1.0, 1.0, -1.5, -1.5, 1.5, 1.5};
		
		for(int i = 0; i < 16; i++)
		{
			vector offsetAngles = vector.FromYaw(offsetsX[i]).VectorToAngles();
			offsetAngles[1] = offsetsY[i];
			vector rayDir = (aimDir.VectorToAngles() + offsetAngles).AnglesToVector();
			vector rayEnd = aimPos + (rayDir * (float)m_iMaxLockOnRange);
			
			IEntity hit = TraceRay(aimPos, rayEnd);
			if(hit)
			{
				if(currentLockedEnt == hit)
					return m_eTargetData;
					
				BGONE_TargetData scanData = new BGONE_TargetData();
				RplComponent hitRpl = RplComponent.Cast(hit.FindComponent(RplComponent));
				if(hitRpl)
					scanData.targetRplId = hitRpl.Id();
				return scanData;
			}
		}

		return null;
	}

	protected IEntity TraceRay(vector from, vector to)
	{
		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eLauncher);
		if(m_eLauncher.GetRootParent())
			m_aExcludeEntities.Insert(m_eLauncher.GetRootParent());
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		
		GetGame().GetWorld().TraceMove(m_TraceParam, null);
		if(m_TraceParam.TraceEnt)
		{
			IEntity root = m_TraceParam.TraceEnt.GetRootParent();
			if(!root)
				root = m_TraceParam.TraceEnt;
				
			if(root != m_eLauncher && root != m_eLauncher.GetRootParent() && CheckUnitType(root))
			{
				vector targetPoint = GetAimPoint(root);
				float dist = vector.Distance(from, targetPoint);
				if(dist >= m_iMinLockOnRange && dist <= m_iMaxLockOnRange)
					return root;
			}
		}
		return null;
	}

	protected bool CheckUnitType(IEntity ent)
	{
		if(!ent)
			return false;
			
		IEntity root = ent.GetRootParent();
		if(!root)
			root = ent;
			
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(root.FindComponent(SCR_EditableEntityComponent));
		if(!editable)
			editable = SCR_EditableEntityComponent.Cast(ent.FindComponent(SCR_EditableEntityComponent));
			
		if(editable)
		{
			EEditableEntityType entityType = editable.GetEntityType();
			if(!m_eUnitTypesToLock || m_eUnitTypesToLock.IsEmpty() || m_eUnitTypesToLock.Contains(entityType))
				return true;
		}
		
		if(Vehicle.Cast(root) || Vehicle.Cast(ent))
			return true;
			
		if(root.FindComponent(VehicleControllerComponent) || ent.FindComponent(VehicleControllerComponent))
			return true;
			
		if(SCR_ChimeraCharacter.Cast(root) || SCR_ChimeraCharacter.Cast(ent))
			return true;
			
		if(root.FindComponent(CharacterControllerComponent) || ent.FindComponent(CharacterControllerComponent))
			return true;
			
		return false;
	}

	protected bool TraceLOS(vector from, vector to, IEntity targetEntity)
	{
		if(vector.DistanceSq(from, to) < 0.01)
			return true;
			
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eLauncher);
		if(m_eLauncher.GetRootParent())
			m_aExcludeEntities.Insert(m_eLauncher.GetRootParent());
		if(targetEntity)
		{
			m_aExcludeEntities.Insert(targetEntity);
			if(targetEntity.GetRootParent())
				m_aExcludeEntities.Insert(targetEntity.GetRootParent());
		}
		
		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		
		float fraction = GetGame().GetWorld().TraceMove(m_TraceParam, null);
		return (fraction >= 0.98);
	}

	protected void DisplayOrUpdateLockonWidget()
	{
		if(!m_wDisplay)
			return;
			
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if(!workspace)
			return;
			
		IEntity lockingTarget = null;
		if(m_eTargetData)
			lockingTarget = m_eTargetData.GetTargetEntity();

		vector boundsMin = Vector(0,0,0);
		vector boundsMax = Vector(0,0,0);
		if(lockingTarget)
		{
			vector offsets = lockingTarget.CoordToLocal(GetAimPoint(lockingTarget));
			WorldToScreenBounds(boundsMin, boundsMax, m_wDisplay, Vector(0,0,0), offsets, lockingTarget);
		}

		vector currentDir, aimFrom;
		GetAimDirAndPosOfLauncher(m_eLauncher, currentDir, aimFrom);
		
		vector topLeftOffset = vector.FromYaw(-4.5).VectorToAngles();
		topLeftOffset[1] = 2;
		vector bottomRightOffset = vector.FromYaw(4.0).VectorToAngles();
		bottomRightOffset[1] = -2;
		
		vector tlPos = aimFrom + (currentDir.VectorToAngles() + topLeftOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		vector brPos = aimFrom + (currentDir.VectorToAngles() + bottomRightOffset).AnglesToVector() * (float)m_iMaxLockOnRange;
		
		vector tlScreen = workspace.ProjWorldToScreen(tlPos, GetGame().GetWorld());
		vector brScreen = workspace.ProjWorldToScreen(brPos, GetGame().GetWorld());
		
		float constraintLeft = workspace.DPIUnscale(tlScreen[0]);
		float constraintTop = workspace.DPIUnscale(tlScreen[1]);
		float constraintRight = workspace.DPIUnscale(brScreen[0]);
		float constraintBottom = workspace.DPIUnscale(brScreen[1]);
		
		float lerp = Math.Min(m_fCurrentLockProgress / 0.8, 1.0);
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
		
		if(m_wTL) FrameSlot.SetPos(m_wTL, minX, minY);
		if(m_wTR) FrameSlot.SetPos(m_wTR, maxX, minY);
		if(m_wBL) FrameSlot.SetPos(m_wBL, minX, maxY);
		if(m_wBR) FrameSlot.SetPos(m_wBR, maxX, maxY);

		if(m_fCurrentLockProgress >= 1.0 && lockingTarget)
		{
			m_wDisplay.SetColorInt(Color.GREEN);
			if(m_wCross)
			{
				vector uiPos = workspace.ProjWorldToScreen(GetAimPoint(lockingTarget), GetGame().GetWorld());
				float crossX = workspace.DPIUnscale(uiPos[0]);
				float crossY = workspace.DPIUnscale(uiPos[1]);
				
				m_wCross.SetWidthOverride(46);
				m_wCross.SetHeightOverride(46);
				m_wCross.SetColorInt(Color.GRAY);
				FrameSlot.SetPos(m_wCross, crossX - 23, crossY - 23);
				m_wCross.SetVisible(true);
			}
		}
		else 
		{
			m_wDisplay.SetColorInt(Color.GRAY);
			if(m_wCross)
				m_wCross.SetVisible(false);
		}
	}

	protected void WorldToScreenBounds(out vector boundsMin, out vector boundsMax, Widget widget, vector margins, vector offsets, IEntity target)
	{
		if(!target || !widget)
			return;
			
		WorkspaceWidget workspace = widget.GetWorkspace();
		if(!workspace)
			return;
			
		float minX = 5000;
		float minY = 5000;
		float maxX = -5000;
		float maxY = -5000;
		
		vector objectBoundsMin, objectBoundsMax;
		target.GetBounds(objectBoundsMin, objectBoundsMax);
		
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
		    vector screenPos = workspace.ProjWorldToScreen(target.CoordToParent(corner), GetGame().GetWorld());
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
		
		boundsMin[0] = minX;
		boundsMin[1] = minY;
		boundsMax[0] = maxX;
		boundsMax[1] = maxY;
	}

	override void PlayLockOnAudio(float currentLockProgress)
	{
		if(!m_eSoundComponent && m_eLauncher)
		{
			m_eSoundComponent = SoundComponent.Cast(m_eLauncher.FindComponent(SoundComponent));
			if(!m_eSoundComponent)
			{
				IEntity root = m_eLauncher.GetRootParent();
				if(root)
					m_eSoundComponent = SoundComponent.Cast(root.FindComponent(SoundComponent));
			}
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
		if(m_fCurrentLockProgress >= 1.0)
			return m_eTargetData;
		return null;
	}
}
