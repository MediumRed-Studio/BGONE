class BGONE_ShippedBadCaller
{
	void Run(SCR_BaseGameMode gm)
	{
		BGONE_DiagFixtureHelper.Probe();
		gm.BGONE_DiagModdedHook();
	}
}
