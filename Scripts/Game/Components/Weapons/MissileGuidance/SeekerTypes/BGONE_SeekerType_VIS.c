[BaseContainerProps()]
class BGONE_SeekerType_VIS : BGONE_SeekerType_Base
{
	[Attribute("10", UIWidgets.Slider, "FOV in degrees the seeker can see the target in relation to its forward vector", "0 90 0.1", category: "BGONE")]
	protected float m_fSeekerFOV;
	
	[Attribute("100", UIWidgets.Slider, "Min distance from launch before missile arms", "0 1000 1", category: "BGONE")]
	protected int m_iArmingDistance;
	
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;
	
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
	}

	override array<int> GetAvailableArmingDistances()
	{
		return {m_iArmingDistance};
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData)
			return null;
			
		IEntity target = targetData.GetTargetEntity();
		if(!target)
		{
			return targetData;
		}

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
			centerOfMass = target.GetPhysics().GetCenterOfMass();
			
		vector centerPos = target.CoordToParent(centerOfMass);
		vector projPos = m_eProjectile.GetOrigin();
		vector toTarget = centerPos - projPos;
		float distToTarget = toTarget.Length();
		
		if(distToTarget < 0.001)
			return targetData;

		// FOV validation
		vector vel = Vector(0,0,1);
		if(m_eProjectile.GetPhysics())
			vel = m_eProjectile.GetPhysics().GetVelocity();
			
		if(vel.Length() > 0.01)
		{
			float dot = Math.Clamp(vector.Dot(vel.Normalized(), toTarget.Normalized()), -1.0, 1.0);
			float angle = Math.Acos(dot) * Math.RAD2DEG;
			if(angle > m_fSeekerFOV)
			{
				// Lost line of sight FOV
				return targetData;
			}
		}

		// Line of Sight check
		if(!TraceLOS(projPos, centerPos, target))
		{
			return targetData;
		}

		// Lead prediction
		vector targetVel = Vector(0,0,0);
		if(target.GetPhysics())
			targetVel = target.GetPhysics().GetVelocity();

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
