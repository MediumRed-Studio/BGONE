class BGONE_FixtureBadRpc
{
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_TrustWirePlayer(int playerId)
	{
		// Comment mentioning GetPlayerId() must not satisfy MP001.
		Print(playerId);
	}
}
