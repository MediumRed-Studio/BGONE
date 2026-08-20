[BaseContainerProps()]
class BGONE_AttackProfile_PLOS_FLYOVER : BGONE_AttackProfile_PLOS
{
	[Attribute("2", UIWidgets.Slider, "Height In Meters Missile Will Fly Over The Target", "0 20 0.1", category: "BGONE")]
	protected float m_fFlyOverOffset;
	
	[Attribute("20", UIWidgets.Slider, "Distance In Meters From Launch Before Missile Achieves Flyover Offset", "0 200 1", category: "BGONE")]
	protected int m_iFlyOverOffsetRange;

	void BGONE_AttackProfile_PLOS_FLYOVER()
	{
		m_sProfileName = "Fly Over";
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		targetData = super.ProcessFrame(targetData, flightTime);
		if(!targetData)
			return null;
		
		float distFromLaunch = GetDistanceFromLaunch(targetData);
		float offsetMultiplier = Math.Clamp(distFromLaunch / Math.Max(m_iFlyOverOffsetRange, 1.0), 0, 1);
		
		targetData.targetPosition += (Vector(0, 1, 0) * (m_fFlyOverOffset * offsetMultiplier));
		return targetData;
	}
}
