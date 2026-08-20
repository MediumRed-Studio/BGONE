[BaseContainerProps()]
class BGONE_MissileEngine_Simulated : BGONE_MissileEngine_Base
{
	[Attribute("0.0005", UIWidgets.Slider, "Min deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMinDeflection;
	[Attribute("0.01", UIWidgets.Slider, "Max deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMaxDeflection;
	
	override bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if(!projectile || !projectile.GetPhysics())
			return true;
			
		if(flightTime > m_fTimeToLive)
			return true;
			
		float adjustTime = timeSlice / 0.01;
		float minDeflection = m_fMinDeflection * adjustTime;
		float maxDeflection = m_fMaxDeflection * adjustTime;
		
		if ((minDeflection != 0 || maxDeflection != 0) && targetPos != Vector(0,0,0))
		{
			vector targetDir = vector.Direction(projectile.GetOrigin(), targetPos);
			vector targetVector = vector.Zero;
			if(targetDir.Length() > 0.001)
				targetVector = targetDir.Normalized();
			else
				targetVector = projectile.GetYawPitchRoll().AnglesToVector();
				
			vector vel = projectile.GetPhysics().GetVelocity();
			vector velDir = vector.Zero;
			if(vel.Length() > 0.001)
				velDir = vel.Normalized();
			else
				velDir = projectile.GetYawPitchRoll().AnglesToVector();
				
			vector adjustVector = targetVector - velDir;
			
			for(int i = 0; i < 3; i++)
			{
				if(adjustVector[i] < 0)
					adjustVector[i] = -Math.Clamp(Math.AbsFloat(adjustVector[i]), minDeflection, maxDeflection);
				else if(adjustVector[i] > 0)
					adjustVector[i] = Math.Clamp(Math.AbsFloat(adjustVector[i]), minDeflection, maxDeflection);
			}
			
			vector newVector = velDir + adjustVector;
			if(newVector.Length() > 0.001)
				newVector = newVector.Normalized();
			else
				newVector = targetVector;
			
			vector rotationError = newVector.VectorToAngles() - projectile.GetYawPitchRoll();
			vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * Math.DEG2RAD;
			
			float currentSpeed = vel.Length();
			if(flightTime >= m_fThrustDelay && flightTime < m_fBurnTime)
			{
				float burnFraction = Math.Clamp((flightTime - m_fThrustDelay) / Math.Max(m_fBurnTime - m_fThrustDelay, 0.001), 0, 1);
				currentSpeed = Math.Lerp(m_fInitialSpeed, m_fMaximumSpeed, burnFraction);
			}
			else if(flightTime >= m_fBurnTime)
			{
				float decayFraction = Math.Clamp((flightTime - m_fBurnTime) / Math.Max(m_fTimeToLive - m_fBurnTime, 0.001), 0, 1);
				currentSpeed = Math.Lerp(m_fMaximumSpeed, m_fInitialSpeed, decayFraction);
			}
			if(currentSpeed < 1.0)
				currentSpeed = Math.Max(m_fInitialSpeed, 10.0);
			
			projectile.GetPhysics().SetAngularVelocity(angularVel);
			projectile.GetPhysics().SetVelocity(newVector * currentSpeed);
		}
		return false;
	}
};
