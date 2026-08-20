[BaseContainerProps()]
class BGONE_LockType_PLOS : BGONE_LockType_Base
{
	[Attribute("10", UIWidgets.Slider, "Max change in yaw or pitch the launcher can detect. (Degrees of change per second)", "0 45 0.1", category: "BGONE")]
	protected float m_fMaxAngularChangeDetection;
	
	protected float lastYawPitch[] = {0,0};
	protected ref BGONE_TargetData m_cTargetDataPLOS;
	protected bool m_bInitialLockComputed = false;
	
	override void StartLock()
	{
		super.StartLock();
		
		GetGame().GetCallqueue().Remove(LockLost);
		
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		vector angles = currentDir.VectorToAngles();
		
		lastYawPitch[0] = angles[0];
		lastYawPitch[1] = angles[1];
		m_cTargetDataPLOS = new BGONE_TargetData();
		m_cTargetDataPLOS.launchDir = currentDir;
		m_bInitialLockComputed = false;
	}
	
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if(!super.UpdateLock(timeSlice))
			return null;
		
		vector currentDir = GetAimDirAndPosOfLauncher(m_eLauncher)[0];
		m_cTargetDataPLOS.launchDir = currentDir;
		
		vector angles = currentDir.VectorToAngles();
		float currentYaw = angles[0];
		float currentPitch = angles[1];
		
		if(m_fLockDuration < 0.75)
		{
			return null;
		}
		
		if(!m_bInitialLockComputed)
		{
		 	m_cTargetDataPLOS.yawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / Math.Max(m_fLockDuration, 0.001);
			m_cTargetDataPLOS.pitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / Math.Max(m_fLockDuration, 0.001);
			m_bInitialLockComputed = true;
		}
		else 
		{
			float tempYawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / Math.Max(timeSlice, 0.001);
			float tempPitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / Math.Max(timeSlice, 0.001);
			
			float alpha = timeSlice / 1.5; // Smoothing factor
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
		m_bInitialLockComputed = false;
	}
};
