[BaseContainerProps()]
class BGONE_SeekerType_VIS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How many seconds until the missile self destructs (ammo prefab overrides win; must match engine TTL + MissileMove TimeToLive there)", "0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	[Attribute("30", UIWidgets.Slider, "Seeker half-angle in degrees: target must stay within this deviation from the missile velocity vector (upstream parity values: 30 VIS / 60 SACLOS)", "0 90 0.1", category: "BGONE")]
	protected float m_fSeekerFOV;
	
	[Attribute("2.0", UIWidgets.Slider, "How many seconds after target is lost until the missile self destructs", "0 100 0.1", category: "BGONE")]
	protected float m_fNoTargetDestructTime;
	
	[Attribute("100", UIWidgets.Slider, "Min distance from launch before missile arms", "0 1000 1", category: "BGONE")]
	protected int m_iArmingDistance;
	
	protected const float PROXIMITY_RANGE = 3.0;
	
	protected float m_fTargetLastSeenTime = 0;
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;
	
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
		m_fTargetLastSeenTime = 0;
	}

	override array<int> GetAvailableArmingDistances()
	{
		return {m_iArmingDistance};
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || !m_eProjectile)
			return targetData;

		// Upstream TTL check
		if(m_fTimeToLive > 0 && flightTime > m_fTimeToLive)
		{
			targetData.detonated = EBGONE_DetonationState.IMPACT;
			return targetData;
		}
		
		if(!targetData.targetRplId.IsValid())
		{
			// Fired without a lock: origin behavior is an unguided coast
			// to TTL (no track to gate on). Kept as-is.
			return targetData;
		}
		
		IEntity target = targetData.GetTargetEntity();
		if(!target)
		{
			// No resolved target: coast on last transmitted position.
			// (Null guard is 1.8 crash-safety only; origin had none.)
			return targetData;
		}
		
		Physics targetPhys = target.GetPhysics();
		if(!targetPhys)
		{
			// No physics to aim at: same coast, no gate to run.
			return targetData;
		}
		
		vector centerOfMass = targetPhys.GetCenterOfMass();
		vector centerPos;
		if(centerOfMass == vector.Zero)
			centerPos = target.GetOrigin() + Vector(0, 1, 0);
		else
			centerPos = target.GetOrigin() + centerOfMass;
		
		vector projPos = m_eProjectile.GetOrigin();
		vector projVel = Vector(0,0,0);
		if(m_eProjectile.GetPhysics())
			projVel = m_eProjectile.GetPhysics().GetVelocity();
		
		// Upstream parity: the seeker only tracks while the target is inside
		// its FOV cone and line-of-sight is clear. Otherwise the no-target
		// timer runs and expiry self-destructs the missile. Lazy eval skips
		// the trace on FOV failure.
		if(!CheckSeekerAngle(projPos, projVel, centerPos) || !TraceLOS(projPos, centerPos, target, targetData.GetShooterEntity()))
		{
			if(flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime)
			{
				targetData.detonated = EBGONE_DetonationState.IMPACT;
				return targetData;
			}
			
			targetData.targetPosition = Vector(0,0,0);
			return targetData;
		}
		m_fTargetLastSeenTime = flightTime;
		
		// Armed proximity detonation around the target. Deliberately after
		// the gate: no fusing through walls or outside the seeker cone.
		if(GetDistanceFromLaunch(targetData) >= m_iArmingDistance)
		{
			if(vector.Distance(target.GetOrigin(), projPos) < PROXIMITY_RANGE)
			{
				targetData.detonated = EBGONE_DetonationState.IMPACT;
				return targetData;
			}
		}
		
		// Lead the target: center-of-mass aimpoint + velocity * time-to-hit.
		vector targetVel = targetPhys.GetVelocity();
		float projSpeed = projVel.Length();
		if(projSpeed < 10.0)
			projSpeed = 150.0;
		
		float timeToImpact = vector.Distance(centerPos, projPos) / projSpeed;
		targetData.targetPosition = centerPos + (targetVel * timeToImpact);
		
		return targetData;
	}
	
	// Upstream parity: target inside the seeker FOV cone around the missile
	// velocity vector. Skipped while nearly stationary (launch transient).
	protected bool CheckSeekerAngle(vector seekerPos, vector seekerDirection, vector targetPos)
	{
		if(seekerDirection.Length() < 0.01)
			return true;
		
		vector testPointVector = vector.Direction(seekerPos, targetPos).Normalized();
		float testDotProduct = vector.Dot(seekerDirection.Normalized(), testPointVector);
		
		return testDotProduct > Math.Cos(m_fSeekerFOV * Math.DEG2RAD);
	}

	protected bool TraceLOS(vector from, vector to, IEntity target, IEntity shooter = null)
	{
		if(vector.DistanceSq(from, to) < 0.01)
			return true;
			
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eProjectile);
		if(shooter)
		{
			m_aExcludeEntities.Insert(shooter);
			if(shooter.GetRootParent())
				m_aExcludeEntities.Insert(shooter.GetRootParent());
		}
		if(target)
		{
			m_aExcludeEntities.Insert(target);
			if(target.GetRootParent())
				m_aExcludeEntities.Insert(target.GetRootParent());
		}
		
		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		
		float fraction = GetGame().GetWorld().TraceMove(m_TraceParam, null);
		return (fraction >= 0.98);
	}
}
