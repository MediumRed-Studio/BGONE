class BGONE_ArmingDistanceSwitchUserAction : ScriptedUserAction
{
	protected BGONE_GuidedMissileLauncherComponent m_LauncherComponent;
	
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		if(pOwnerEntity)
			m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(pOwnerEntity.FindComponent(BGONE_GuidedMissileLauncherComponent));
	}
	
	override bool CanBeShownScript(IEntity user)
	{
		if(!m_LauncherComponent)
			return false;
			
		return (m_LauncherComponent.GetArmingDistancesCount() > 1);
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if(!m_LauncherComponent)
			return false;
			
		outName = string.Format("Change Arming Distance: %1m", m_LauncherComponent.GetCurrentArmingDistance());
		return true;
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if(!m_LauncherComponent && pOwnerEntity)
			m_LauncherComponent = BGONE_GuidedMissileLauncherComponent.Cast(pOwnerEntity.FindComponent(BGONE_GuidedMissileLauncherComponent));
			
		if(!m_LauncherComponent)
			return;
			
		m_LauncherComponent.CycleArmingDistance();
	}
}
