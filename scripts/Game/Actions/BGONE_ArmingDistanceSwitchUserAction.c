class BGONE_ArmingDistanceSwitchUserAction : SCR_InspectionUserAction
{
	//------------------------------------------------------------------------------------------------
	protected BGONE_GuidedMissileLauncherComponent ResolveLauncherComponent(IEntity fallbackOwner = null)
	{
		BGONE_GuidedMissileLauncherComponent missileComponent;
		if (m_WeaponComponent)
		{
			missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.FindComponent(BGONE_GuidedMissileLauncherComponent));
			if (!missileComponent && m_WeaponComponent.GetOwner())
				missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(m_WeaponComponent.GetOwner().FindComponent(BGONE_GuidedMissileLauncherComponent));
		}

		if (!missileComponent && fallbackOwner)
		{
			missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(fallbackOwner.FindComponent(BGONE_GuidedMissileLauncherComponent));
			if (!missileComponent)
			{
				BaseWeaponComponent fallbackWeapon = BaseWeaponComponent.Cast(fallbackOwner.FindComponent(BaseWeaponComponent));
				if (fallbackWeapon)
					missileComponent = BGONE_GuidedMissileLauncherComponent.Cast(fallbackWeapon.FindComponent(BGONE_GuidedMissileLauncherComponent));
			}
		}

		return missileComponent;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!user || !super.CanBeShownScript(user))
			return false;

		BGONE_GuidedMissileLauncherComponent missileComponent = ResolveLauncherComponent();
		if (!missileComponent)
			return false;

		if (missileComponent.GetArmingDistancesCount() == 0)
			missileComponent.RefreshMissileCapabilities();

		return missileComponent.GetArmingDistancesCount() > 1;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		BGONE_GuidedMissileLauncherComponent missileComponent = ResolveLauncherComponent();
		if (!missileComponent)
			return false;

		if (missileComponent.GetArmingDistancesCount() == 0)
			missileComponent.RefreshMissileCapabilities();

		if (missileComponent.GetArmingDistancesCount() == 0)
			return false;

		outName = "Change Arming Distance For Seeker (" + missileComponent.GetCurrentArmingDistance().ToString() + "m)";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		BGONE_GuidedMissileLauncherComponent missileComponent = ResolveLauncherComponent(pOwnerEntity);
		if (!missileComponent)
			return;

		if (missileComponent.GetArmingDistancesCount() == 0)
			missileComponent.RefreshMissileCapabilities();

		if (missileComponent.GetArmingDistancesCount() == 0)
			return;

		missileComponent.CycleArmingDistance();
	}
};
