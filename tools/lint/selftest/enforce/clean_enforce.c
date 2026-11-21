class BGONE_CleanEnforce
{
	[Attribute("1")]
	protected int m_iConfigVal;

	[RplProp()]
	protected int m_iNetVal;

	// Comments with a ? b : c and vals[0] += 1 must not trip ENF00x.
	void Test(array<int> vals, bool flag)
	{
		string s = "flag ? 1 : 2 and arr[0] += 1";
		vals[0] = vals[0] + 1;
		int x = 2;
		if (flag)
			x = 1;
		x += 1;
	}
}
