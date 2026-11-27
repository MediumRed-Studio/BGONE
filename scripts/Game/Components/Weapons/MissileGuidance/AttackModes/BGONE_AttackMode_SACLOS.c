
[BaseContainerProps()]
class BGONE_AttackProfile_SACLOS : BGONE_AttackProfile_Base
{
	[Attribute("", UIWidgets.Auto, "Where the missile wants to stay in relation to the crosshair", category: "BGONE")]
	protected vector m_vCrosshairOffset;

	[Attribute("3", UIWidgets.Slider, "Where the missile wants to stay in relation to the crosshair", "0 100 1", category: "BGONE")]
	protected float m_fMaxCorrectableDistance;

	protected MissileMoveComponent m_MoveComponent;

	//------------------------------------------------------------------------------------------------
	void BGONE_AttackProfile_SACLOS()
	{
		m_cProfileName = "Line Of Sight";
	}

	//------------------------------------------------------------------------------------------------
	override void InitAttackMode(Projectile projectile)
	{
		super.InitAttackMode(projectile);
		if (m_eProjectile)
			m_MoveComponent = MissileMoveComponent.Cast(m_eProjectile.FindComponent(MissileMoveComponent));
		else
			m_MoveComponent = null;
	}

	//------------------------------------------------------------------------------------------------
	protected vector ResolveForwardDirection()
	{
		if (m_MoveComponent)
		{
			vector moveVel = m_MoveComponent.GetVelocity();
			if (moveVel.LengthSq() > 0.0001)
				return moveVel.Normalized();
		}

		if (m_eProjectile)
		{
			Physics phys = m_eProjectile.GetPhysics();
			if (phys)
			{
				vector physVel = phys.GetVelocity();
				if (physVel.LengthSq() > 0.0001)
					return physVel.Normalized();
			}

			vector fwd = m_eProjectile.GetWorldTransformAxis(2);
			if (fwd.LengthSq() > 0.0001)
				return fwd.Normalized();
		}

		return Vector(0, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!m_eProjectile || !targetData)
			return targetData;

		vector origin = m_eProjectile.GetOrigin();
		if (targetData.targetPosition == vector.Zero)
		{
			targetData.targetPosition = origin + ResolveForwardDirection() * 10.0;
			return targetData;
		}

		vector relativeCorrection = m_eProjectile.VectorToLocal(origin - targetData.targetPosition);
		relativeCorrection = relativeCorrection - m_vCrosshairOffset;

		float mag = relativeCorrection.Length();
		if (mag < 0.001)
		{
			targetData.targetPosition = origin + ResolveForwardDirection() * 10.0;
			return targetData;
		}

		float fovImpulse = 1.0;
		if (m_fMaxCorrectableDistance > 0.0)
			fovImpulse = Math.Min(1.0, mag / m_fMaxCorrectableDistance);

		vector scaledCorrection = (relativeCorrection / mag) * fovImpulse;
		vector newTargetPos = origin - m_eProjectile.VectorToParent(scaledCorrection);
		targetData.targetPosition = newTargetPos;

		return targetData;
	}
};
