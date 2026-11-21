class BGONE_SelfTestCommentMarkers
{
	void TestMarkers(bool cond, array<int> arr)
	{
		Print("/* not a block comment");
		int x = cond ? 1 : 2;

		// Handle /* markers in line comment
		arr[0] += 1;
	}
}
