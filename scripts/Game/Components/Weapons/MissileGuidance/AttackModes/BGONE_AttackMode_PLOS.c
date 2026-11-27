[BaseContainerProps()]
class BGONE_AttackProfile_PLOS : BGONE_AttackProfile_Base
{
	//------------------------------------------------------------------------------------------------
	void BGONE_AttackProfile_PLOS()
	{
		m_cProfileName = "Direct Attack";
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!m_eProjectile || !targetData)
			return targetData;

		vector launchDir = targetData.launchDir;
		if (launchDir.LengthSq() < 0.0001)
		{
			launchDir = m_eProjectile.GetWorldTransformAxis(2);
			if (launchDir.LengthSq() < 0.0001)
				return targetData;
		}

		vector baseAngles = launchDir.VectorToAngles();
		float basePitch = Math.MapAngle(baseAngles[1]);
		float newYaw = baseAngles[0] + (targetData.yawChange * flightTime);
		float newPitch = Math.Clamp(basePitch + (targetData.pitchChange * flightTime), -85.0, 85.0);

		vector newTargetDirection = Vector(newYaw, newPitch, 0.0).AnglesToVector();
		vector targetPos = targetData.launchPos + (newTargetDirection * (GetDistanceFromLaunch(targetData) + 10.0));

		targetData.targetPosition = targetPos;
		return targetData;
	}
};
