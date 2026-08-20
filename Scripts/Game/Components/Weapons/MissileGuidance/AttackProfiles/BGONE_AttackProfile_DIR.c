[BaseContainerProps()]
class BGONE_AttackProfile_DIR : BGONE_AttackProfile_Base
{
	void BGONE_AttackProfile_DIR()
	{
		m_cProfileName = "Direct Attack";
	}
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || targetData.targetPosition == Vector(0,0,0))
		 	return targetData;
		
		return targetData;
	}		
};
