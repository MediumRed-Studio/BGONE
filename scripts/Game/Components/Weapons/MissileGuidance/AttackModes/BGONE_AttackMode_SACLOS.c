
[BaseContainerProps()]
class BGONE_AttackProfile_SACLOS : BGONE_AttackProfile_Base
{
	[Attribute("", UIWidgets.Auto, "Where the missile wants to stay in relation to the crosshair", category: "BGONE")]
	protected vector m_vCrosshairOffset;
	
	[Attribute("3", UIWidgets.Slider, "Where the missile wants to stay in relation to the crosshair", "0 100 1", category: "BGONE")]
	protected float m_fMaxCorrectableDistance;
	
	void BGONE_AttackProfile_SACLOS()
	{
		m_cProfileName = "Line Of Sight";
	}
	
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!m_eProjectile || !targetData)
			return targetData;

		if (targetData.targetPosition == Vector(0,0,0))
		{
			Physics phys = m_eProjectile.GetPhysics();
			if (phys)
				targetData.targetPosition = m_eProjectile.GetOrigin() + phys.GetVelocity().Normalized() * 10; // Continue in a straight line
			return targetData;
		}
		
		vector relativeCorrection = m_eProjectile.VectorToLocal(m_eProjectile.GetOrigin() - targetData.targetPosition);
		relativeCorrection -= m_vCrosshairOffset;
		
		float mag = relativeCorrection.Length();
		float fovImpulse = 1;
		if (m_fMaxCorrectableDistance > 0)
			fovImpulse = Math.Min(1, mag / m_fMaxCorrectableDistance);
		
		relativeCorrection = relativeCorrection.Normalized() * fovImpulse;
		vector newTargetPos = m_eProjectile.GetOrigin() - m_eProjectile.VectorToParent(relativeCorrection);
		
		targetData.targetPosition = newTargetPos;

		return targetData;
	}		
	
};
