[BaseContainerProps()]
class BGONE_MissileEngine_Base : ScriptAndConfig
{
	[Attribute("0.2", UIWidgets.Slider, "Thrust Delay (Seconds) Before Engine Ignites After Launch", "0 10 0.1", category: "BGONE")]
	protected float m_fThrustDelay;
	
	[Attribute("2.1", UIWidgets.Slider, "Thrust Duration (Seconds) While Rocket Motor Burns", "0 60 0.1", category: "BGONE")]
	protected float m_fThrustBurnTime;
	
	[Attribute("50", UIWidgets.Slider, "Initial Exit Velocity (Meters per Second)", "0 500 1", category: "BGONE")]
	protected float m_fInitialSpeed;
	
	[Attribute("200", UIWidgets.Slider, "Max Powered Speed (Meters per Second)", "0 1500 1", category: "BGONE")]
	protected float m_fMaxSpeed;
	
	[Attribute("30", UIWidgets.Slider, "Total Flight Lifetime Before Fuel / Battery Exhaustion (ammo prefab overrides win; must match seeker TTL + MissileMove TimeToLive there — per-weapon: PLOS 6.1, SACLOS 12, VIS 30)", "0 120 1", category: "BGONE")]
	protected float m_fTimeToLive;

	float GetThrustDelay()
	{
		return m_fThrustDelay;
	}

	float CalculateSpeed(float flightTime)
	{
		if(flightTime < m_fThrustDelay)
			return m_fInitialSpeed;

		if(flightTime <= m_fThrustBurnTime)
		{
			float burnFraction = (flightTime - m_fThrustDelay) / Math.Max(m_fThrustBurnTime - m_fThrustDelay, 0.001);
			return Math.Lerp(m_fInitialSpeed, m_fMaxSpeed, burnFraction);
		}

		float coastFraction = (flightTime - m_fThrustBurnTime) / Math.Max(m_fTimeToLive - m_fThrustBurnTime, 0.001);
		return Math.Lerp(m_fMaxSpeed, m_fInitialSpeed, Math.Clamp(coastFraction, 0, 1));
	}

	int ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if(!projectile)
			return EBGONE_DetonationState.NONE;
		
		// Upstream parity: engine TTL expiry detonates. This is the PLOS
		// warhead path (PLOS has no seeker TTL, so engine + MissileMove are
		// its only timers); SACLOS/VIS seekers fire first on ties since the
		// seeker runs earlier in EOnSimulate. Expiry detonates on the next
		// simulate tick (the flag is assigned after the detonation check).
		if(m_fTimeToLive > 0 && flightTime > m_fTimeToLive)
			return EBGONE_DetonationState.IMPACT;
			
		Physics phys = projectile.GetPhysics();
		if(!phys)
			return EBGONE_DetonationState.NONE;

		vector currentPos = projectile.GetOrigin();
		vector toTarget = targetPos - currentPos;
		float distToTarget = toTarget.Length();
		
		if(distToTarget < 0.001)
			return EBGONE_DetonationState.NONE;

		vector targetDir = toTarget.Normalized();
		float currentSpeed = CalculateSpeed(flightTime);
		
		// Map angular error to [-180, 180] to prevent Euler 0/360 gimbal snap
		vector targetAngles = targetDir.VectorToAngles();
		vector currentAngles = projectile.GetYawPitchRoll();
		vector rotationError = Vector(
			Math.MapAngle(targetAngles[0] - currentAngles[0]),
			Math.MapAngle(targetAngles[1] - currentAngles[1]),
			Math.MapAngle(targetAngles[2] - currentAngles[2])
		);
		
		vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * Math.DEG2RAD;
		vector vel = targetDir * currentSpeed;
		
		projectile.SetYawPitchRoll(targetAngles);
		phys.SetAngularVelocity(angularVel);
		phys.SetVelocity(vel);
		
		return EBGONE_DetonationState.NONE;
	}
}
