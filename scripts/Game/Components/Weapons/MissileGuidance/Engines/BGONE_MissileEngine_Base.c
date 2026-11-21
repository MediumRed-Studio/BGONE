
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
	
	bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if (flightTime > m_fTimeToLive)
			return true;

		if (!projectile)
			return false;

		Physics phys = projectile.GetPhysics();
		if (!phys)
			return false;
		
		vector targetVector = vector.Direction(projectile.GetOrigin(), targetPos).Normalized();
		
		// Limitations on the missile's agility goes here.
		//vector adjustVector = targetVector - projectile.GetYawPitchRoll().AnglesToVector();
		//vector newVector = projectile.GetYawPitchRoll().AnglesToVector() + adjustVector;
		
		float currentSpeed; // = m_eOwner.GetPhysics().GetVelocity().Length();
		if (flightTime > m_fThrustDelay && flightTime < m_fBurnTime)
			currentSpeed = Math.Lerp(m_fInitialSpeed, m_fMaximumSpeed, Math.Clamp(flightTime / m_fBurnTime, 0, 1));
		else if (flightTime > m_fBurnTime)
			currentSpeed = Math.Lerp(m_fMaximumSpeed, m_fInitialSpeed, Math.Clamp((flightTime - m_fBurnTime) / m_fTimeToLive, 0, 1));
		if (currentSpeed == 0)
			currentSpeed = phys.GetVelocity().Length();
		
		vector rotationError = targetVector.VectorToAngles() - projectile.GetYawPitchRoll();
		vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * timeSlice * Math.DEG2RAD;
		vector vel = targetVector * currentSpeed;
		
		// This is not sufficient to make the missile face the travel direction.
		phys.SetAngularVelocity(angularVel);
		phys.SetVelocity(vel);
		return false;
	}			
};