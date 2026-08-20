[BaseContainerProps()]
class BGONE_AttackProfile_Base : ScriptAndConfig
{
	[Attribute("Base", UIWidgets.EditBox, desc: "Name of the attack profile", category: "BGONE")]
	protected string m_sProfileName = "Base";
	
	protected Projectile m_eProjectile;

	void InitAttackMode(Projectile projectile, BGONE_TargetData targetData)
	{
		m_eProjectile = projectile;
	}

	BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		return targetData;
	}

	string GetProfileName()
	{
		return m_sProfileName;
	}

	protected float GetDistanceFromLaunch(BGONE_TargetData targetData)
	{
		if(!m_eProjectile || !targetData)
			return 0;
			
		return vector.Distance(targetData.launchPos, m_eProjectile.GetOrigin());
	}
}
