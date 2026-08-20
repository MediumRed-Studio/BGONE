[BaseContainerProps()]
class BGONE_LockType_VIS : BGONE_LockType_Base
{
	[Attribute("2000", UIWidgets.EditBox, desc: "Max Distance In Meters Launcher Will Lock Onto Target", category: "BGONE")]
	protected int m_iMaxLockOnRange;
	
	[Attribute("100", UIWidgets.EditBox, desc: "Min Distance In Meters Launcher Will Lock Onto Target", category: "BGONE")]
	protected int m_iMinLockOnRange;

	[Attribute("1.0", UIWidgets.EditBox, desc: "Duration In Seconds Needed To Achieve Full Lock", category: "BGONE")]
	protected float m_fLockAcquiringDuration;

	[Attribute("0.5", UIWidgets.EditBox, desc: "Duration In Seconds Before Lock is Fully Lost When Target Leaves Seeker", category: "BGONE")]
	protected float m_fLockLossDuration;

	[Attribute("{BF22E0769628374D}UI/layouts/BGONE_VIS_SeekBox.layout", UIWidgets.ResourcePickerThumbnail, desc: "Layout displayed when locking onto a target", category: "BGONE")]
	protected ResourceName m_sLockOnLayout;

	[Attribute("1", UIWidgets.ComboBox, "Units launcher can lock onto", "", ParamEnumArray.FromEnum(EAIUnitType) )]
	protected ref array<EAIUnitType> m_eUnitTypesToLock;

	protected IEntity m_eLauncher;
	protected ref BGONE_TargetData m_eTargetData;
	protected float m_fCurrentLockProgress = 0;
	protected float m_fCurrentLockDuration = 0;
	protected float m_fCurrentLockLossDuration = 0;

	protected float m_fNextScanTime = 0;
	protected float m_fScanInterval = 0.5; // 500ms in seconds
	
	protected Widget m_wDisplay;
	protected SizeLayoutWidget m_wCross;
	protected FrameWidget m_wSeekBox;
	protected ImageWidget m_wTL;
	protected ImageWidget m_wTR;
	protected ImageWidget m_wBL;
	protected ImageWidget m_wBR;
	
	protected AudioHandle m_eLockAudioHandle = AudioHandle.Empty;
	protected SoundComponent m_eSoundComponent;
	protected bool m_bLockEventFired = false;
	
	// Pre-allocated raycast buffers to eliminate per-frame GC allocations
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;
	protected ref array<IEntity> m_aCandidateEntities;
	
