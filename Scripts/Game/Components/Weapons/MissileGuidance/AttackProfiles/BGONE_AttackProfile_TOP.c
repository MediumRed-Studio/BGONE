[BaseContainerProps()]
class BGONE_AttackProfile_TOP : BGONE_AttackProfile_Base
{
	[Attribute("160", UIWidgets.Slider, "Cruise Altitude (Meters) - Scaled Down When Fired Close Range", "0 500 1", category: "BGONE")]
	protected int m_iCruiseAltitude;
	
	[Attribute("1000", UIWidgets.Slider, "Distance To Target (Meters) Where Missile Reaches Full Cruise Altitude", "0 2000 1", category: "BGONE")]
	protected int m_iCruiseAltitudeRange;

	protected int m_iStage = 0;
	protected vector m_vGroundTargetPos = Vector(0,0,0);
	
	void BGONE_AttackProfile_TOP()
	{
		m_sProfileName = "Top Attack";
	}

	override void InitAttackMode(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitAttackMode(projectile, targetData);
		m_iStage = 0;
		if(targetData)
			m_vGroundTargetPos = targetData.targetPosition;
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || !m_eProjectile)
			return targetData;
		
		// Always update ground target base position from target data (or entity if available)
		IEntity targetEnt = targetData.GetTargetEntity();
		if(targetEnt)
		{
			if(targetEnt.GetPhysics())
				m_vGroundTargetPos = targetEnt.CoordToParent(targetEnt.GetPhysics().GetCenterOfMass());
			else
				m_vGroundTargetPos = targetEnt.GetOrigin();
		}
		else if(targetData.targetPosition != Vector(0,0,0) && m_vGroundTargetPos == Vector(0,0,0))
		{
			m_vGroundTargetPos = targetData.targetPosition;
		}

		if(m_vGroundTargetPos == Vector(0,0,0))
			return targetData;

		vector currentPos = m_eProjectile.GetOrigin();
		vector toGroundTarget = m_vGroundTargetPos - currentPos;
		float distanceToGroundTarget = toGroundTarget.Length();
		float distanceFromLaunch = GetDistanceFromLaunch(targetData);
		
		// Scale cruise altitude for short ranges
		float scaledCruiseAltitude = m_iCruiseAltitude;
		float rangeFraction = (distanceFromLaunch + distanceToGroundTarget) / Math.Max(m_iCruiseAltitudeRange, 1.0);
		if(rangeFraction < 1.0)
			scaledCruiseAltitude = m_iCruiseAltitude * rangeFraction;
			
		float altitudeDiff = currentPos[1] - m_vGroundTargetPos[1];
		
		// State machine for flight profile
		switch(m_iStage)
		{
			case 0:
			{
				// Initial launch climb
				if(distanceFromLaunch > 10.0)
					m_iStage = 1;
					
				targetData.targetPosition = m_vGroundTargetPos + (Vector(0, 1, 0) * (distanceToGroundTarget * 2.0));
				break;
			}
			case 1:
			{
				// Cruise climb
				if(altitudeDiff >= scaledCruiseAltitude)
				{
					if(rangeFraction < 1.0)
						m_iStage = 3; // Short range: dive immediately
					else
						m_iStage = 2; // Level cruise
				}
				
				targetData.targetPosition = m_vGroundTargetPos + (Vector(0, 1, 0) * (distanceToGroundTarget * 1.5));
				break;
			}
			case 2:
			{
				// Level cruise
				if(distanceToGroundTarget <= (altitudeDiff * 2.0))
				{
					m_iStage = 3; // Transition to terminal dive
				}
				
				targetData.targetPosition = m_vGroundTargetPos + (Vector(0, 1, 0) * scaledCruiseAltitude);
				break;
			}
			case 3:
			{
				// Terminal dive directly onto target
				targetData.targetPosition = m_vGroundTargetPos;
				break;
			}
		}
		
		return targetData;
	}
}
