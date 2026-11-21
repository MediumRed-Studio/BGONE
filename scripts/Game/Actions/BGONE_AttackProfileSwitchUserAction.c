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
		
		return missileComponent.GetAttackModesCount() > 1;
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_WeaponComponent)
			return false;

		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent)
			return false;

		BGONE_AttackProfile_Base currentMode = missileComponent.GetCurrentAttackMode();
		if (!currentMode)
			return false;
		
		outName = "Change Attack Mode (" + currentMode.GetProfileName() + ")";
		return true;
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_WeaponComponent)
			return;
		BGONE_GuidedMissileLauncherComponent missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (!missileComponent || missileComponent.GetAttackModesCount() == 0)
			return;
		
		missileComponent.CycleAttackMode();
	}
};