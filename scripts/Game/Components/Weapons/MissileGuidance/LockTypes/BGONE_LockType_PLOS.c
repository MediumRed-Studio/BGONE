
[BaseContainerProps()]
class BGONE_LockType_PLOS : BGONE_LockType_Base
{
	[Attribute("10", UIWidgets.Slider, "Max change in yaw or pitch the launcher can detect. (Degrees of change per second)", "0 45 0.1", category: "BGONE")]
	protected float m_fMaxAngularChangeDetection;

	protected float lastYawPitch[2] = {0, 0};
	protected bool m_bHasInitialSample = false;
	protected ref BGONE_TargetData m_cTargetDataPLOS;

	//------------------------------------------------------------------------------------------------
	override void StartLock()
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(LockLost);

		super.StartLock();

		m_bHasInitialSample = false;

		array<vector> dirAndPos = GetAimDirAndPosOfLauncher(m_eLauncher);
		vector currentDir = vector.Zero;
		if (dirAndPos && !dirAndPos.IsEmpty())
			currentDir = dirAndPos[0];

		vector initialAngles = currentDir.VectorToAngles();
		lastYawPitch[0] = initialAngles[0];
		lastYawPitch[1] = initialAngles[1];

		m_cTargetDataPLOS = new BGONE_TargetData();
		m_cTargetDataPLOS.launchDir = currentDir;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if (!super.UpdateLock(timeSlice))
			return null;

		if (!m_cTargetDataPLOS)
			m_cTargetDataPLOS = new BGONE_TargetData();

		array<vector> dirAndPos = GetAimDirAndPosOfLauncher(m_eLauncher);
		if (!dirAndPos || dirAndPos.IsEmpty())
			return null;

		vector currentDir = dirAndPos[0];
		if (currentDir.LengthSq() < 0.0001)
			return null;

		// Keep launchDir up to date every frame, including during the initial 0.75s integration window.
		m_cTargetDataPLOS.launchDir = currentDir;

		if (!m_bHasInitialSample && m_fLockDuration < 0.75)
			return null;

		vector currentAngles = currentDir.VectorToAngles();
		float currentYaw = currentAngles[0];
		float currentPitch = currentAngles[1];

		if (!m_bHasInitialSample)
		{
			m_cTargetDataPLOS.yawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / m_fLockDuration;
			m_cTargetDataPLOS.pitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / m_fLockDuration;
			m_bHasInitialSample = true;
		}
		else
		{
			float tempYawChange = Math.MapAngle(currentYaw - lastYawPitch[0]) / timeSlice;
			float tempPitchChange = Math.MapAngle(currentPitch - lastYawPitch[1]) / timeSlice;

			// Smoothing factor for changing the sensitivity of the prediction.
			float alpha = Math.Clamp(timeSlice / 1.5, 0.0, 1.0);
			m_cTargetDataPLOS.yawChange = (tempYawChange * alpha) + m_cTargetDataPLOS.yawChange * (1.0 - alpha);
			m_cTargetDataPLOS.pitchChange = (tempPitchChange * alpha) + m_cTargetDataPLOS.pitchChange * (1.0 - alpha);
		}

		lastYawPitch[0] = currentYaw;
		lastYawPitch[1] = currentPitch;

		// Limit the angular rate passed on.
		m_cTargetDataPLOS.yawChange = Math.Clamp(m_cTargetDataPLOS.yawChange, -m_fMaxAngularChangeDetection, m_fMaxAngularChangeDetection);
		m_cTargetDataPLOS.pitchChange = Math.Clamp(m_cTargetDataPLOS.pitchChange, -m_fMaxAngularChangeDetection, m_fMaxAngularChangeDetection);

		return null;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData GetCurrentTargetData()
	{
		return m_cTargetDataPLOS;
	}

	//------------------------------------------------------------------------------------------------
	override void StopLock()
	{
		super.StopLock();

		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
		{
			queue.Remove(LockLost);
			if (m_eLauncher)
				queue.CallLater(LockLost, 750, false);
			else
				LockLost();
		}
		else
		{
			LockLost();
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnCleanup()
	{
		ScriptCallQueue queue = GetGame().GetCallqueue();
		if (queue)
			queue.Remove(LockLost);

		super.StopLock();
		LockLost();
	}

	//------------------------------------------------------------------------------------------------
	override protected void LockLost()
	{
		super.LockLost();
		m_bHasInitialSample = false;
		m_cTargetDataPLOS = null;
	}

	//------------------------------------------------------------------------------------------------
	float GetYawChange()
	{
		if (!m_cTargetDataPLOS)
			return 0.0;

		return m_cTargetDataPLOS.yawChange;
	}

	//------------------------------------------------------------------------------------------------
	float GetPitchChange()
	{
		if (!m_cTargetDataPLOS)
			return 0.0;

		return m_cTargetDataPLOS.pitchChange;
	}

	//------------------------------------------------------------------------------------------------
	bool HasInitialSample()
	{
		return m_bHasInitialSample;
	}
};
