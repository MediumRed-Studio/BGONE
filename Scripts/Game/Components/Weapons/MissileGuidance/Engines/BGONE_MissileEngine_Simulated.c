[BaseContainerProps()]
class BGONE_MissileEngine_Simulated : BGONE_MissileEngine_Base
{
	[Attribute("0.1", UIWidgets.Slider, "Min Control Fin Deflection Step In Degrees", "0 10 0.01", category: "BGONE")]
	protected float m_fMinDeflection;
	
	[Attribute("1.0", UIWidgets.Slider, "Max Control Fin Deflection Step In Degrees", "0 45 0.1", category: "BGONE")]
	protected float m_fMaxDeflection;

	override int ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if(!projectile)
			return EBGONE_DetonationState.NONE;
		
		// Same TTL-expiry detonation as the base engine (see there).
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
		vector currentVel = phys.GetVelocity();
		vector velDir = targetDir;
		if(currentVel.Length() > 0.01)
			velDir = currentVel.Normalized();

		// Compute angular rotation from current velocity to target direction
		float dot = Math.Clamp(vector.Dot(velDir, targetDir), -1.0, 1.0);
		float angleError = Math.Acos(dot) * Math.RAD2DEG;
		
		float step = Math.Clamp(angleError * timeSlice * 10.0, m_fMinDeflection * timeSlice, m_fMaxDeflection * timeSlice);
		float fraction = 1.0;
		if(angleError > 0.001)
			fraction = Math.Clamp(step / angleError, 0, 1);
			
		vector newVelDir = vector.Lerp(velDir, targetDir, fraction).Normalized();
		float currentSpeed = CalculateSpeed(flightTime);
		vector vel = newVelDir * currentSpeed;

		vector targetAngles = newVelDir.VectorToAngles();
		vector currentAngles = projectile.GetYawPitchRoll();
		vector rotationError = Vector(
			Math.MapAngle(targetAngles[0] - currentAngles[0]),
			Math.MapAngle(targetAngles[1] - currentAngles[1]),
			Math.MapAngle(targetAngles[2] - currentAngles[2])
		);
		
		vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * Math.DEG2RAD;

		phys.SetAngularVelocity(angularVel);
		phys.SetVelocity(vel);
		
		return EBGONE_DetonationState.NONE;
	}
}
