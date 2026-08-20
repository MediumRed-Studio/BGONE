class BGONE_AttackProfileSwitchUserAction : SCR_InspectionUserAction
{
	protected BGONE_GuidedMissileLauncherComponent m_LauncherComponent;

	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		
		if(m_WeaponComponent)
			m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		else if(pOwnerEntity)
			m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(pOwnerEntity.FindComponent(BGONE_GuidedMissileLauncherComponent));
	}

	override bool CanBeShownScript(IEntity user)
	{
		if(!super.CanBeShownScript(user))
			return false;
			
		if(!m_LauncherComponent)
		{
			if(m_WeaponComponent)
				m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
			else if(GetOwner())
				m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(GetOwner().FindComponent(BGONE_GuidedMissileLauncherComponent));
		}
			
		if(!m_LauncherComponent)
			return false;
			
		return (m_LauncherComponent.GetAttackModesCount() > 1);
	}

	override bool GetActionNameScript(out string outName)
	{
		if(!m_LauncherComponent)
			return false;
			
		string profileName = "Unknown";
		BGONE_AttackProfile_Base activeProfile = m_LauncherComponent.GetCurrentAttackMode();
		if(activeProfile)
			profileName = activeProfile.GetProfileName();
			
		outName = string.Format("Change Attack Mode: %1", profileName);
		return true;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if(!m_LauncherComponent)
		{
			if(m_WeaponComponent)
				m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
			else if(pOwnerEntity)
				m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(pOwnerEntity.FindComponent(BGONE_GuidedMissileLauncherComponent));
		}
			
		if(!m_LauncherComponent)
			return;
			
		m_LauncherComponent.CycleAttackMode();
	}
}
