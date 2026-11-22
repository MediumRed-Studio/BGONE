
[BaseContainerProps()]
class BGONE_SeekerType_VIS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How Many Seconds Until The Missile Self Destructs.","0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;
	
	[Attribute("30", UIWidgets.Slider, "Field Of View For The Seeker In Degrees.","0 360 1", category: "BGONE")]
	protected float m_fSeekerFieldOfView;
	
	[Attribute("2", UIWidgets.Slider, "How Many Seconds After Target Is Lost Until The Missile Self Destructs.","0 100 0.1", category: "BGONE")]
	protected float m_fNoTargetDestructTime;
	
	protected ref BGONE_TargetData m_eTargetData;
	protected IEntity target;
	protected float m_fTargetLastSeenTime;
	
	
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_eTargetData = targetData;
	}
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!targetData)
			return null;

		m_eTargetData = targetData;

		if (flightTime > m_fTimeToLive)
		{
			m_eTargetData.detonated = 1;
			return m_eTargetData;
		}
				
		if (!targetData.targetRplId.IsValid())
			return targetData;

		if (!target)
			target = targetData.GetTargetEntity();
		if (!target || !m_eProjectile)
			return targetData;

		Physics projPhys = m_eProjectile.GetPhysics();
		if (!projPhys)
			return targetData;

		Physics targetPhys = target.GetPhysics();
		vector centerOfMass = vector.Zero;
		if (targetPhys)
			centerOfMass = targetPhys.GetCenterOfMass();

		vector centerPos;
		if (centerOfMass == vector.Zero)
			centerPos = target.GetOrigin() + vector.Up;
		else
			centerPos = target.GetOrigin() + centerOfMass;
		
		vector targetPos = centerPos;
		
		// Check target within seeker angle and LOS to target is clear.
		bool seekerAnglesOk = CheckSeekerAngle(m_eProjectile.GetOrigin(), projPhys.GetVelocity(), targetPos);
		bool losToTarget = TraceLOS(m_eProjectile.GetOrigin(), targetPos);
		if (!seekerAnglesOk || !losToTarget)
		{
			if (flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime)
				m_eTargetData.detonated = 1;
			
			m_eTargetData.targetPosition = Vector(0,0,0);
			return m_eTargetData;
		}
		m_fTargetLastSeenTime = flightTime;
		
		// Calculate lead
		float projectileSpeed = projPhys.GetVelocity().Length();
		vector calulatedLead = vector.Zero;
		if (projectileSpeed > 0.01 && targetPhys)
		{
			float distanceToTarget = vector.Distance(targetPos, m_eProjectile.GetOrigin());
			float timeToHit = distanceToTarget / projectileSpeed;
			calulatedLead = targetPhys.GetVelocity() * timeToHit;
		}

		targetData.targetPosition = targetPos + calulatedLead;
			
		m_eTargetData = targetData;
		return m_eTargetData;
	}
	
	protected bool TraceLOS(vector from, vector to)
	{						
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		ref array<IEntity> exclude = {m_eProjectile, target};
		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.LayerMask = EPhysicsLayerDefs.Projectile;
		param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS; 
		param.ExcludeArray = exclude;

		float percent = world.TraceMove(param, null);
		if (percent == 1)	// If trace travels the entire path, return true
			return true;
				
		return false;
	}
	protected bool CheckSeekerAngle(vector seekerPos, vector seekerDirection, vector targetPos)
	{
		vector testPointVector = vector.Direction(seekerPos, targetPos).Normalized();
		float testDotProduct = vector.Dot(seekerDirection.Normalized(), testPointVector);
		
		return testDotProduct > Math.Cos(m_fSeekerFieldOfView * Math.DEG2RAD);
	}
	
};
