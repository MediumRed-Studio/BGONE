class BGONE_LockingData_BASE
{
	vector lockingPos;
	float lockingProgress = 0;
	ref BGONE_TargetData targetData;
}

[BaseContainerProps()]
class BGONE_LockType_Base : ScriptAndConfig
{
	protected ref ScriptInvoker m_OnLockStartAcquire;
	protected ref ScriptInvoker m_OnLockAcquired;
	protected ref ScriptInvoker m_OnLockLost;
	
	protected ref BGONE_LockingData_BASE m_eLockingData;
	protected IEntity m_eLauncher;
	
	protected bool m_bIsLocking = false;
	protected float m_fLockDuration = 0;
	
	void InitLockType(IEntity owner)
	{
		m_eLauncher = owner;
		m_eLockingData = new BGONE_LockingData_BASE();
	}
	
	void StartLock()
	{
		if(!m_eLockingData)
			m_eLockingData = new BGONE_LockingData_BASE();
			
		m_eLockingData.lockingProgress = 0;
		m_bIsLocking = true;
		m_fLockDuration = 0;
	}
	
	BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		if(!m_bIsLocking)
			return null;
		
		if(timeSlice == 0)
			return null;
		
		m_fLockDuration += timeSlice;
		
		return m_eLockingData;
	}
	
	void StopLock()
	{
		m_bIsLocking = false;
		m_fLockDuration = 0;
		if(m_eLockingData)
		{
			m_eLockingData.lockingProgress = 0;
			m_eLockingData.lockingPos = Vector(0,0,0);
		}
	}
	
	bool IsLocking()
	{
		return m_bIsLocking;
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

	ScriptInvoker GetOnLockStartAcquire()
	{
		if(!m_OnLockStartAcquire)
			m_OnLockStartAcquire = new ScriptInvoker();
		return m_OnLockStartAcquire;
	}

	ScriptInvoker GetOnLockAcquired()
	{
		if(!m_OnLockAcquired)
			m_OnLockAcquired = new ScriptInvoker();
		return m_OnLockAcquired;
	}

	ScriptInvoker GetOnLockLost()
	{
		if(!m_OnLockLost)
			m_OnLockLost = new ScriptInvoker();
		return m_OnLockLost;
	}

	ScriptInvoker GetOnLockStartAquire()
	{
		return GetOnLockStartAcquire();
	}

	ScriptInvoker GetOnLockAquired()
	{
		return GetOnLockAcquired();
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
