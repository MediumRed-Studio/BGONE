[BaseContainerProps()]
class BGONE_AttackProfile_DIR : BGONE_AttackProfile_Base
{
	void BGONE_AttackProfile_DIR()
	{
		m_sProfileName = "Direct Attack";
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		return targetData;
	}
}
