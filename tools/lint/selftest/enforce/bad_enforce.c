class BGONE_BadEnforce
{
	[Attribute("1")]
	[RplProp()]
	protected int m_iStacked;

	void Test(array<int> vals, bool flag)
	{
		vals[0] += 1;
		int x = flag ? 1 : 2;
	}
}
