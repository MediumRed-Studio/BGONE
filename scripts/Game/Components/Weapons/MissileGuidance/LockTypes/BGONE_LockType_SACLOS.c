
[BaseContainerProps()]
class BGONE_LockType_SACLOS : BGONE_LockType_Base
{
	protected ref BGONE_TargetData m_cTargetDataSACLOS;
	
	override BGONE_LockingData_BASE UpdateLock(float timeSlice)
	{
		return null;
	}
	
	override BGONE_TargetData GetCurrentTargetData() 
	{
		m_cTargetDataSACLOS = new BGONE_TargetData();
		if (!m_eLauncher)
			return m_cTargetDataSACLOS;
		
		// Check if launcher is in a turret.
		Turret turret = Turret.Cast(m_eLauncher.GetParent());
		if (turret)
		{
			TurretControllerComponent controller = TurretControllerComponent.Cast(turret.FindComponent(TurretControllerComponent));
			if (controller)
			{
				BaseCompartmentSlot slot = controller.GetCompartmentSlot();
				if (slot)
					m_cTargetDataSACLOS.shooterRplId = BGONE_TargetData.ResolveEntityRplId(slot.GetOccupant());
			}
		}
		else
		{
			m_cTargetDataSACLOS.shooterRplId = BGONE_TargetData.ResolveEntityRplId(m_eLauncher.GetRootParent());
		}
		
		return m_cTargetDataSACLOS;
	}
	
	override void StartLock()
	{
		return;
	}
	
	override void StopLock()
	{
		return;
	}
	
};
