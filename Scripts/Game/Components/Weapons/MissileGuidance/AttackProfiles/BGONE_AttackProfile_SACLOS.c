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
		if(!m_eProjectile || !targetData)
			return targetData;
			
		if(targetData.targetPosition == Vector(0,0,0))
		{
			vector vel = vector.Zero;
			if(m_eProjectile.GetPhysics())
				vel = m_eProjectile.GetPhysics().GetVelocity();
				
			vector velDir = vector.Zero;
			if(vel.Length() > 0.001)
				velDir = vel.Normalized();
			else
				velDir = m_eProjectile.GetYawPitchRoll().AnglesToVector();
				
			targetData.targetPosition = m_eProjectile.GetOrigin() + velDir * 10;
			return targetData;
		}
		
		vector relativeCorrection = m_eProjectile.VectorToLocal(m_eProjectile.GetOrigin() - targetData.targetPosition);
		relativeCorrection -= m_vCrosshairOffset;
		
		float mag = relativeCorrection.Length();
		float maxDist = Math.Max(m_fMaxCorrectableDistance, 0.001);
		float fovImpulse = Math.Min(1, mag / maxDist);
		
		if(mag > 0.001)
			relativeCorrection = relativeCorrection.Normalized() * fovImpulse;
		else
			relativeCorrection = vector.Zero;
			
		vector newTargetPos = m_eProjectile.GetOrigin() - m_eProjectile.VectorToParent(relativeCorrection);
		targetData.targetPosition = newTargetPos;

		return targetData;
	}		
};
