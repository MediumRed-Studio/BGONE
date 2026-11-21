
[BaseContainerProps()]
class BGONE_MissileEngine_Simulated : BGONE_MissileEngine_Base
{
	
	[Attribute("0.0005", UIWidgets.Slider, "Min deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMinDeflection;
	[Attribute("0.01", UIWidgets.Slider, "Max deflection of the missile fins.", "0 10 0.0005", precision: 4, category: "BGONE")]
	protected float m_fMaxDeflection;
	
	override bool ProcessFrame(Projectile projectile, vector targetPos, float flightTime, float timeSlice)
	{
		if (!projectile)
			return false;

		Physics phys = projectile.GetPhysics();
		if (!phys)
			return false;

		float adjustTime = timeSlice / 0.01;
		float minDeflection = Math.Max(m_fMinDeflection - (m_fMinDeflection * adjustTime), 0);
		float maxDeflection = m_fMaxDeflection * adjustTime;
		
		if ((minDeflection != 0 || maxDeflection != 0) && targetPos != Vector(0,0,0))
		{
			vector targetVector = vector.Direction(projectile.GetOrigin(), targetPos);
			vector adjustVector = targetVector.Normalized() - phys.GetVelocity().Normalized();
			
			if(adjustVector[0] < 0)
			{
				adjustVector[0] = -Math.Clamp(Math.AbsFloat(adjustVector[0]),minDeflection,maxDeflection);
			}
			else if(adjustVector[0] > 0)
			{
				adjustVector[0] = Math.Clamp(Math.AbsFloat(adjustVector[0]),minDeflection,maxDeflection);
			}
			if(adjustVector[1] < 0)
			{
				adjustVector[1] = -Math.Clamp(Math.AbsFloat(adjustVector[1]),minDeflection,maxDeflection);
			}
			else if(adjustVector[1] > 0)
			{
				adjustVector[1] = Math.Clamp(Math.AbsFloat(adjustVector[1]),minDeflection,maxDeflection);
			}
			if(adjustVector[2] < 0)
			{
				adjustVector[2] = -Math.Clamp(Math.AbsFloat(adjustVector[2]),minDeflection,maxDeflection);
			}
			else if(adjustVector[2] > 0)
			{
				adjustVector[2] = Math.Clamp(Math.AbsFloat(adjustVector[2]),minDeflection,maxDeflection);
			}
			
			vector newVector = phys.GetVelocity().Normalized() + adjustVector;
			
			vector rotationError = newVector.VectorToAngles() - projectile.GetYawPitchRoll();
			vector angularVel = SCR_Math3D.GetFixedAxisVector(rotationError) * timeSlice * Math.DEG2RAD;
			
			phys.SetAngularVelocity(angularVel);
			phys.SetVelocity(newVector.Normalized() * phys.GetVelocity().Length());
			
			// Non working but better simulation...
			//float XZ_Mag = Math.Sqrt(Math.Pow(newVector[0], 2) + Math.Pow(newVector[2], 2));
			//float Y_Mag = -newVector[1] / XZ_Mag;
			
			//vector angMat[] = {};
			//Math3D.DirectionAndUpMatrix(newVector, Vector(newVector[0] * Y_Mag, newVector[2] * Y_Mag, XZ_Mag), angMat);
			//Math3D.DirectionAndUpMatrix(targetVector, vector.Up, angMat);
			
			//targetData.projectile.SetYawPitchRoll(newVector.VectorToAngles());

			//targetData.projectile.GetPhysics().SetAngularVelocity(Math3D.MatrixToAngles(angMat).AnglesToVector() * XZ_Mag);
			//targetData.projectile.GetPhysics().SetAngularVelocity(vector.Lerp(targetData.projectile.GetPhysics().GetAngularVelocity(), Math3D.MatrixToAngles(angMat).AnglesToVector() * rotateForce, 0.2 ));
			
			//targetData.projectile.GetPhysics().SetAngularVelocity((targetVector.Normalized().VectorToAngles() - targetData.projectile.GetYawPitchRoll()) * Math.DEG2RAD * (timeSlice * 10));
			
			/*
			vector rotationError = targetVector.VectorToAngles() - targetData.projectile.GetYawPitchRoll();
			Print("targetAng: " + targetVector.VectorToAngles());
			Print("CurrentAng: " + targetData.projectile.GetYawPitchRoll());
			Print("rotationError: " + rotationError);
			
			vector applyAngRad = rotationError * Math.DEG2RAD;
			// Convert the rotation to a matrix
			vector rotationMatrix[] = {};
			Math3D.AnglesToMatrix(applyAngRad, rotationMatrix);
			
			// Get the current rotation matrix of the projectile
			vector currentRotationMatrix[] = {};
			Math3D.AnglesToMatrix(targetData.projectile.GetYawPitchRoll() * Math.DEG2RAD, rotationMatrix);
			
			// Multiply the current rotation matrix by the new rotation matrix to get the result in world space
			vector newRotationMatrix[] = {};
			Math3D.MatrixMultiply3(currentRotationMatrix, rotationMatrix, newRotationMatrix);
			
			// Extract the new angles from the resulting matrix
			vector newAngles = Math3D.MatrixToAngles(newRotationMatrix);
			
			// Calculate the difference between the new angles and the current angles
			vector applyAngle = newAngles - targetData.projectile.GetYawPitchRoll();
			
			float qCurrentRot[4] = {};
			targetData.projectile.GetYawPitchRoll().QuatFromAngles(qCurrentRot);
			
			float qTargetRot[4] = {};
			rotationError.QuatFromAngles(qTargetRot);
			
			float qRotateMat[4] = {};
			Math3D.QuatRotateTowards(qRotateMat,qCurrentRot,qTargetRot, 100);
			
			vector rotation = Math3D.QuatToAngles(qRotateMat);
			
			targetData.projectile.GetPhysics().SetAngularVelocity(SCR_Math3D.GetFixedAxisVector(rotationError) * targetData.timeSlice * Math.DEG2RAD * 500);
			targetData.projectile.GetPhysics().SetVelocity(targetVector.Normalized() * targetData.projectile.GetPhysics().GetVelocity().Length());
			*/
		}
		return false;
	}
};