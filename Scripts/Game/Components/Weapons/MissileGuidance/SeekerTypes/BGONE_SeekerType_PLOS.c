[BaseContainerProps()]
class BGONE_SeekerType_PLOS : BGONE_SeekerType_Base
{
	[Attribute("20, 100", UIWidgets.EditBox, "Distances In Meters The Seeker Arms (Arming Distance Is Determined By Launcher Selection)", category: "BGONE")]
	protected ref array<int> m_aArmingDistances;
	
	protected bool m_bIsFlyover;
	protected ref BGONE_TargetData m_eTargetData;
	
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;

	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
		
		BGONE_GuidedMissileComponent missileComponent = BGONE_GuidedMissileComponent.Cast(projectile.FindComponent(BGONE_GuidedMissileComponent));
		if(missileComponent && missileComponent.GetActiveAttackProfile())
		{
			m_bIsFlyover = (missileComponent.GetActiveAttackProfile().Type() == BGONE_AttackProfile_PLOS_FLYOVER);
		}
	}

	override array<int> GetAvailableArmingDistances()
	{
		return m_aArmingDistances;
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		m_eTargetData = targetData;
		if(!m_eTargetData)
			return null;
			
		int currentArmingDistance = 0;
		if(m_aArmingDistances && m_aArmingDistances.IsIndexValid(m_eTargetData.armingDistancesIndex))
			currentArmingDistance = m_aArmingDistances[m_eTargetData.armingDistancesIndex];
			
		if(GetDistanceFromLaunch(m_eTargetData) >= currentArmingDistance)
		{
			if(m_bIsFlyover)
			{
				IEntity target = TopDownTracer();
				if(target && CheckIfIsVehicle(target))
				{
					m_eTargetData.detonated = EBGONE_DetonationState.AIRBURST;
					return m_eTargetData;
				}
			}
		}
		
		return m_eTargetData;
	}

	protected IEntity TopDownTracer()
	{
		if(!m_eProjectile)
			return null;
			
		vector pos = m_eProjectile.GetOrigin();
		vector endPos = pos + (Vector(0, -1, 0) * 3.0);
		
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eProjectile);
		
		m_TraceParam.Start = pos;
		m_TraceParam.End = endPos;
		m_TraceParam.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		
		float fraction = GetGame().GetWorld().TraceMove(m_TraceParam, null);
		if(fraction < 1.0)
			return m_TraceParam.TraceEnt;
			
		return null;
	}

	protected bool CheckIfIsVehicle(IEntity ent)
	{
		if(!ent)
			return false;
			
		if(Vehicle.Cast(ent) || Vehicle.Cast(ent.GetRootParent()))
			return true;
			
		return false;
	}
}