	override void InitLockType(IEntity owner)
	{
		m_eLauncher = owner;
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
		m_aCandidateEntities = new array<IEntity>();
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
						m_wCross = SizeLayoutWidget.Cast(m_wDisplay.FindAnyWidget("Cross"));
						m_wSeekBox = FrameWidget.Cast(m_wDisplay.FindAnyWidget("SeekBox"));
						m_wTL = ImageWidget.Cast(m_wDisplay.FindWidget("TL"));
						m_wTR = ImageWidget.Cast(m_wDisplay.FindWidget("TR"));
						m_wBL = ImageWidget.Cast(m_wDisplay.FindWidget("BL"));
						m_wBR = ImageWidget.Cast(m_wDisplay.FindWidget("BR"));
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
			m_wCross = null;
			m_wSeekBox = null;
			m_wTL = null;
			m_wTR = null;
			m_wBL = null;
			m_wBR = null;
		}
	}

	override void UpdateLock(float timeSlice)
	{
		float currentTime = GetGame().GetWorld().GetWorldTime();
		if(currentTime > m_fNextScanTime)
		{
			m_fNextScanTime = currentTime + m_fScanInterval;
			m_eTargetData = ScanForTarget();
		}
		
		if(m_eTargetData)
		{
			IEntity target = m_eTargetData.GetTargetEntity();
			if(target)
			{
				m_fCurrentLockDuration += timeSlice;
				m_fCurrentLockProgress = Math.Clamp(m_fCurrentLockDuration / m_fLockAcquiringDuration, 0, 1);
				
				if(!m_bLockEventFired)
				{
					ref BGONE_LockingData_BASE data = new BGONE_LockingData_BASE();
					data.lockingProgress = m_fCurrentLockProgress;
					data.targetData = m_eTargetData;
					
					if(m_fCurrentLockProgress >= 1.0)
					{
						m_bLockEventFired = true;
						m_OnLockAcquired.Invoke(data);
					}
					else
					{
						m_OnLockStartAcquire.Invoke(data);
					}
				}
			}
			else
			{
				HandleLockLoss(timeSlice);
			}
		}
		else
		{
			HandleLockLoss(timeSlice);
		}
		
		UpdateDisplay();
	}

	protected void HandleLockLoss(float timeSlice)
	{
		m_fCurrentLockLossDuration += timeSlice;
		if(m_fCurrentLockLossDuration >= m_fLockLossDuration)
		{
			if(m_bLockEventFired || m_fCurrentLockProgress > 0)
			{
				m_OnLockLost.Invoke();
			}
			StopLock();
		}
	}

	protected BGONE_TargetData ScanForTarget()
	{
		vector aimDir, aimPos;
		GetAimDirAndPosOfLauncher(m_eLauncher, aimDir, aimPos);
		
		// If tracking an existing locked target, verify line of sight first
		if(m_eTargetData && m_eTargetData.GetTargetEntity())
		{
			IEntity currentTarget = m_eTargetData.GetTargetEntity();
			vector targetPos = currentTarget.GetOrigin();
			if(currentTarget.GetPhysics())
				targetPos = currentTarget.CoordToParent(currentTarget.GetPhysics().GetCenterOfMass());
				
			vector toTarget = targetPos - aimPos;
			float dist = toTarget.Length();
			if(dist >= m_iMinLockOnRange && dist <= m_iMaxLockOnRange)
			{
				float dot = Math.Clamp(vector.Dot(aimDir, toTarget.Normalized()), -1.0, 1.0);
				if(Math.Acos(dot) <= 0.35) // ~20 degree cone
				{
					if(TraceLOS(aimPos, targetPos, currentTarget))
						return m_eTargetData;
				}
			}
		}

		// Broadphase: Spatial sphere query for candidate targets
		m_aCandidateEntities.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(aimPos, m_iMaxLockOnRange, FilterCandidateEntity, EQueryEntitiesFlags.DYNAMIC);
		
		IEntity bestTarget = null;
		float bestScore = -1.0;
		
		foreach(IEntity candidate : m_aCandidateEntities)
		{
			if(!candidate || candidate == m_eLauncher || candidate == m_eLauncher.GetRootParent())
				continue;
				
			vector candPos = candidate.GetOrigin();
			if(candidate.GetPhysics())
				candPos = candidate.CoordToParent(candidate.GetPhysics().GetCenterOfMass());
				
			vector toCand = candPos - aimPos;
			float candDist = toCand.Length();
			if(candDist < m_iMinLockOnRange || candDist > m_iMaxLockOnRange)
				continue;
				
			float dot = Math.Clamp(vector.Dot(aimDir, toCand.Normalized()), -1.0, 1.0);
			if(dot < 0.94) // Must be within ~20 degrees of center
				continue;
				
			if(CheckUnitType(candidate))
			{
				if(TraceLOS(aimPos, candPos, candidate))
				{
					if(dot > bestScore)
					{
						bestScore = dot;
						bestTarget = candidate;
					}
				}
			}
		}
		
		if(bestTarget)
		{
			BGONE_TargetData data = new BGONE_TargetData();
			RplComponent rpl = RplComponent.Cast(bestTarget.FindComponent(RplComponent));
			if(rpl)
				data.targetRplId = rpl.Id();
			return data;
		}
		
		return null;
	}

	protected bool FilterCandidateEntity(IEntity ent)
	{
		if(ent && ent != m_eLauncher)
		{
			m_aCandidateEntities.Insert(ent);
		}
		return true;
	}

	protected bool CheckUnitType(IEntity ent)
	{
		if(!ent)
			return false;
			
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(ent.FindComponent(SCR_EditableEntityComponent));
		if(editable)
		{
			EAIUnitType unitType = editable.GetAIUnitType();
			if(m_eUnitTypesToLock.Contains(unitType))
				return true;
		}
		
		// Fallback for vehicles
		if(Vehicle.Cast(ent) || Vehicle.Cast(ent.GetRootParent()))
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

	override void PlayLockOnAudio(float currentLockProgress)
	{
		if(!m_eSoundComponent && m_eLauncher)
		{
			IEntity root = m_eLauncher.GetRootParent();
			if(root)
				m_eSoundComponent = SoundComponent.Cast(root.FindComponent(SoundComponent));
		}
		
		if(!m_eSoundComponent)
			return;
			
		if(currentLockProgress >= 1.0)
		{
			m_eSoundComponent.SoundEvent("SOUND_LOCK_CONFIRMED");
		}
		else
		{
			m_eSoundComponent.SoundEvent("SOUND_LOCKING_LOOP");
		}
	}

	override void TerminateLockOnAudio()
	{
		if(m_eSoundComponent && m_eLockAudioHandle != AudioHandle.Empty)
		{
			m_eSoundComponent.Terminate(m_eLockAudioHandle);
			m_eLockAudioHandle = AudioHandle.Empty;
		}
	}

	protected void UpdateDisplay()
	{
		if(!m_wDisplay)
			return;
			
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if(!workspace)
			return;
			
		int color = Color.WHITE;
		if(m_fCurrentLockProgress >= 1.0)
			color = Color.GREEN;
		else if(m_fCurrentLockProgress > 0.0)
			color = Color.YELLOW;

		if(m_wCross)
			m_wCross.SetColorInt(color);

		if(m_eTargetData && m_eTargetData.GetTargetEntity() && m_wSeekBox)
		{
			IEntity target = m_eTargetData.GetTargetEntity();
			vector minCorner, maxCorner;
			target.GetBounds(minCorner, maxCorner);
			
			vector worldCenter = target.GetOrigin();
			if(target.GetPhysics())
				worldCenter = target.CoordToParent(target.GetPhysics().GetCenterOfMass());
				
			vector screenPos = workspace.ProjWorldToScreen(worldCenter, GetGame().GetWorld());
			if(screenPos[2] > 0) // Visible in front of camera
			{
				float screenX = workspace.DPIUnscale(screenPos[0]);
				float screenY = workspace.DPIUnscale(screenPos[1]);
				
				float boxSize = 80.0;
				FrameSlot.SetPos(m_wSeekBox, screenX - (boxSize * 0.5), screenY - (boxSize * 0.5));
				FrameSlot.SetSize(m_wSeekBox, boxSize, boxSize);
				m_wSeekBox.SetVisible(true);
				m_wSeekBox.SetColorInt(color);
			}
			else
			{
				m_wSeekBox.SetVisible(false);
			}
		}
		else if(m_wSeekBox)
		{
			m_wSeekBox.SetVisible(false);
		}
	}

	override BGONE_TargetData GetCurrentTargetData()
	{
		if(m_fCurrentLockProgress >= 1.0)
			return m_eTargetData;
		return null;
	}
}
