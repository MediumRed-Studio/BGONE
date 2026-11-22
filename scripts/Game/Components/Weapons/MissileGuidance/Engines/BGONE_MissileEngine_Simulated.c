[BaseContainerProps()]
class BGONE_MissileEngine_Simulated : BGONE_MissileEngine_Base
{
	[Attribute("0.0005", UIWidgets.Slider, "Min deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMinDeflection;

	[Attribute("0.01", UIWidgets.Slider, "Max deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMaxDeflection;

	//------------------------------------------------------------------------------------------------
	override bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if (m_fTimeToLive > 0 && flightTime >= m_fTimeToLive)
			return true;

		if (!projectile || timeSlice <= 0.0001)
			return false;

		MissileMoveComponent moveComp = ResolveMoveComponent(projectile);
		vector currentVel;
		if (!ReadVelocity(projectile, moveComp, currentVel))
			return false;

		float currentSpeedSq = currentVel.LengthSq();
		if (currentSpeedSq < 0.000001)
			return false;

		float currentSpeed = Math.Sqrt(currentSpeedSq);
		vector currentDir = currentVel / currentSpeed;

		float adjustTime = timeSlice / 0.01;
		float minDeflection = Math.Max(m_fMinDeflection - (m_fMinDeflection * adjustTime), 0.0);
		float maxDeflection = m_fMaxDeflection * adjustTime;
		if (maxDeflection < minDeflection)
			minDeflection = maxDeflection;

		if ((minDeflection != 0.0 || maxDeflection != 0.0) && targetPos != vector.Zero)
		{
			vector toTarget = vector.Direction(projectile.GetOrigin(), targetPos);
			if (toTarget.LengthSq() < 0.000001)
				return false;

			vector targetDir = toTarget.Normalized();
			vector adjustVector = targetDir - currentDir;

			for (int i = 0; i < 3; i++)
			{
				float comp = adjustVector[i];
				if (comp < 0.0)
					adjustVector[i] = -Math.Clamp(Math.AbsFloat(comp), minDeflection, maxDeflection);
				else if (comp > 0.0)
					adjustVector[i] = Math.Clamp(Math.AbsFloat(comp), minDeflection, maxDeflection);
			}

			vector newVector = currentDir + adjustVector;
			if (newVector.LengthSq() < 0.000001)
				newVector = currentDir;
			else
				newVector.Normalize();

			vector angularVel = ComputeAngularVelocity(projectile, newVector, timeSlice);
			ApplyMotion(projectile, moveComp, newVector * currentSpeed, angularVel);
		}

		return false;
	}
};
