[BaseContainerProps()]
class BGONE_AttackProfile_PLOS : BGONE_AttackProfile_Base
{
	void BGONE_AttackProfile_PLOS()
	{
		m_sProfileName = "PLOS Direct";
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || !m_eProjectile)
			return targetData;
		
		vector launchAngles = targetData.launchDir.VectorToAngles();
		float newYaw = launchAngles[0] + (targetData.yawChange * flightTime);
		float newPitch = launchAngles[1] + (targetData.pitchChange * flightTime);
		
		vector targetAngles = Vector(newYaw, newPitch, 0);
		vector targetPos = targetData.launchPos + (targetAngles.AnglesToVector() * (GetDistanceFromLaunch(targetData) + 10.0));
		targetData.targetPosition = targetPos;
		
		return targetData;
	}
}
