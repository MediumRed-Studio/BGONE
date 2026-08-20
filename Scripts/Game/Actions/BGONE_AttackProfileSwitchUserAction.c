class BGONE_AttackProfileSwitchUserAction : SCR_InspectionUserAction
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
		
		return (missileComponent.GetAttackModesCount() > 1);
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_WeaponComponent)
		{
			outName = "Change Attack Mode";
			return false;
		}
		
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
		{
			outName = "Change Attack Mode";
			return false;
		}
		
		BGONE_AttackProfile_Base profile = missileComponent.GetCurrentAttackMode();
		string profileName = "Unknown";
		if (profile)
			profileName = profile.GetProfileName();
			
		outName = "Change Attack Mode (" + profileName + ")";
		return true;
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_WeaponComponent)
			return;
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
			return;
		
		missileComponent.CycleAttackMode();
	}
};
