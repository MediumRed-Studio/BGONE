class BGONE_ArmingDistanceSwitchUserAction : SCR_InspectionUserAction
{
	override bool CanBeShownScript(IEntity user)
	{
		if (!super.CanBeShownScript(user))
			return false;
		
		if (!m_WeaponComponent)
			return false;
			
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
			return false;
		
		return (missileComponent.GetArmingDistancesCount() > 1);
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_WeaponComponent)
		{
			outName = "Change Arming Distance";
			return false;
		}
		
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
		{
			outName = "Change Arming Distance";
			return false;
		}
		
		outName = "Change Arming Distance (" + missileComponent.GetCurrentArmingDistance().ToString() + "m)";
		return true;
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_WeaponComponent)
			return;
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
			return;
		
		missileComponent.CycleArmingDistance();
	}
};
