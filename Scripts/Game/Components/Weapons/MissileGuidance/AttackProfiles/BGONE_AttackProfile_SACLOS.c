[BaseContainerProps()]
class BGONE_AttackProfile_SACLOS : BGONE_AttackProfile_Base
{
	[Attribute("0 0 0", UIWidgets.Coords, "Where the missile wants to stay in relation to the crosshair", category: "BGONE")]
	protected vector m_vCrossHairOffset;
	
	[Attribute("10", UIWidgets.Slider, "Max Distance in Meters The Missile Can Correct Its Path Towards The Crosshair", "0 100 0.1", category: "BGONE")]
	protected float m_fMaxCorrectableDistance;

	void BGONE_AttackProfile_SACLOS()
	{
		m_sProfileName = "SACLOS";
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || !m_eProjectile)
			return targetData;
		
		if(targetData.targetPosition == Vector(0,0,0))
			return targetData;
		
		Physics phys = m_eProjectile.GetPhysics();
		if(!phys)
			return targetData;
			
		vector vel = phys.GetVelocity();
		if(vel.Length() < 0.001)
			return targetData;

		vector projPos = m_eProjectile.GetOrigin();
		vector targetPos = targetData.targetPosition;
		
		// Vector from projectile to target
		vector toTarget = targetPos - projPos;
		float distToTarget = toTarget.Length();
		if(distToTarget < 0.001)
			return targetData;
			
		// Proportional correction vector
		vector leadPos = projPos + (vel.Normalized() * (distToTarget + 10.0));
		vector error = targetPos - leadPos + m_vCrossHairOffset;
		float errorDist = error.Length();
		
		float correctionFraction = Math.Clamp(errorDist / Math.Max(m_fMaxCorrectableDistance, 0.001), 0, 1);
		targetData.targetPosition = targetPos + (error * correctionFraction);
		
		return targetData;
	}
}
