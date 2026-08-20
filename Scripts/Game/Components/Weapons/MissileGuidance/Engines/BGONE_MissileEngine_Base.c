[BaseContainerProps()]
class BGONE_MissileEngine_Base
{
	[Attribute("0.2", UIWidgets.Slider, "Delay in seconds until engine engages", "0 10 0.1", category: "BGONE")]
	protected float m_fThrustDelay;
	
	[Attribute("2.1", UIWidgets.Slider, "Duration in seconds the engine will provide thrust", "0 60 0.1", category: "BGONE")]
	protected float m_fBurnTime;
	
	[Attribute("20", UIWidgets.Slider, "The speed in m/s the missile is launched at", "0 1000 0.1", category: "BGONE")]
	protected float m_fInitialSpeed;
	
	[Attribute("200", UIWidgets.Slider, "The peak speed the missile will reach at the end of burn time", "0 1000 0.1", category: "BGONE")]
	protected float m_fMaximumSpeed;
	
	[Attribute("5.6", UIWidgets.Slider, "Seconds until the missile self destructs","0 120 0.1", category: "BGONE")]
	protected float m_fTimeToLive;
	
	float GetThrustDelay()
	{
		return m_fThrustDelay;
	}
	
	bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if(!projectile || !projectile.GetPhysics())
			return true;
			
		if(flightTime > m_fTimeToLive)
			return true;
		
		vector dir = vector.Direction(projectile.GetOrigin(), targetPos);
		vector targetVector;
		if(dir.Length() > 0.001)
			targetVector = dir.Normalized();
		else if(projectile.GetPhysics().GetVelocity().Length() > 0.001)
			targetVector = projectile.GetPhysics().GetVelocity().Normalized();
		else
			targetVector = projectile.GetYawPitchRoll().AnglesToVector();
		
		float currentSpeed = projectile.GetPhysics().GetVelocity().Length();
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
		
		vector rotationError = targetVector.VectorToAngles() - projectile.GetYawPitchRoll();
		vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * Math.DEG2RAD;
		vector vel = targetVector * currentSpeed;
		
		projectile.GetPhysics().SetAngularVelocity(angularVel);
		projectile.GetPhysics().SetVelocity(vel);
		return false;
	}			
};
