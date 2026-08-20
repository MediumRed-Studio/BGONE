[BaseContainerProps()]
class BGONE_AttackProfile_Base
{
	protected Projectile m_eProjectile;
	protected string m_cProfileName = "Base Attack Profile";
	
	void InitAttackMode(Projectile projectile, BGONE_TargetData targetData = null)
	{
		m_eProjectile = projectile;
	}
	
	BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		return targetData;
	}
	
	protected float GetDistanceFromLaunch(BGONE_TargetData targetData)
	{
		if(!m_eProjectile || !targetData)
			return 0;
		return vector.Distance(m_eProjectile.GetOrigin(), targetData.launchPos);
	}
	
	string GetProfileName()
	{
		return m_cProfileName;
	}
};
