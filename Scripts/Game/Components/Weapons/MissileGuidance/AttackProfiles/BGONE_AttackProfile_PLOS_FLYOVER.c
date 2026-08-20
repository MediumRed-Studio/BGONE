[BaseContainerProps()]
class BGONE_AttackProfile_PLOS_FLYOVER : BGONE_AttackProfile_PLOS
{
	void BGONE_AttackProfile_PLOS_FLYOVER()
	{
		m_cProfileName = "FlyOver";
	}
	
	[Attribute("2", UIWidgets.Slider, "How Many Meters Above The Target Should The Missile Fly In FlyOver Mode", "0 10 0.1", category: "BGONE")]
	protected float m_fFlyOverOffset;
	
	[Attribute("20", UIWidgets.Slider, "Distance Until The FlyOver Offset Is Reached", "0 1000 1", category: "BGONE")]
	protected int m_fFlyOverOffsetRange;
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData)
			return null;
			
		targetData = super.ProcessFrame(targetData, flightTime);
		if(!targetData)
			return null;
			
		int range = Math.Max(m_fFlyOverOffsetRange, 1);
		float fraction = Math.Clamp(GetDistanceFromLaunch(targetData) / range, 0, 1);
		float offset = Math.Lerp(0, m_fFlyOverOffset, fraction);
		targetData.targetPosition += (vector.Up * offset);
		
		return targetData;
	}		
};
