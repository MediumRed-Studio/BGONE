class BGONE_BadMenuUI
{
	protected Widget m_wRoot;
	void Init()
	{
		Widget ok = m_wRoot.FindAnyWidget("PresentWidget");
		Widget missing = m_wRoot.FindAnyWidget("GhostWidget");
	}
}
