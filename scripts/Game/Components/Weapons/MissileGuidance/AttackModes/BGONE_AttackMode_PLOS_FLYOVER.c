[BaseContainerProps()]
class BGONE_AttackProfile_PLOS_FLYOVER : BGONE_AttackProfile_PLOS
{
	[Attribute("2", UIWidgets.Slider, "How Many Meters Above The Target Should The Missile Fly In FlyOver Mode", "0 10 0.1", category: "BGONE")]
	protected float m_fFlyOverOffset;

	[Attribute("20", UIWidgets.Slider, "Distance Until The FlyOver Offset Is Reached", "0 1000 1", category: "BGONE")]
	protected int m_fFlyOverOffsetRange;

	//------------------------------------------------------------------------------------------------
	void BGONE_AttackProfile_PLOS_FLYOVER()
	{
		m_cProfileName = "FlyOver";
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		targetData = super.ProcessFrame(targetData, flightTime);
		if (!targetData)
			return null;

		float offsetProgress = 1.0;
		if (m_fFlyOverOffsetRange > 0)
		{
			float clampedDist = Math.Clamp(GetDistanceFromLaunch(targetData), 0.0, (float)m_fFlyOverOffsetRange);
			offsetProgress = Math.Map(clampedDist, 0.0, (float)m_fFlyOverOffsetRange, 0.0, 1.0);
		}

		targetData.targetPosition = targetData.targetPosition + (vector.Up * Math.Lerp(0.0, m_fFlyOverOffset, offsetProgress));
		return targetData;
	}
};
