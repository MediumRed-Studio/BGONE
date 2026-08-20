[BaseContainerProps()]
class BGONE_SeekerType_VIS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How many seconds until the missile self destructs", "0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	[Attribute("30", UIWidgets.Slider, "FOV in degrees the seeker can see the target in relation to its forward vector", "0 90 0.1", category: "BGONE")]
	protected float m_fSeekerFOV;
	
	[Attribute("2.0", UIWidgets.Slider, "How many seconds after target is lost until the missile self destructs", "0 100 0.1", category: "BGONE")]
	protected float m_fNoTargetDestructTime;
	
	[Attribute("100", UIWidgets.Slider, "Min distance from launch before missile arms", "0 1000 1", category: "BGONE")]
	protected int m_iArmingDistance;
	
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
		if(!targetData)
			return null;

		// Upstream TTL check
		if(m_fTimeToLive > 0 && flightTime > m_fTimeToLive)
		{
			targetData.detonated = EBGONE_DetonationState.IMPACT;
			return targetData;
		}
			
		IEntity target = targetData.GetTargetEntity();
		vector centerPos = targetData.targetPosition;
		vector targetVel = Vector(0,0,0);
		
		if(target)
		{
			if(GetDistanceFromLaunch(targetData) >= m_iArmingDistance)
			{
				// Check proximity detonation
				if(vector.Distance(target.GetOrigin(), m_eProjectile.GetOrigin()) < 3.0)
				{
					targetData.detonated = EBGONE_DetonationState.IMPACT;
					return targetData;
				}
			}

			vector centerOfMass = Vector(0,0,0);
			if(target.GetPhysics())
			{
				centerOfMass = target.GetPhysics().GetCenterOfMass();
				targetVel = target.GetPhysics().GetVelocity();
			}
			centerPos = target.CoordToParent(centerOfMass);
		}
		else if(centerPos == Vector(0,0,0))
		{
			return targetData;
		}

		vector projPos = m_eProjectile.GetOrigin();
		vector toTarget = centerPos - projPos;
		float distToTarget = toTarget.Length();
		
		if(distToTarget < 0.001)
			return targetData;

		// FOV validation
		vector vel = Vector(0,0,1);
		if(m_eProjectile.GetPhysics())
			vel = m_eProjectile.GetPhysics().GetVelocity();
			
		bool angleOk = true;
		if(vel.Length() > 0.01)
		{
			float dot = Math.Clamp(vector.Dot(vel.Normalized(), toTarget.Normalized()), -1.0, 1.0);
			float angle = Math.Acos(dot) * Math.RAD2DEG;
			if(angle > m_fSeekerFOV)
				angleOk = false;
		}

		bool losOk = TraceLOS(projPos, centerPos, target);

		if(!angleOk || !losOk)
		{
			if(m_fNoTargetDestructTime > 0 && (flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime))
			{
				targetData.detonated = EBGONE_DetonationState.IMPACT;
				return targetData;
			}
			targetData.targetPosition = Vector(0,0,0);
			return targetData;
		}

		m_fTargetLastSeenTime = flightTime;

		// Lead prediction
		float projSpeed = vel.Length();
		if(projSpeed < 10.0)
			projSpeed = 150.0; // Fallback speed
			
		float timeToImpact = distToTarget / projSpeed;
		targetData.targetPosition = centerPos + (targetVel * timeToImpact);
		
		return targetData;
	}

	protected bool TraceLOS(vector from, vector to, IEntity target)
	{
		if(vector.DistanceSq(from, to) < 0.01)
			return true;
			
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eProjectile);
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
