[BaseContainerProps()]
class BGONE_AttackProfile_PLOS : BGONE_AttackProfile_Base
{
	void BGONE_AttackProfile_PLOS()
	{
		m_cProfileName = "Direct Attack";
	}
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData)
			return null;
			
		float m_fNewYaw = targetData.launchDir.ToYaw() + (targetData.yawChange * flightTime);
		float m_fNewPitch = targetData.launchDir.VectorToAngles()[1] + (targetData.pitchChange * flightTime);
		
		vector newTargetDirection = Vector(m_fNewYaw, m_fNewPitch, 0).AnglesToVector();
		vector m_vTargetPos = targetData.launchPos + (newTargetDirection * (GetDistanceFromLaunch(targetData) + 10));
			
		targetData.targetPosition = m_vTargetPos;
		return targetData;
	}		
};
