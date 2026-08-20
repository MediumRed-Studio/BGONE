class BGONE_ArtilleryComputerUserAction : SCR_ScriptedUserAction
{		
	protected SCR_MapEntity m_MapEntity;
	protected bool m_bIsFirstTimeOpened;
	protected SCR_DamageManagerComponent m_DamageComponent;
	protected BGONE_ArtilleryComputerComponent m_eArtilleryComputer;
	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		
		m_DamageComponent = SCR_DamageManagerComponent.GetDamageManager(pOwnerEntity);
		m_eArtilleryComputer = BGONE_ArtilleryComputerComponent.Cast(pOwnerEntity.FindComponent(BGONE_ArtilleryComputerComponent));
		//~ Delay init so action manager has all action initialized
		GetGame().GetCallqueue().CallLater(DelayedInit, param1: pOwnerEntity);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void DelayedInit(IEntity owner)
	{
		if (!owner)
			return;
		
		if (!m_MapEntity)
			m_MapEntity = SCR_MapEntity.GetMapInstance();
	}
	
	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!super.CanBeShownScript(user))
			return false;

		//~ Don't show if damage manager destroyed
		if (m_DamageComponent && m_DamageComponent.GetState() == EDamageState.DESTROYED)
			return false;
		
		if (!m_MapEntity || !m_eArtilleryComputer)
		{
			return false;
		}
		
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
 	{
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity) 
 	{
		//~ Execute Audio broadcast
		super.PerformAction(pOwnerEntity, pUserEntity);
		
		//~ Only show notification for player that used it
		if (SCR_PossessingManagerComponent.GetPlayerIdFromControlledEntity(pUserEntity) != SCR_PlayerController.GetLocalPlayerId())
			return;
		
		m_eArtilleryComputer.OpenComputer(m_MapEntity,pUserEntity);
		
	}
};