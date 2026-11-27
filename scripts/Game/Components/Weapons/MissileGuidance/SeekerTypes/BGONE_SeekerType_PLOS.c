
[BaseContainerProps()]
class BGONE_SeekerType_PLOS : BGONE_SeekerType_Base
{
	[Attribute("20", UIWidgets.Auto, "Possible Selections Of Arming Distances Until The Magnetic Proximity Sensor Is Activated (Only In FlyOver Mode).","0 10000", category: "BGONE")]
	protected ref array<int> m_fArmingDistances;

	[Attribute("5", UIWidgets.Auto, "How Far Down Can The Proximity Sensor Detect Targets (Only In FlyOver Mode).","0 10000", category: "BGONE")]
	protected float m_fProximityDetectionRange;

	protected int m_iArmingDistanceIndex = 0;
	BGONE_TargetData m_eTargetData;
	protected bool m_bIsFlyover;
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aTraceExclude;

	//------------------------------------------------------------------------------------------------
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_bIsFlyover = false;
		m_iArmingDistanceIndex = 0;

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};

		if (!projectile || !targetData)
			return;

		BGONE_GuidedMissileComponent missileComponent = BGONE_GuidedMissileComponent.Cast(projectile.FindComponent(BGONE_GuidedMissileComponent));
		if (missileComponent)
		{
			BGONE_AttackProfile_Base activeProfile = missileComponent.GetActiveAttackProfile();
			if (activeProfile)
				m_bIsFlyover = activeProfile.Type() == BGONE_AttackProfile_PLOS_FLYOVER;
		}

		m_iArmingDistanceIndex = targetData.armingDistancesIndex;
	}

	//------------------------------------------------------------------------------------------------
	override array<int> GetAvailableArmingDistances()
	{
		return m_fArmingDistances;
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!targetData)
			return null;

		if (targetData.detonated > 0)
		{
			if (m_bIsFlyover)
				targetData.detonated = 2;
			return targetData;
		}

		if (m_bIsFlyover && m_fArmingDistances && m_iArmingDistanceIndex >= 0 && m_iArmingDistanceIndex < m_fArmingDistances.Count() && GetDistanceFromLaunch(targetData) > m_fArmingDistances[m_iArmingDistanceIndex])
			targetData.detonated = ProcessProxyFuse();

		m_eTargetData = targetData;
		return m_eTargetData;
	}

	//------------------------------------------------------------------------------------------------
	protected int ProcessProxyFuse()
	{
		if (!m_eProjectile)
			return 0;

		bool detonate = TopDownTracer(m_eProjectile.GetOrigin(), m_fProximityDetectionRange, m_eProjectile);
		if (!detonate)
			return 0;

		return 2;
	}

	//------------------------------------------------------------------------------------------------
	protected bool TopDownTracer(vector vStartPosition, float fTravelDistance, IEntity owner = null, IEntity target = null)
	{
		if (!owner)
			return false;

		BaseWorld world = owner.GetWorld();
		if (!world)
			return false;

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};

		m_aTraceExclude.Clear();
		m_aTraceExclude.Insert(owner);
		if (target)
			m_aTraceExclude.Insert(target);

		m_TraceParam.Flags = TraceFlags.ENTS;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		m_TraceParam.Exclude = null;
		m_TraceParam.ExcludeArray = m_aTraceExclude;
		m_TraceParam.Start = vStartPosition;
		m_TraceParam.End = vStartPosition + fTravelDistance * vector.Up * -1.0;
		m_TraceParam.TraceEnt = null;

		world.TraceMove(m_TraceParam, CheckIfIsVehicle);
		return m_TraceParam.TraceEnt != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool CheckIfIsVehicle(IEntity ent)
	{
		IEntity cur = ent;
		while (cur)
		{
			if (BaseVehicle.Cast(cur))
				return true;

			cur = cur.GetParent();
		}

		return false;
	}
};
