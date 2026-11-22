[BaseContainerProps()]
class BGONE_MissileEngine_Base
{
	static const float MAX_ANGULAR_SPEED_RAD = 12.0;

	[Attribute("0.2", UIWidgets.Slider, "Delay in seconds until engine engages", "0 10 0.1", category: "BGONE")]
	protected float m_fThrustDelay;

	[Attribute("2.1", UIWidgets.Slider, "Duration in seconds the engine will provide thrust", "0 60 0.1", category: "BGONE")]
	protected float m_fBurnTime;

	[Attribute("20", UIWidgets.Slider, "The speed in m/s the missile is launched at", "0 1000 0.1", category: "BGONE")]
	protected float m_fInitialSpeed;

	[Attribute("200", UIWidgets.Slider, "The peak speed the missile will reach at the end of burn time", "0 1000 0.1", category: "BGONE")]
	protected float m_fMaximumSpeed;

	[Attribute("5.6", UIWidgets.Slider, "Seconds until the missile self destructs", "0 120 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	protected Projectile m_CachedProjectile;
	protected MissileMoveComponent m_CachedMoveComp;

	//------------------------------------------------------------------------------------------------
	float GetTimeToLive()
	{
		return m_fTimeToLive;
	}

	//------------------------------------------------------------------------------------------------
	float GetMaximumSpeed()
	{
		return m_fMaximumSpeed;
	}

	//------------------------------------------------------------------------------------------------
	protected MissileMoveComponent ResolveMoveComponent(Projectile inProjectile)
	{
		if (!inProjectile)
			return null;

		if (inProjectile != m_CachedProjectile || !m_CachedMoveComp)
		{
			m_CachedProjectile = inProjectile;
			m_CachedMoveComp = MissileMoveComponent.Cast(inProjectile.FindComponent(MissileMoveComponent));
		}

		return m_CachedMoveComp;
	}

	//------------------------------------------------------------------------------------------------
	protected bool ReadVelocity(Projectile inProjectile, MissileMoveComponent inMoveComp, out vector outVel)
	{
		if (inMoveComp)
		{
			outVel = inMoveComp.GetVelocity();
			return true;
		}

		if (inProjectile)
		{
			Physics phys = inProjectile.GetPhysics();
			if (phys)
			{
				outVel = phys.GetVelocity();
				return true;
			}
		}

		outVel = vector.Zero;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyMotion(Projectile inProjectile, MissileMoveComponent inMoveComp, vector inVel, vector inAngVel)
	{
		if (inMoveComp)
		{
			inMoveComp.SetAngularVelocity(inAngVel);
			inMoveComp.SetVelocity(inVel);
			return;
		}

		if (!inProjectile)
			return;

		Physics phys = inProjectile.GetPhysics();
		if (phys)
		{
			phys.SetAngularVelocity(inAngVel);
			phys.SetVelocity(inVel);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected vector ComputeAngularVelocity(Projectile inProjectile, vector inDesiredDir, float inTimeSlice)
	{
		if (!inProjectile || inTimeSlice <= 0.0001 || inDesiredDir.LengthSq() < 0.000001)
			return vector.Zero;

		vector desiredAngles = inDesiredDir.VectorToAngles();
		vector currentAngles = inProjectile.GetYawPitchRoll();
		vector rotationError = SCR_Math3D.FixEulerVector180(desiredAngles - currentAngles);
		vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * (Math.DEG2RAD / inTimeSlice);
		return SCR_Math3D.ClampMagnitude(angularVel, MAX_ANGULAR_SPEED_RAD);
	}

	//------------------------------------------------------------------------------------------------
	protected float ComputePhaseSpeed(float inFlightTime, float inCurrentSpeed)
	{
		float burnEnd = m_fThrustDelay + m_fBurnTime;
		if (inFlightTime < m_fThrustDelay)
		{
			if (inCurrentSpeed > 0.01)
				return inCurrentSpeed;

			return m_fInitialSpeed;
		}

		if (inFlightTime < burnEnd)
		{
			float burnProgress = 1.0;
			if (m_fBurnTime > 0.0001)
				burnProgress = Math.Clamp((inFlightTime - m_fThrustDelay) / m_fBurnTime, 0.0, 1.0);

			return Math.Lerp(m_fInitialSpeed, m_fMaximumSpeed, burnProgress);
		}

		float coastDuration = Math.Max(m_fTimeToLive - burnEnd, 0.1);
		float coastProgress = Math.Clamp((inFlightTime - burnEnd) / coastDuration, 0.0, 1.0);
		return Math.Lerp(m_fMaximumSpeed, m_fInitialSpeed, coastProgress);
	}

	//------------------------------------------------------------------------------------------------
	bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if (m_fTimeToLive > 0 && flightTime >= m_fTimeToLive)
			return true;

		if (!projectile || timeSlice <= 0.0001)
			return false;

		MissileMoveComponent moveComp = ResolveMoveComponent(projectile);
		vector currentVel;
		if (!ReadVelocity(projectile, moveComp, currentVel))
			return false;

		vector toTarget = vector.Direction(projectile.GetOrigin(), targetPos);
		if (toTarget.LengthSq() < 0.000001)
			return false;

		vector targetVector = toTarget.Normalized();
		float currentSpeed = ComputePhaseSpeed(flightTime, currentVel.Length());
		vector angularVel = ComputeAngularVelocity(projectile, targetVector, timeSlice);
		vector vel = targetVector * currentSpeed;

		ApplyMotion(projectile, moveComp, vel, angularVel);
		return false;
	}
};
