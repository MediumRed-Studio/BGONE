class BGONE_FixtureCleanRpc
{
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_DerivedIdentity(int playerId)
	{
		playerId = GetPlayerId();
		if (playerId <= 0) return;
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TargetParam(int targetPlayerId)
	{
		Print(targetPlayerId);
	}

	// MP001-EXEMPT: fixture test for explicit reviewed exemption.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Exempt(int senderPlayerId)
	{
		Print(senderPlayerId);
	}
}
