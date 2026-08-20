class BGONE_LockingData_BASE
{
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

	void InitLockType(IEntity owner)
	{
	}

	void StartLock()
	{
	}

	void StopLock()
	{
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

	void GetAimDirAndPosOfLauncher(IEntity launcher, out vector aimDir, out vector aimPos)
	{
		aimDir = Vector(0,0,1);
		aimPos = Vector(0,0,0);
		
		if(!launcher)
			return;

		Turret turret = Turret.Cast(launcher.GetParent());
		if(turret)
		{
			TurretControllerComponent turretComp = TurretControllerComponent.Cast(turret.FindComponent(TurretControllerComponent));
			if(turretComp)
			{
				BaseSightsComponent sights = turretComp.GetCurrentSights();
				if(sights)
				{
					aimDir = sights.GetSightsDirection(false);
					aimPos = sights.GetSightsRearPosition(false);
					return;
				}
				
				vector mat[4];
				turret.GetWorldTransform(mat);
				aimDir = mat[2];
				aimPos = turret.GetOrigin();
				return;
			}
		}

		SCR_ChimeraCharacter shooter = SCR_ChimeraCharacter.Cast(launcher.GetRootParent());
		if(!shooter)
			return;

		BaseWeaponManagerComponent weaponManager = BaseWeaponManagerComponent.Cast(shooter.FindComponent(BaseWeaponManagerComponent));
		if(weaponManager && weaponManager.GetCurrentWeapon())
		{
			BaseSightsComponent sights = weaponManager.GetCurrentWeapon().GetSights();
			if(sights)
			{
				aimDir = sights.GetSightsDirection(false);
				aimPos = sights.GetSightsRearPosition(false);
				return;
			}
		}

		aimDir = shooter.EyePosition() - launcher.GetOrigin();
		aimDir.Normalize();
		aimPos = shooter.EyePosition();
	}
}
