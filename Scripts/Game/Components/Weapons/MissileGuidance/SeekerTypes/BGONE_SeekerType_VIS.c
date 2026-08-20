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
		if(flightTime > m_fTimeToLive)
		{
			if(m_eTargetData)
				m_eTargetData.detonated = 1;
			else
				targetData.detonated = 1;
			return targetData;
		}
				
		if(!targetData || !targetData.targetRplId.IsValid())
			return targetData;
			
		if(!target)
			target = targetData.GetTargetEntity();
			
		if(!target)
		{
			targetData.targetPosition = Vector(0,0,0);
			return targetData;
		}
		
		vector centerOfMass = vector.Zero;
		if(target.GetPhysics())
			centerOfMass = target.GetPhysics().GetCenterOfMass();
			
		vector centerPos;
		if(centerOfMass == vector.Zero)
			centerPos = target.GetOrigin() + vector.Up;
		else
			centerPos = target.CoordToParent(centerOfMass);
		
		vector targetPos = centerPos;
		
		if(!m_eProjectile || !m_eProjectile.GetPhysics())
			return targetData;
			
		// Check target within seeker angle and LOS to target is clear.
		bool seekerAnglesOk = CheckSeekerAngle(m_eProjectile.GetOrigin(), m_eProjectile.GetPhysics().GetVelocity(), targetPos);
		bool losToTarget = TraceLOS(m_eProjectile.GetOrigin(), targetPos);
		if(!seekerAnglesOk || !losToTarget)
		{
			if(flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime)
				targetData.detonated = 1;
			
			targetData.targetPosition = Vector(0,0,0);
			m_eTargetData = targetData;
			return m_eTargetData;
		}
		m_fTargetLastSeenTime = flightTime;
		
		// Calculate lead
		float projectileSpeed = m_eProjectile.GetPhysics().GetVelocity().Length();
		if(projectileSpeed < 1.0)
			projectileSpeed = 150.0;
			
		float distanceToTarget = vector.Distance(targetPos, m_eProjectile.GetOrigin());
		float timeToHit = distanceToTarget / projectileSpeed;
		
		vector targetVel = vector.Zero;
		if(target.GetPhysics())
			targetVel = target.GetPhysics().GetVelocity();
			
		vector calculatedLead = targetVel * timeToHit;

		targetData.targetPosition = targetPos + calculatedLead;
		m_eTargetData = targetData;
		return m_eTargetData;
	}
	
	protected bool TraceLOS(vector from, vector to)
	{	
		if(!target || !m_eProjectile)
			return false;
			
		ref array<IEntity> exclude = {m_eProjectile, target, target.GetRootParent() };
		TraceParam param = new TraceParam;
		param.Start = from;
		param.End = to;
		param.LayerMask = EPhysicsLayerDefs.Projectile;
		param.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS; 
		param.ExcludeArray = exclude;

		World world = GetGame().GetWorld();
		if(!world)
			return false;
			
		float percent = world.TraceMove(param, null);
		if (percent == 1)
			return true;
				
		return false;
	}
	
	protected bool CheckSeekerAngle(vector seekerPos, vector seekerDirection, vector targetPos)
	{
		vector testDir = vector.Direction(seekerPos, targetPos);
		if(testDir.Length() < 0.001)
			return true;
			
		vector testPointVector = testDir.Normalized();
		vector seekerDir = vector.Zero;
		if(seekerDirection.Length() > 0.001)
			seekerDir = seekerDirection.Normalized();
		else if(m_eProjectile)
			seekerDir = m_eProjectile.GetYawPitchRoll().AnglesToVector();
			
		float testDotProduct = vector.Dot(seekerDir, testPointVector);
		return testDotProduct > Math.Cos(m_fSeekerFieldOfView * Math.DEG2RAD);
	}
};
