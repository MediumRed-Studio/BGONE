
class BGONE_ArtilleryComputerCloseButtonUI: SCR_MapUIBaseComponent
{	
	protected SCR_MapToolEntry m_ToolMenuEntry;

	protected void CloseMap()
	{
		GetGame().GetMenuManager().CloseMenuByPreset(ChimeraMenuPreset.MB_ArtilleryComputer);
	}
	override void Init()
	{
		SCR_MapToolMenuUI toolMenu = SCR_MapToolMenuUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapToolMenuUI));
		if (toolMenu)
		{
			m_ToolMenuEntry = toolMenu.RegisterToolMenuEntry(SCR_MapToolMenuUI.s_sToolMenuIcons, "cancel", 2); // add to menu
			m_ToolMenuEntry.m_OnClick.Insert(CloseMap);
			m_ToolMenuEntry.SetEnabled(true);
		}
	}
};