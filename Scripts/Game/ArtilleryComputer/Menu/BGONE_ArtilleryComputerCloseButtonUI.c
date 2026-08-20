class BGONE_ArtilleryComputerCloseButtonUI: SCR_MapUIBaseComponent
{	
	protected SCR_MapToolEntry m_ToolMenuEntry;

	protected void CloseMap()
	{
		GetGame().GetMenuManager().CloseMenuByPreset(ChimeraMenuPreset.MB_ArtilleryComputer);
	}
	
	override void Init()
	{
		if(!m_MapEntity)
			return;
			
		SCR_MapToolMenuUI toolMenu = SCR_MapToolMenuUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapToolMenuUI));
		if (toolMenu)
		{
			m_ToolMenuEntry = toolMenu.RegisterToolMenuEntry(SCR_MapToolMenuUI.s_sToolMenuIcons, "cancel", 2);
			if(m_ToolMenuEntry)
			{
				m_ToolMenuEntry.m_OnClick.Insert(CloseMap);
				m_ToolMenuEntry.SetEnabled(true);
			}
		}
	}
	
	void ~BGONE_ArtilleryComputerCloseButtonUI()
	{
		if(m_ToolMenuEntry)
		{
			m_ToolMenuEntry.m_OnClick.Remove(CloseMap);
			m_ToolMenuEntry = null;
		}
	}
}
