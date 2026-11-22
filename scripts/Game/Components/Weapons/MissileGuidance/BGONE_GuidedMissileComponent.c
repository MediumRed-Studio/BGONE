[ComponentEditorProps(category: "GameScripted/Weapons/BGONE", description: "")]
class BGONE_GuidedMissileComponentClass : ScriptComponentClass
{
};

class BGONE_GuidedMissileComponent : ScriptComponent
{
	static const float SYNC_INTERVAL_SEC = 0.05;
	static const float PROXY_SNAP_DISTANCE_SQ = 4.0;
	static const float PROXY_CORRECTION_GAIN = 6.0;
	static const float PROXY_MAX_CORRECTION_SPEED = 45.0;
	static const float PROXY_MAX_EXTRAPOLATION_SEC = 0.25;
	static const int LOCAL_COPY_CLEANUP_DELAY_MS = 20;

	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Updating The Target During Flight - Also Handles Proximity Detonation", category: "BGONE")]
	protected ref BGONE_SeekerType_Base m_eSeekerTypeComponent;

	[Attribute("", UIWidgets.Object, desc: "Scripts Responsible For Adjusting The Attack Mode Of The Missile", category: "BGONE")]
	protected ref array<ref BGONE_AttackProfile_Base> m_eAttackProfileComponents;

	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Physically Moving The Missile", category: "BGONE")]
	protected ref BGONE_MissileEngine_Base m_eMissileEngineComponent;

	protected int m_iArmingDistanceIndex = 0;
	protected int m_eAttackProfileComponentIndex = 0;
	protected Projectile m_eOwner;
	protected MissileMoveComponent m_MoveComponent;
	protected BaseTriggerComponent m_TriggerComponent;
	protected RplComponent m_RplComponent;
	protected float m_fFlightTime;
	protected bool m_bGuidanceActive = false;
	protected bool m_bDetonated = false;
	protected int m_iDetonationCount = 0;
	protected bool m_bIsLocalPredictedCopy = false;
	protected bool m_bLocalCleanupScheduled = false;
	protected ref BGONE_TargetData m_eCurrentTargetData;
	protected ref Instigator m_CachedInstigator;
	BGONE_GuidedMissileLauncherComponent launcherComp;
	protected vector m_vLastTargetPosition = vector.Zero;
	protected vector m_vLaunchOrigin = vector.Zero;
	protected vector m_vLaunchForward = vector.Zero;

	// Throttled server -> proxy flight replication state.
	protected float m_fSyncAccumulator = 0.0;
	protected bool m_bSentInitialSync = false;
	protected int m_iSyncSendCount = 0;

	// Proxy dead-reckoning state.
	protected bool m_bHasProxySync = false;
	protected vector m_vSyncedPos = vector.Zero;
	protected vector m_vSyncedVel = vector.Zero;
	protected float m_fProxySyncAge = 0.0;
	protected int m_iProxyFrameTicks = 0;
	protected int m_iSyncReceiveCount = 0;
	protected float m_fLastSyncErrorMeters = 0.0;
	protected float m_fMaxSyncErrorMeters = 0.0;
	protected int m_iSnapCorrectionCount = 0;

	// Flight telemetry.
	protected float m_fTravelDistanceMeters = 0.0;
	protected float m_fLastSpeed = 0.0;
	protected vector m_vLastKnownOrigin = vector.Zero;

	protected float m_fSaclosFixNextUpdateTime = 0;
	protected float m_fSaclosFixUpdateInterval = 50;

	// Enable for debug lines of target pos as missile flies.
	protected bool m_bDebugEnabled = false;
	protected ref array<ref Shape> m_aDbgCollisionShapes;
	protected vector m_vPreviousTargetPos;

	// Process-wide read-only telemetry tracker (survives post-detonation entity deletion).
	protected static BGONE_GuidedMissileComponent s_LastTrackedMissile;
	protected static bool s_bHasLastTracked;
	protected static RplId s_LastTrackedRplId;
	protected static int s_iLaunchEpoch;
	protected static float s_fLastFlightTime;
	protected static float s_fLastTravelDistance;
	protected static float s_fLastSpeed;
	protected static bool s_bLastGuidanceActive;
	protected static bool s_bLastDetonated;
	protected static int s_iLastDetonationCount;
	protected static int s_iLastSyncSendCount;
	protected static int s_iLastSyncReceiveCount;
	protected static float s_fLastSyncErrorMeters;
	protected static float s_fLastMaxSyncErrorMeters;
	protected static int s_iLastSnapCorrectionCount;
	protected static int s_iLastProxyFrameTicks;
	protected static vector s_vLastOrigin;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		ResolveOwnerComponents(owner);
		if (!m_eOwner)
			return;

