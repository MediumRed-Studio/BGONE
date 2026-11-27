[BaseContainerProps()]
class BGONE_AttackProfile_TOP : BGONE_AttackProfile_Base
{
	[Attribute("160", UIWidgets.Slider, "Cruise Altitude", "0 1000 1", category: "BGONE")]
	protected int m_fCruiseAltitude;

	[Attribute("1250", UIWidgets.Slider, "How Far The Target Must Be From The Shooter For Cruise Altitude To Be Used", "0 10000 1", category: "BGONE")]
	protected int m_fCruiseAltitudeRange;

	protected int m_iCurrentStage = 0;

	//------------------------------------------------------------------------------------------------
	void BGONE_AttackProfile_TOP()
	{
		m_cProfileName = "Top Attack";
	}

	//------------------------------------------------------------------------------------------------
	override void InitAttackMode(Projectile projectile)
	{
		super.InitAttackMode(projectile);
		m_iCurrentStage = 0;
	}

	//------------------------------------------------------------------------------------------------
	int GetCurrentStage()
	{
		return m_iCurrentStage;
	}

	//------------------------------------------------------------------------------------------------
	bool IsLofting()
	{
		return m_iCurrentStage < 3;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!m_eProjectile || !targetData || targetData.targetPosition == vector.Zero)
			return targetData;

		vector origin = m_eProjectile.GetOrigin();
		float distanceFromShooter = vector.Distance(targetData.launchPos, origin);
		float distanceToTarget = vector.Distance(targetData.targetPosition, origin);
		float totalDistance = vector.Distance(targetData.launchPos, targetData.targetPosition);

		switch (m_iCurrentStage)
		{
			// Launch
			case 0:
			{
				if (distanceFromShooter < 10.0)
				{
					targetData.targetPosition = targetData.targetPosition + vector.Up * distanceToTarget * 2.0;
				}
				else
				{
					m_iCurrentStage = 1;
					targetData.targetPosition = targetData.targetPosition + vector.Up * distanceToTarget * 1.5;
				}
				break;
			}
			// Climb
			case 1:
			{
				float currentCruiseAlt = m_fCruiseAltitude;
				if (m_fCruiseAltitudeRange > 0 && totalDistance < m_fCruiseAltitudeRange)
					currentCruiseAlt = m_fCruiseAltitude * (totalDistance / (float)m_fCruiseAltitudeRange);

				float horizontalDist = vector.DistanceXZ(origin, targetData.targetPosition);
				float diveTriggerDist = Math.Max(currentCruiseAlt * 1.25, 35.0);

				if (origin[1] - targetData.targetPosition[1] >= currentCruiseAlt)
				{
					if (currentCruiseAlt < m_fCruiseAltitude)
						m_iCurrentStage = 3;
					else
						m_iCurrentStage = 2;
				}
				else if (horizontalDist < diveTriggerDist)
				{
					// Short-range overshoot guard: begin terminal dive instead of climbing past the target.
					m_iCurrentStage = 3;
				}
				else
				{
					targetData.targetPosition = targetData.targetPosition + vector.Up * distanceToTarget * 1.5;
				}
				break;
			}
			// Coast
			case 2:
			{
				if (distanceToTarget < (origin[1] - targetData.targetPosition[1]) * 2.0)
				{
					m_iCurrentStage = 3;
				}
				else
				{
					// Hold missile's current cruise altitude over target XZ (never double-add target Y).
					vector coastPos = targetData.targetPosition;
					coastPos[1] = origin[1];
					targetData.targetPosition = coastPos;
				}
				break;
			}
			// Terminal
			case 3:
			{
				break;
			}
		}

		return targetData;
	}
};
