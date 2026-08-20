[BaseContainerProps()]
class BGONE_LockType_SACLOS : BGONE_LockType_Base
{
	protected ref BGONE_TargetData m_cTargetDataSACLOS;
	
	override void InitLockType(IEntity owner)
	{
		super.InitLockType(owner);
	}
	
	override BGONE_TargetData GetCurrentTargetData() 
	{
		m_cTargetDataSACLOS = new BGONE_TargetData();
		
		if(!m_eLauncher)
			return m_cTargetDataSACLOS;
			
		Turret turret = Turret.Cast(m_eLauncher.GetParent());
		if(turret)
		{
			TurretControllerComponent controller = TurretControllerComponent.Cast(turret.FindComponent(TurretControllerComponent));
			if(controller)
			{
				BaseCompartmentSlot slot = controller.GetCompartmentSlot();
				if(slot && slot.GetOccupant())
					m_cTargetDataSACLOS.shooterRplId = Replication.FindItemId(slot.GetOccupant());
			}
		}
		else
		{
			IEntity rootParent = m_eLauncher.GetRootParent();
			if(rootParent)
				m_cTargetDataSACLOS.shooterRplId = Replication.FindItemId(rootParent);
		}
		
		return m_cTargetDataSACLOS;
	}
}