		SetEventMask(m_eOwner, EntityEvent.INIT | EntityEvent.FRAME);

		if (IsClientLocalPredictedCopy())
		{
			m_bIsLocalPredictedCopy = true;
			if (!m_eOwner.GetParent())
				ScheduleLocalPredictedCopyCleanup();

			return;
		}

		if (launcherComp)
			m_aDbgCollisionShapes = new array<ref Shape>();
	}

	//------------------------------------------------------------------------------------------------
	override void OnRemovedFromParent(IEntity child, IEntity parent)
	{
		super.OnRemovedFromParent(child, parent);

		if (!m_eOwner)
			ResolveOwnerComponents(child);

		if (IsClientLocalPredictedCopy())
		{
			m_bIsLocalPredictedCopy = true;
			ScheduleLocalPredictedCopyCleanup();
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
		{
			queue.Remove(DeleteLocalPredictedCopy);
			queue.Remove(ExplodeWrapper);
		}

		if (!m_bIsLocalPredictedCopy && !m_bDetonated && m_TriggerComponent && m_TriggerComponent.WasTriggered())
		{
			m_bDetonated = true;
			m_bGuidanceActive = false;
			m_iDetonationCount = m_iDetonationCount + 1;
			UpdateStaticTelemetry();
		}

		if (s_LastTrackedMissile == this)
		{
			UpdateStaticTelemetry();
			s_LastTrackedMissile = null;
		}

		m_eCurrentTargetData = null;
		m_CachedInstigator = null;
		m_aDbgCollisionShapes = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void ResolveOwnerComponents(IEntity inOwner)
	{
		if (!inOwner)
			return;

		if (!m_eOwner)
			m_eOwner = Projectile.Cast(inOwner);

		if (!m_eOwner)
			return;

		if (!m_RplComponent)
			m_RplComponent = RplComponent.Cast(m_eOwner.FindComponent(RplComponent));

		if (!m_MoveComponent)
			m_MoveComponent = MissileMoveComponent.Cast(m_eOwner.FindComponent(MissileMoveComponent));

		if (!m_TriggerComponent)
			m_TriggerComponent = BaseTriggerComponent.Cast(m_eOwner.FindComponent(BaseTriggerComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! Returns true only on a remote MP client for a locally-spawned predicted projectile copy
	//! (not a replicated proxy from the server).
	protected bool IsClientLocalPredictedCopy()
	{
		if (!Replication.IsRunning() || !Replication.IsClient())
			return false;

		if (!m_RplComponent)
			return false;

		if (m_RplComponent.IsProxy())
			return false;

		return m_RplComponent.Role() == RplRole.Authority;
	}

	//------------------------------------------------------------------------------------------------
	protected void ScheduleLocalPredictedCopyCleanup()
	{
		if (m_bLocalCleanupScheduled || !m_eOwner)
			return;

		m_bLocalCleanupScheduled = true;
		ClearEventMask(m_eOwner, EntityEvent.FRAME | EntityEvent.SIMULATE);

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.CallLater(DeleteLocalPredictedCopy, LOCAL_COPY_CLEANUP_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteLocalPredictedCopy()
	{
		if (!m_eOwner)
			return;

		// Never delete a round still seated inside a launcher/magazine, or a server/proxy entity.
		if (m_eOwner.GetParent())
			return;

		if (!IsClientLocalPredictedCopy())
			return;

		if (m_RplComponent && m_RplComponent.Id().IsValid())
			RplComponent.DeleteRplEntity(m_eOwner, false);
		else
			SCR_EntityHelper.DeleteEntityAndChildren(m_eOwner);
	}

	//------------------------------------------------------------------------------------------------
	protected vector GetCurrentVelocity()
	{
		if (m_MoveComponent)
			return m_MoveComponent.GetVelocity();

		if (m_eOwner)
		{
			Physics phys = m_eOwner.GetPhysics();
			if (phys)
				return phys.GetVelocity();
		}

		return vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	protected void SetCurrentVelocity(vector inVel)
	{
		if (m_MoveComponent)
		{
			m_MoveComponent.SetVelocity(inVel);
			return;
		}

		if (m_eOwner)
		{
			Physics phys = m_eOwner.GetPhysics();
			if (phys)
				phys.SetVelocity(inVel);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SetCurrentAngularVelocity(vector inAngVel)
	{
		if (m_MoveComponent)
		{
			m_MoveComponent.SetAngularVelocity(inAngVel);
			return;
		}

		if (m_eOwner)
		{
			Physics phys = m_eOwner.GetPhysics();
			if (phys)
				phys.SetAngularVelocity(inAngVel);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected Instigator ResolveLaunchInstigator(BGONE_TargetData inTargetData, BGONE_GuidedMissileLauncherComponent inLauncher)
	{
		if (inTargetData)
		{
			SCR_ChimeraCharacter shooter = inTargetData.GetShooterEntity();
			if (shooter)
				return Instigator.CreateInstigator(shooter);
		}

		if (inLauncher)
		{
			IEntity launcherOwner = inLauncher.GetCurrentOwnerEntity();
			if (launcherOwner)
				return Instigator.CreateInstigator(launcherOwner);
		}

		if (m_MoveComponent)
		{
			Instigator moveInstigator = m_MoveComponent.GetInstigator();
			if (moveInstigator)
				return moveInstigator;
		}

		if (m_TriggerComponent)
		{
			Instigator trigInstigator = m_TriggerComponent.GetInstigator();
			if (trigInstigator)
				return trigInstigator;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected vector ResolveForwardDirection()
	{
		vector vel = GetCurrentVelocity();
		if (vel.LengthSq() > 0.0001)
			return vel.Normalized();

		if (m_eOwner)
		{
			vector fwd = m_eOwner.GetWorldTransformAxis(2);
			if (fwd.LengthSq() > 0.0001)
				return fwd.Normalized();
		}

		if (m_vLaunchForward.LengthSq() > 0.0001)
			return m_vLaunchForward.Normalized();

		return Vector(0, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void RegisterAsLastTracked()
	{
		if (m_bIsLocalPredictedCopy)
			return;

		s_LastTrackedMissile = this;
		s_bHasLastTracked = true;
		s_iLaunchEpoch = s_iLaunchEpoch + 1;
		if (m_RplComponent && m_RplComponent.Id().IsValid())
			s_LastTrackedRplId = m_RplComponent.Id();
		else
			s_LastTrackedRplId = RplId.Invalid();

		UpdateStaticTelemetry();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateStaticTelemetry()
	{
		if (m_bIsLocalPredictedCopy || s_LastTrackedMissile != this)
			return;

		if (m_eOwner)
			m_vLastKnownOrigin = m_eOwner.GetOrigin();

		if (m_RplComponent && m_RplComponent.Id().IsValid())
			s_LastTrackedRplId = m_RplComponent.Id();

		s_fLastFlightTime = m_fFlightTime;
		s_fLastTravelDistance = m_fTravelDistanceMeters;
		s_fLastSpeed = m_fLastSpeed;
		s_bLastGuidanceActive = m_bGuidanceActive;
		s_bLastDetonated = m_bDetonated;
		s_iLastDetonationCount = m_iDetonationCount;
		s_iLastSyncSendCount = m_iSyncSendCount;
		s_iLastSyncReceiveCount = m_iSyncReceiveCount;
		s_fLastSyncErrorMeters = m_fLastSyncErrorMeters;
		s_fLastMaxSyncErrorMeters = m_fMaxSyncErrorMeters;
		s_iLastSnapCorrectionCount = m_iSnapCorrectionCount;
		s_iLastProxyFrameTicks = m_iProxyFrameTicks;
		s_vLastOrigin = m_vLastKnownOrigin;
	}

	//------------------------------------------------------------------------------------------------
	void SetAttackAndFireModes(int attackModeIndex, int armingDistanceIndex)
	{
		m_eAttackProfileComponentIndex = attackModeIndex;
		m_iArmingDistanceIndex = armingDistanceIndex;
	}

	//------------------------------------------------------------------------------------------------
	// Only ran on server
	array<ref Shape> onLaunched(BGONE_TargetData targetData, BGONE_GuidedMissileLauncherComponent launcher)
	{
		if (!m_eOwner)
			ResolveOwnerComponents(GetOwner());

		if (!targetData || !m_eOwner || !m_eSeekerTypeComponent || !m_eAttackProfileComponents)
			return m_aDbgCollisionShapes;

		int profileIdx = targetData.attackProfileIndex;
		if (profileIdx < 0 || profileIdx >= m_eAttackProfileComponents.Count() || !m_eAttackProfileComponents[profileIdx])
			return m_aDbgCollisionShapes;

		launcherComp = launcher;
		m_eCurrentTargetData = targetData;

		vector initForward = ResolveForwardDirection();
		m_vLaunchForward = initForward;

		if (m_eCurrentTargetData.launchPos == vector.Zero)
			m_eCurrentTargetData.launchPos = m_eOwner.GetOrigin();

		m_vLaunchOrigin = m_eCurrentTargetData.launchPos;
		m_vLastKnownOrigin = m_eOwner.GetOrigin();
		m_vLastTargetPosition = vector.Zero;

		// Safe unlocked launch initialization: never allow zero launchDir to steer toward world origin or North.
		if (!m_eCurrentTargetData.targetRplId.IsValid()
			&& m_eCurrentTargetData.targetPosition == vector.Zero
			&& m_eCurrentTargetData.launchDir == vector.Zero)
		{
			m_eCurrentTargetData.launchDir = initForward;
		}

		m_CachedInstigator = ResolveLaunchInstigator(m_eCurrentTargetData, launcherComp);

		// Send initial data to seeker and attack profile.
		SetAttackAndFireModes(m_eCurrentTargetData.attackProfileIndex, m_eCurrentTargetData.armingDistancesIndex);
		m_eSeekerTypeComponent.InitSeeker(m_eOwner, m_eCurrentTargetData);
		m_eAttackProfileComponents[profileIdx].InitAttackMode(m_eOwner);

		m_fFlightTime = 0.0;
		m_fSyncAccumulator = 0.0;
		m_bSentInitialSync = false;
		m_iSyncSendCount = 0;
		m_bDetonated = false;
		m_iDetonationCount = 0;
		m_fTravelDistanceMeters = 0.0;
		m_fLastSpeed = GetCurrentVelocity().Length();
		m_bGuidanceActive = true;

		// Pre-loaded tube rounds (M72A3-based) require EntityFlags.ACTIVE + EntityEvent.FRAME explicitly on launch.
		m_eOwner.SetFlags(EntityFlags.ACTIVE, false);
		if (!IsActive())
			Activate(m_eOwner);
		SetEventMask(m_eOwner, EntityEvent.INIT | EntityEvent.FRAME);
		if (m_MoveComponent)
			m_MoveComponent.EnableSimulation(m_eOwner);
		RegisterAsLastTracked();

		return m_aDbgCollisionShapes;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (m_bIsLocalPredictedCopy || m_bDetonated || timeSlice <= 0.0001)
			return;

		if (!m_eOwner)
			ResolveOwnerComponents(owner);

		if (!m_eOwner)
			return;

		if (m_RplComponent && m_RplComponent.IsProxy())
		{
			UpdateProxyDeadReckoning(timeSlice);
			return;
		}

		if (m_RplComponent && m_RplComponent.Role() != RplRole.Authority)
			return;

		if (!m_bGuidanceActive || !m_eCurrentTargetData || !m_eSeekerTypeComponent)
			return;

		// Record collision detonation if vanilla CollisionTriggerComponent already fired.
		if (m_TriggerComponent && m_TriggerComponent.WasTriggered())
		{
			m_bDetonated = true;
			m_bGuidanceActive = false;
			m_iDetonationCount = m_iDetonationCount + 1;
			UpdateStaticTelemetry();
			return;
		}

		m_fFlightTime = m_fFlightTime + timeSlice;
		m_fSyncAccumulator = m_fSyncAccumulator + timeSlice;

		vector currentOrigin = m_eOwner.GetOrigin();
		m_vLastKnownOrigin = currentOrigin;
		float distFromLaunch = vector.Distance(m_vLaunchOrigin, currentOrigin);
		if (distFromLaunch > m_fTravelDistanceMeters)
			m_fTravelDistanceMeters = distFromLaunch;

		// Pass Data To Seeker, Get Adjusted data back.
		m_eCurrentTargetData = m_eSeekerTypeComponent.ProcessFrame(m_eCurrentTargetData, m_fFlightTime);
		if (!m_eCurrentTargetData)
			return;

		// SACLOS fix
		if (m_eSeekerTypeComponent.Type() == BGONE_SeekerType_SACLOS)
		{
			BaseWorld world = GetGame().GetWorld();
			if (world && world.GetWorldTime() > m_fSaclosFixNextUpdateTime)
			{
				m_fSaclosFixNextUpdateTime = world.GetWorldTime() + m_fSaclosFixUpdateInterval;
				if (launcherComp)
					launcherComp.SaclosFix();
			}
		}

		// Use last target position if seeker lost track of target.
		if (m_eCurrentTargetData.targetPosition != vector.Zero)
			m_vLastTargetPosition = m_eCurrentTargetData.targetPosition;
		else if (m_vLastTargetPosition != vector.Zero)
			m_eCurrentTargetData.targetPosition = m_vLastTargetPosition;

		if (m_eCurrentTargetData.detonated > 0)
		{
			DetonateServer(m_eCurrentTargetData.detonated == 2);
			return;
		}

		// Pass Data To AttackMode, Get Adjusted Data Back.
		int profileIdx = m_eCurrentTargetData.attackProfileIndex;
		if (!m_eAttackProfileComponents || profileIdx < 0 || profileIdx >= m_eAttackProfileComponents.Count() || !m_eAttackProfileComponents[profileIdx])
			return;

		m_eCurrentTargetData = m_eAttackProfileComponents[profileIdx].ProcessFrame(m_eCurrentTargetData, m_fFlightTime);
		if (!m_eCurrentTargetData)
			return;

		if (m_bDebugEnabled)
		{
			if (!m_aDbgCollisionShapes)
				m_aDbgCollisionShapes = new array<ref Shape>();

			if (m_vPreviousTargetPos == vector.Zero)
				m_vPreviousTargetPos = m_eCurrentTargetData.launchPos;

			Debug_DrawLineSimple(m_vPreviousTargetPos, m_eCurrentTargetData.targetPosition, m_aDbgCollisionShapes);
			m_vPreviousTargetPos = m_eCurrentTargetData.targetPosition;
		}

		// Pass Data To Engine, moving missile.
		if (m_fFlightTime > 0.5 && m_eMissileEngineComponent)
		{
			vector engineTargetPos = m_eCurrentTargetData.targetPosition;
			if (engineTargetPos == vector.Zero)
				engineTargetPos = currentOrigin + ResolveForwardDirection() * 10.0;

			if (m_eMissileEngineComponent.ProcessFrame(m_eOwner, engineTargetPos, m_fFlightTime, timeSlice))
			{
				m_eCurrentTargetData.detonated = 1;
				BGONE_TargetData resolvedDet = m_eSeekerTypeComponent.ProcessFrame(m_eCurrentTargetData, m_fFlightTime);
				if (resolvedDet)
					m_eCurrentTargetData = resolvedDet;

				if (m_eCurrentTargetData.detonated <= 0)
					m_eCurrentTargetData.detonated = 1;

				DetonateServer(m_eCurrentTargetData.detonated == 2);
				return;
			}
		}

		vector velocity = GetCurrentVelocity();
		m_fLastSpeed = velocity.Length();

		// Throttled ~20 Hz broadcast replication to proxy clients.
		if (Replication.IsRunning() && (!m_bSentInitialSync || m_fSyncAccumulator >= SYNC_INTERVAL_SEC))
		{
			m_bSentInitialSync = true;
			m_fSyncAccumulator = 0.0;
			m_iSyncSendCount = m_iSyncSendCount + 1;
			Rpc(RpcDo_SyncFlight, m_eOwner.GetOrigin(), velocity);
		}

		UpdateStaticTelemetry();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateProxyDeadReckoning(float inTimeSlice)
	{
		if (!m_bHasProxySync || m_bDetonated || !m_eOwner)
			return;

		if (m_TriggerComponent && m_TriggerComponent.WasTriggered())
		{
			m_bDetonated = true;
			m_iDetonationCount = m_iDetonationCount + 1;
			UpdateStaticTelemetry();
			return;
		}

		m_iProxyFrameTicks = m_iProxyFrameTicks + 1;
		m_fFlightTime = m_fFlightTime + inTimeSlice;
		m_fProxySyncAge = m_fProxySyncAge + inTimeSlice;

		bool staleSync = m_fProxySyncAge >= PROXY_MAX_EXTRAPOLATION_SEC;
		float clampedAge = m_fProxySyncAge;
		if (staleSync)
			clampedAge = PROXY_MAX_EXTRAPOLATION_SEC;

		vector targetProxyPos = m_vSyncedPos + m_vSyncedVel * clampedAge;
		vector curOrigin = m_eOwner.GetOrigin();

		vector feedForwardVel = m_vSyncedVel;
		if (staleSync)
			feedForwardVel = vector.Zero;

		// EOnFrame runs before native MissileMoveComponent integrates this frame's velocity, so
		// curOrigin is at t while targetProxyPos is at t + inTimeSlice. Compare at t + inTimeSlice
		// and let MissileMoveComponent integrate desiredVel once (never double-integrate via SetOrigin).
		vector referenceOrigin = curOrigin + feedForwardVel * inTimeSlice;
		vector posError = targetProxyPos - referenceOrigin;
		float effectiveGain = PROXY_CORRECTION_GAIN;
		float maxSafeGain = 0.5 / inTimeSlice;
		if (effectiveGain > maxSafeGain)
			effectiveGain = maxSafeGain;

		vector correctionVel = SCR_Math3D.ClampMagnitude(posError * effectiveGain, PROXY_MAX_CORRECTION_SPEED);
		vector desiredVel = feedForwardVel + correctionVel;

		SetCurrentVelocity(desiredVel);
		m_fLastSpeed = desiredVel.Length();

		if (desiredVel.LengthSq() > 0.01)
		{
			vector desiredAngles = desiredVel.VectorToAngles();
			vector curAngles = m_eOwner.GetYawPitchRoll();
			vector angErr = SCR_Math3D.FixEulerVector180(desiredAngles - curAngles);
			vector angularVel = SCR_Math3D.GetFixedAxisVector(angErr) * (Math.DEG2RAD / inTimeSlice);
			angularVel = SCR_Math3D.ClampMagnitude(angularVel, BGONE_MissileEngine_Base.MAX_ANGULAR_SPEED_RAD);
			SetCurrentAngularVelocity(angularVel);
		}
		else
		{
			SetCurrentAngularVelocity(vector.Zero);
		}

		m_vLastKnownOrigin = curOrigin;
		float distFromStart = vector.Distance(m_vLaunchOrigin, curOrigin);
		if (distFromStart > m_fTravelDistanceMeters)
			m_fTravelDistanceMeters = distFromStart;

		UpdateStaticTelemetry();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	protected void RpcDo_SyncFlight(vector position, vector velocity)
	{
		if (!m_eOwner)
			ResolveOwnerComponents(GetOwner());

		if (!m_eOwner || m_bDetonated || m_bIsLocalPredictedCopy)
			return;

		if (m_RplComponent && !m_RplComponent.IsProxy())
			return;

		bool isFirstSync = !m_bHasProxySync;
		if (isFirstSync)
		{
			m_vLaunchOrigin = position;
			RegisterAsLastTracked();
		}

		// Replicated proxy projectiles spawned inside a launcher start inactive; wake entity + component
		// so EOnFrame dead-reckoning ticks smoothly between 20 Hz updates.
		if ((m_eOwner.GetFlags() & EntityFlags.ACTIVE) == 0)
			m_eOwner.SetFlags(EntityFlags.ACTIVE, false);

		if (!IsActive())
			Activate(m_eOwner);

		if ((GetEventMask() & EntityEvent.FRAME) == 0)
			SetEventMask(m_eOwner, EntityEvent.INIT | EntityEvent.FRAME);

		vector curOrigin = m_eOwner.GetOrigin();
		float distSq = vector.DistanceSq(curOrigin, position);
		float distErr = Math.Sqrt(distSq);

		m_iSyncReceiveCount = m_iSyncReceiveCount + 1;
		m_fLastSyncErrorMeters = distErr;
		if (!isFirstSync && distErr > m_fMaxSyncErrorMeters)
			m_fMaxSyncErrorMeters = distErr;

		// Never unconditionally teleport via SetOrigin (kills RocketTraceEffectComponent trail).
		bool snapped = false;
		if (distSq > PROXY_SNAP_DISTANCE_SQ)
		{
			m_eOwner.SetOrigin(position);
			m_iSnapCorrectionCount = m_iSnapCorrectionCount + 1;
			snapped = true;
		}

		if (isFirstSync || snapped)
		{
			// Ensure proxy MissileMoveComponent simulation is enabled even if the client missed
			// native launch activation (e.g. remote observer seeing a pre-loaded tube round).
			if (m_MoveComponent)
				m_MoveComponent.EnableSimulation(m_eOwner);

			SetCurrentVelocity(velocity);
			if (velocity.LengthSq() > 0.01)
				m_eOwner.SetYawPitchRoll(velocity.VectorToAngles());
		}

		m_vSyncedPos = position;
		m_vSyncedVel = velocity;
		m_fProxySyncAge = 0.0;
		m_bHasProxySync = true;
		m_fLastSpeed = velocity.Length();
		m_vLastKnownOrigin = m_eOwner.GetOrigin();

		UpdateStaticTelemetry();
	}

	//------------------------------------------------------------------------------------------------
	void UpdateTurretAim(vector aimDir, vector aimPos)
	{
		if (Replication.IsServer())
		{
			ApplyAimingDirServer(aimDir, aimPos);
			return;
		}

		Rpc(RpcDo_UpdateAimingDir, aimDir, aimPos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcDo_UpdateAimingDir(vector aimDir, vector aimPos)
	{
		ApplyAimingDirServer(aimDir, aimPos);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAimingDirServer(vector aimDir, vector aimPos)
	{
		if (!Replication.IsServer() || m_bDetonated)
			return;

		BGONE_SeekerType_SACLOS seeker = BGONE_SeekerType_SACLOS.Cast(m_eSeekerTypeComponent);
		if (!seeker)
			return;

		seeker.UpdateAimingDirServer(aimDir, aimPos);
	}

	//------------------------------------------------------------------------------------------------
	protected void DetonateServer(bool down)
	{
		if (!m_eOwner || m_bDetonated)
			return;

		if (m_RplComponent && m_RplComponent.Role() != RplRole.Authority)
			return;

		m_bDetonated = true;
		m_bGuidanceActive = false;
		m_iDetonationCount = m_iDetonationCount + 1;

		vector explodePos = m_eOwner.GetOrigin();
		vector curVel = GetCurrentVelocity();
		if (curVel.LengthSq() > 0.0001)
			explodePos = explodePos + curVel.Normalized() * 1.5;

		m_eOwner.SetOrigin(explodePos);
		if (down)
		{
			vector curAngles = m_eOwner.GetYawPitchRoll();
			m_eOwner.SetYawPitchRoll(Vector(curAngles[0], -90.0, 0.0));
		}

		m_vLastKnownOrigin = explodePos;
		UpdateStaticTelemetry();

		if (Replication.IsRunning())
			Rpc(RpcDo_Explode, explodePos, down);

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.CallLater(ExplodeWrapper, 1, false);
		else
			ExplodeWrapper();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Explode(vector pos, bool down)
	{
		if (!m_eOwner)
			ResolveOwnerComponents(GetOwner());

		if (!m_eOwner || m_bDetonated || m_bIsLocalPredictedCopy)
			return;

		if (m_RplComponent && !m_RplComponent.IsProxy())
			return;

		if (!m_bHasProxySync)
			RegisterAsLastTracked();

		m_bDetonated = true;
		m_bGuidanceActive = false;
		m_iDetonationCount = m_iDetonationCount + 1;

		m_eOwner.SetOrigin(pos);
		if (down)
		{
			vector curAngles = m_eOwner.GetYawPitchRoll();
			m_eOwner.SetYawPitchRoll(Vector(curAngles[0], -90.0, 0.0));
		}

		m_vLastKnownOrigin = pos;
		UpdateStaticTelemetry();
		ExplodeWrapper();
	}

	//------------------------------------------------------------------------------------------------
	protected void ExplodeWrapper()
	{
		if (!m_eOwner)
			return;

		if (!m_TriggerComponent)
			m_TriggerComponent = BaseTriggerComponent.Cast(m_eOwner.FindComponent(BaseTriggerComponent));

		if (!m_TriggerComponent || m_TriggerComponent.WasTriggered())
			return;

		m_eOwner.Update();
		m_TriggerComponent.SetLive();

		Instigator instigator = m_CachedInstigator;
		if (!instigator)
			instigator = ResolveLaunchInstigator(m_eCurrentTargetData, launcherComp);

		if (instigator)
			m_TriggerComponent.OnUserTriggerOverrideInstigator(m_eOwner, instigator);
		else
			m_TriggerComponent.OnUserTrigger(m_eOwner);
	}

	//------------------------------------------------------------------------------------------------
	BGONE_AttackProfile_Base GetActiveAttackProfile()
	{
		if (!m_eAttackProfileComponents || m_eAttackProfileComponentIndex < 0 || m_eAttackProfileComponentIndex >= m_eAttackProfileComponents.Count())
			return null;

		return m_eAttackProfileComponents[m_eAttackProfileComponentIndex];
	}

	//------------------------------------------------------------------------------------------------
	// Read-only instance telemetry getters.
	float GetFlightTime() { return m_fFlightTime; }
	float GetTravelDistanceMeters() { return m_fTravelDistanceMeters; }
	float GetLastSpeed() { return m_fLastSpeed; }
	bool IsGuidanceActive() { return m_bGuidanceActive; }
	bool IsDetonated() { return m_bDetonated; }
	int GetDetonationCount() { return m_iDetonationCount; }
	int GetSyncSendCount() { return m_iSyncSendCount; }
	int GetSyncReceiveCount() { return m_iSyncReceiveCount; }
	float GetLastSyncErrorMeters() { return m_fLastSyncErrorMeters; }
	float GetMaxSyncErrorMeters() { return m_fMaxSyncErrorMeters; }
	int GetSnapCorrectionCount() { return m_iSnapCorrectionCount; }
	int GetProxyFrameTicks() { return m_iProxyFrameTicks; }
	vector GetLastKnownOrigin() { return m_vLastKnownOrigin; }

	//------------------------------------------------------------------------------------------------
	RplId GetMissileRplId()
	{
		if (m_RplComponent && m_RplComponent.Id().IsValid())
			return m_RplComponent.Id();

		return RplId.Invalid();
	}

	//------------------------------------------------------------------------------------------------
	// Read-only static telemetry getters for active / last missile.
	static BGONE_GuidedMissileComponent GetLastMissile() { return s_LastTrackedMissile; }
	static int GetLastLaunchEpoch() { return s_iLaunchEpoch; }
	static float GetLastTrackedFlightTime() { return s_fLastFlightTime; }
	static float GetLastTrackedTravelDistance() { return s_fLastTravelDistance; }
	static float GetLastTrackedSpeed() { return s_fLastSpeed; }
	static bool IsLastTrackedGuidanceActive() { return s_bLastGuidanceActive; }
	static bool IsLastTrackedDetonated() { return s_bLastDetonated; }
	static int GetLastTrackedDetonationCount() { return s_iLastDetonationCount; }
	static int GetLastTrackedSyncSendCount() { return s_iLastSyncSendCount; }
	static int GetLastTrackedSyncReceiveCount() { return s_iLastSyncReceiveCount; }
	static float GetLastTrackedSyncErrorMeters() { return s_fLastSyncErrorMeters; }
	static float GetLastTrackedMaxSyncErrorMeters() { return s_fLastMaxSyncErrorMeters; }
	static int GetLastTrackedSnapCorrectionCount() { return s_iLastSnapCorrectionCount; }
	static int GetLastTrackedProxyFrameTicks() { return s_iLastProxyFrameTicks; }
	static vector GetLastTrackedOrigin() { return s_vLastOrigin; }

	//------------------------------------------------------------------------------------------------
	static RplId GetLastMissileRplId()
	{
		if (!s_bHasLastTracked)
			return RplId.Invalid();

		return s_LastTrackedRplId;
	}

	//------------------------------------------------------------------------------------------------
	static void ResetLastTrackedTelemetry()
	{
		s_LastTrackedMissile = null;
		s_bHasLastTracked = false;
		s_LastTrackedRplId = RplId.Invalid();
		s_fLastFlightTime = 0.0;
		s_fLastTravelDistance = 0.0;
		s_fLastSpeed = 0.0;
		s_bLastGuidanceActive = false;
		s_bLastDetonated = false;
		s_iLastDetonationCount = 0;
		s_iLastSyncSendCount = 0;
		s_iLastSyncReceiveCount = 0;
		s_fLastSyncErrorMeters = 0.0;
		s_fLastMaxSyncErrorMeters = 0.0;
		s_iLastSnapCorrectionCount = 0;
		s_iLastProxyFrameTicks = 0;
		s_vLastOrigin = vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	// Debug Methods
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

	//------------------------------------------------------------------------------------------------
	private void Do_ClearDbgShapes()
	{
		if (m_aDbgCollisionShapes)
			m_aDbgCollisionShapes.Clear();
	}

	//------------------------------------------------------------------------------------------------
	override void OnAddedToParent(IEntity child, IEntity parent)
	{
		// Make sure all required components are registered
		if (!m_eSeekerTypeComponent || !m_eAttackProfileComponents || m_eAttackProfileComponents.Count() == 0 || !m_eMissileEngineComponent)
		{
			Print("BGONE Guided Missile Component is missing required sub-components!", LogLevel.ERROR);
			return;
		}
		if (!parent)
			return;

		launcherComp = BGONE_GuidedMissileLauncherComponent.Cast(parent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!launcherComp)
			return;
		if (m_eAttackProfileComponentIndex < 0 || m_eAttackProfileComponentIndex >= m_eAttackProfileComponents.Count() || !m_eAttackProfileComponents[m_eAttackProfileComponentIndex])
			return;

		launcherComp.SetAvailableAttackProfiles(m_eAttackProfileComponents);
		launcherComp.SetAvailableArmingDistances(m_eSeekerTypeComponent.GetAvailableArmingDistances());
	}
};
