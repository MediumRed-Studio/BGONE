modded enum ChimeraMenuPreset 
{
	MB_ArtilleryComputer
}

class BGONE_ArtilleryComputerUI: ChimeraMenuBase
{	
	protected ResourceName m_sArtilleryComputerMapConfigPath = "{3C6C98B0E342CAA0}Configs/Map/MapArtilleryComputer.conf";
	
	protected SCR_MapEntity m_MapEntity;	
	
	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{	
		if (m_MapEntity)
		{	
			MapConfiguration mapConfigFullscreen = m_MapEntity.SetupMapConfig(EMapEntityMode.FULLSCREEN, m_sArtilleryComputerMapConfigPath, GetRootWidget());
			m_MapEntity.OpenMap(mapConfigFullscreen);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{		
		if (m_MapEntity)
			m_MapEntity.CloseMap();
	}
		
	//------------------------------------------------------------------------------------------------
	override void OnMenuInit()
	{		
		if (!m_MapEntity)
			m_MapEntity = SCR_MapEntity.GetMapInstance();
	}
};