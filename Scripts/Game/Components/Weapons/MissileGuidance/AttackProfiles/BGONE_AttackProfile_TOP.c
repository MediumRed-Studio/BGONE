[BaseContainerProps()]
class BGONE_AttackProfile_TOP : BGONE_AttackProfile_Base
{	
	[Attribute("160", UIWidgets.Slider, "Cruise Altitude", "0 1000 1", category: "BGONE")]
	protected int m_fCruiseAltitude;
	[Attribute("1250", UIWidgets.Slider, "How Far The Target Must Be From The Shooter For Cruise Altitude To Be Used", "0 10000 1", category: "BGONE")]
	protected int m_fCruiseAltitudeRange;
	
	protected int m_iCurrentStage = 0;
	
	void BGONE_AttackProfile_TOP()
	{
		m_cProfileName = "Top Attack";
	}
	
	override void InitAttackMode(Projectile projectile, BGONE_TargetData targetData = null)
	{
		super.InitAttackMode(projectile, targetData);
		m_iCurrentStage = 0;
	}
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || targetData.targetPosition == Vector(0,0,0) || !m_eProjectile)
		 	return targetData;
		
		float distanceFromShooter = vector.Distance(targetData.launchPos, m_eProjectile.GetOrigin());
		float distanceToTarget = vector.Distance(targetData.targetPosition, m_eProjectile.GetOrigin());
		float totalDistance = vector.Distance(targetData.launchPos, targetData.targetPosition);

		float currentCruiseAlt = m_fCruiseAltitude;
		if(m_fCruiseAltitudeRange > 0 && totalDistance < m_fCruiseAltitudeRange)
		{
			currentCruiseAlt = m_fCruiseAltitude * (totalDistance / m_fCruiseAltitudeRange);
		}

		switch(m_iCurrentStage)
		{
			// Launch climb
			case 0:
			{
				if (distanceFromShooter < 10) 
				{
		            targetData.targetPosition = targetData.targetPosition + vector.Up * (distanceToTarget * 2);
		        } 
				else 
				{
			    	m_iCurrentStage = 1;
		        }
				break;
			}
			// Cruise ascent
			case 1:
			{
	       		if(m_eProjectile.GetOrigin()[1] - targetData.targetPosition[1] >= currentCruiseAlt) 
				{
					if(currentCruiseAlt < m_fCruiseAltitude)
	            		m_iCurrentStage = 3;
					else
						m_iCurrentStage = 2;
	        	} 
				else 
				{
	             	targetData.targetPosition = targetData.targetPosition + vector.Up * (distanceToTarget * 1.5);
	        	}
				break;
			}
			// Cruise level
			case 2:
			{
				if(distanceToTarget < (m_eProjectile.GetOrigin()[1] - targetData.targetPosition[1]) * 2)
					m_iCurrentStage = 3;
				else
					targetData.targetPosition = targetData.targetPosition + vector.Up * currentCruiseAlt;
				break;
			}
			// Terminal dive
			case 3:
			{
				break;
			}
		}
		
		return targetData;
	}		
};
