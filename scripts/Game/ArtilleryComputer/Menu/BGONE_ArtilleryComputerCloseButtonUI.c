class BGONE_ArtilleryComputerCloseButtonUI : SCR_MapUIBaseComponent
{
	protected SCR_MapToolEntry m_ToolMenuEntry;

	//------------------------------------------------------------------------------------------------
	protected void UnbindToolMenuEntry()
	{
		if (!m_ToolMenuEntry)
			return;

		if (m_ToolMenuEntry.m_OnClick)
			m_ToolMenuEntry.m_OnClick.Remove(CloseMap);

		m_ToolMenuEntry = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseMap()
	{
		if (!GetGame())
			return;

		MenuManager menuMgr = GetGame().GetMenuManager();
		if (menuMgr)
			menuMgr.CloseMenuByPreset(ChimeraMenuPreset.MB_ArtilleryComputer);
	}

	//------------------------------------------------------------------------------------------------
	override void Init()
	{
		if (!m_MapEntity)
			return;

		UnbindToolMenuEntry();

		SCR_MapToolMenuUI toolMenu = SCR_MapToolMenuUI.Cast(m_MapEntity.GetMapUIComponent(SCR_MapToolMenuUI));
		if (toolMenu)
		{
			m_ToolMenuEntry = toolMenu.RegisterToolMenuEntry(SCR_MapToolMenuUI.s_sToolMenuIcons, "cancel", 2);
			if (m_ToolMenuEntry)
			{
				m_ToolMenuEntry.m_OnClick.Insert(CloseMap);
				m_ToolMenuEntry.SetEnabled(true);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapClose(MapConfiguration config)
	{
		UnbindToolMenuEntry();
		super.OnMapClose(config);
	}

	//------------------------------------------------------------------------------------------------
	void ~BGONE_ArtilleryComputerCloseButtonUI()
	{
		UnbindToolMenuEntry();
	}
}
