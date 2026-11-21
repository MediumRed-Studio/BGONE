
[BaseContainerProps()]
class BGONE_LockType_PLOS : BGONE_LockType_Base
{
	
	[Attribute("10", UIWidgets.Slider, "Max change in yaw or pitch the launcher can detect. (Degrees of change per second)", "0 45 0.1", category: "BGONE")]
	protected float m_fMaxAngularChangeDetection;
	
	protected float lastYawPitch[2] = {0, 0};
	protected ref BGONE_TargetData m_cTargetDataPLOS;
	
	override void StartLock()
	{
		super.StartLock();
		
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		vector initialWeaponDirection = currentDir;
		
		lastYawPitch[0] = initialWeaponDirection.ToYaw();
		lastYawPitch[1] = initialWeaponDirection[1];
		m_cTargetDataPLOS = new BGONE_TargetData();
		m_cTargetDataPLOS.launchDir = currentDir;
	}
	
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if(!super.UpdateLock(timeSlice))
			return null;
		
		if(m_cTargetDataPLOS.yawChange == 0 && m_fLockDuration < 0.75)
			return null;
		
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		m_cTargetDataPLOS.launchDir = currentDir;
		
		vector currentWeaponDir = currentDir;
		float currentYaw = currentWeaponDir.ToYaw();
		float currentPitch = currentWeaponDir[1];

		float tempYawChange;
		float tempPitchChange;
		
		if(m_cTargetDataPLOS.yawChange == 0)
		{
		 	m_cTargetDataPLOS.yawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / m_fLockDuration;
			m_cTargetDataPLOS.pitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / m_fLockDuration;
		}
		 else 
		{
			tempYawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / timeSlice;
			tempPitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / timeSlice;
			
			float alpha = timeSlice / 1.5; // Smoothing factor for changing the sensitivity of the prediction, higher number = more responsive but more punishing with jerky mouse movement.
			m_cTargetDataPLOS.yawChange = (tempYawChange * alpha) + m_cTargetDataPLOS.yawChange * (1 - alpha);
			m_cTargetDataPLOS.pitchChange = (tempPitchChange * alpha) + m_cTargetDataPLOS.pitchChange * (1 - alpha);
		}

		lastYawPitch[0] = currentYaw;
		lastYawPitch[1] = currentPitch;
		
		// Limit the change passed on.
		m_cTargetDataPLOS.yawChange = Math.Clamp(m_cTargetDataPLOS.yawChange, -m_fMaxAngularChangeDetection, m_fMaxAngularChangeDetection);
		m_cTargetDataPLOS.pitchChange = Math.Clamp(m_cTargetDataPLOS.pitchChange, -m_fMaxAngularChangeDetection, m_fMaxAngularChangeDetection);	
		
		return null;
	}
	override BGONE_TargetData GetCurrentTargetData() 
	{
		return m_cTargetDataPLOS;
	}
	
	override void StopLock()
	{
		super.StopLock();
	
		GetGame().GetCallqueue().CallLater(LockLost, 750, false);
	}
	
	override protected void LockLost() 
	{
		super.LockLost();
		
		m_cTargetDataPLOS = new BGONE_TargetData();
	}
	
};
