class BGONE_LockingData_BASE
{
	vector lockingPos;
	float lockingProgress = 0;
	ref BGONE_TargetData targetData;
}

[BaseContainerProps()]
class BGONE_LockType_Base : ScriptAndConfig
{
	protected ref ScriptInvoker m_OnLockStartAcquire = new ScriptInvoker();
	protected ref ScriptInvoker m_OnLockAcquired = new ScriptInvoker();
	protected ref ScriptInvoker m_OnLockLost = new ScriptInvoker();

	ScriptInvoker GetOnLockStartAcquire()
	{
		return m_OnLockStartAcquire;
	}

	ScriptInvoker GetOnLockAcquired()
	{
		return m_OnLockAcquired;
	}

	ScriptInvoker GetOnLockLost()
	{
		return m_OnLockLost;
	}

	// Legacy typo alias support
	ScriptInvoker GetOnLockStartAquire()
	{
		return m_OnLockStartAcquire;
	}

	ScriptInvoker GetOnLockAquired()
	{
		return m_OnLockAcquired;
	}

	protected bool m_bIsLocking = false;
	protected ref BGONE_LockingData_BASE m_eLockingData;

	bool IsLocking()
	{
		return m_bIsLocking;
	}

	void InitLockType(IEntity owner)
	{
	}

	void StartLock()
	{
		m_bIsLocking = true;
	}

	void StopLock()
	{
		m_bIsLocking = false;
	}

	void UpdateLock(float timeSlice)
	{
	}

	BGONE_TargetData GetCurrentTargetData()
	{
		return null;
	}

	void PlayLockOnAudio(float currentLockProgress)
	{
	}

	void TerminateLockOnAudio()
	{
	}

	void PlayLockOnAuido(float currentLockProgress)
	{
		PlayLockOnAudio(currentLockProgress);
	}

	protected void LockStartAcquire(BGONE_LockingData_BASE lockingData)
	{
		if(m_OnLockStartAcquire)
			m_OnLockStartAcquire.Invoke(lockingData);
	}

	protected void LockAcquired(BGONE_LockingData_BASE lockingData)
	{
		if(m_OnLockAcquired)
			m_OnLockAcquired.Invoke(lockingData);
	}

	protected void LockLost()
	{
		if(m_OnLockLost)
			m_OnLockLost.Invoke();
	}

	// Legacy typo alias support
	protected void LockStartAquire(BGONE_LockingData_BASE lockingData)
	{
		LockStartAcquire(lockingData);
	}

	protected void LockAquired(BGONE_LockingData_BASE lockingData)
	{
		LockAcquired(lockingData);
	}

	void GetAimDirAndPosOfLauncher(IEntity launcher, out vector aimDir, out vector aimPos)
	{
		aimDir = Vector(0,0,1);
		aimPos = Vector(0,0,0);
		
		if(!launcher)
			return;

		SCR_2DPIPSightsComponent pipSight = ArmaReforgerScripted.GetCurrentPIPSights();
		Turret turret = Turret.Cast(launcher.GetParent());
		
		vector mat[4];
		if(pipSight)
		{ 
			SCR_PIPCamera pipCam = pipSight.GetPIPCamera();
			if(pipCam)
			{
				pipCam.GetWorldCameraTransform(mat);
				aimDir = mat[2];
				aimPos = mat[3];
				return;
			}
		}
		
		if(turret)
		{
			TurretControllerComponent controller = TurretControllerComponent.Cast(turret.FindComponent(TurretControllerComponent));
			if(controller)
			{
				BaseCompartmentSlot slot = controller.GetCompartmentSlot();
				if(slot)
				{
					SCR_ChimeraCharacter shooter = SCR_ChimeraCharacter.Cast(slot.GetOccupant());
					if(shooter && shooter.GetWorld())
					{
						shooter.GetWorld().GetCurrentCamera(mat);
						aimDir = mat[2];
						aimPos = mat[3];
						return;
					}
				}
			}
		}
		
		SCR_ChimeraCharacter shooter = SCR_ChimeraCharacter.Cast(launcher.GetRootParent());
		if(shooter && shooter.GetWorld())
		{
			shooter.GetWorld().GetCurrentCamera(mat);
			aimDir = mat[2];
			aimPos = mat[3];
			return;
		}
		
		ArmaReforgerScripted game = GetGame();
		if(game && game.GetWorld())
		{
			game.GetWorld().GetCurrentCamera(mat);
			aimDir = mat[2];
			aimPos = mat[3];
			return;
		}
		
		aimPos = launcher.GetOrigin();
		vector lmat[4];
		launcher.GetWorldTransform(lmat);
		aimDir = lmat[2];
	}
}
