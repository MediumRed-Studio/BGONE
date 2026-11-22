
[BaseContainerProps()]
class BGONE_SeekerType_VIS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How Many Seconds Until The Missile Self Destructs.","0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	[Attribute("30", UIWidgets.Slider, "Field Of View For The Seeker In Degrees.","0 360 1", category: "BGONE")]
	protected float m_fSeekerFieldOfView;

	[Attribute("2", UIWidgets.Slider, "How Many Seconds After Target Is Lost Until The Missile Self Destructs.","0 100 0.1", category: "BGONE")]
	protected float m_fNoTargetDestructTime;

	protected ref BGONE_TargetData m_eTargetData;
	protected IEntity target;
	protected float m_fTargetLastSeenTime = 0.0;
	protected MissileMoveComponent m_MoveComponent;
	protected BGONE_GuidedMissileComponent m_MissileComponent;
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aTraceExclude;
	protected int m_iSeekerLostCount = 0;
	protected bool m_bSelfDestructed = false;

	//------------------------------------------------------------------------------------------------
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_eTargetData = targetData;
		target = null;
		m_fTargetLastSeenTime = 0.0;
		m_iSeekerLostCount = 0;
		m_bSelfDestructed = false;

		if (m_eProjectile)
		{
			m_MoveComponent = MissileMoveComponent.Cast(m_eProjectile.FindComponent(MissileMoveComponent));
			m_MissileComponent = BGONE_GuidedMissileComponent.Cast(m_eProjectile.FindComponent(BGONE_GuidedMissileComponent));
		}
		else
		{
			m_MoveComponent = null;
			m_MissileComponent = null;
		}

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};
	}

	//------------------------------------------------------------------------------------------------
	protected vector ResolveProjectileVelocity()
	{
		if (m_MoveComponent)
		{
			vector moveVel = m_MoveComponent.GetVelocity();
			if (moveVel.LengthSq() > 0.0001)
				return moveVel;
		}

		if (m_eProjectile)
		{
			Physics projPhys = m_eProjectile.GetPhysics();
			if (projPhys)
			{
				vector physVel = projPhys.GetVelocity();
				if (physVel.LengthSq() > 0.0001)
					return physVel;
			}

			vector fwd = m_eProjectile.GetWorldTransformAxis(2);
			if (fwd.LengthSq() > 0.0001)
				return fwd;
		}

		return vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsTopProfileLofting()
	{
		if (!m_MissileComponent)
			return false;

		BGONE_AttackProfile_TOP topProfile = BGONE_AttackProfile_TOP.Cast(m_MissileComponent.GetActiveAttackProfile());
		if (!topProfile)
			return false;

		return topProfile.IsLofting();
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!targetData)
			return null;

		m_eTargetData = targetData;

		if (flightTime > m_fTimeToLive)
		{
			m_eTargetData.detonated = 1;
			return m_eTargetData;
		}

		if (!targetData.targetRplId.IsValid())
			return targetData;

		if (!m_eProjectile)
			return targetData;

		if (!target)
			target = targetData.GetTargetEntity();

		// If target was destroyed/deleted mid-flight, treat as target lost and honor m_fNoTargetDestructTime.
		if (!target)
		{
			m_iSeekerLostCount = m_iSeekerLostCount + 1;
			if (flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime)
			{
				m_eTargetData.detonated = 1;
				m_bSelfDestructed = true;
			}

			m_eTargetData.targetPosition = vector.Zero;
			return m_eTargetData;
		}

		vector projVel = ResolveProjectileVelocity();
		if (projVel.LengthSq() < 0.0001)
			return targetData;

		Physics targetPhys = target.GetPhysics();
		vector centerOfMass = vector.Zero;
		if (targetPhys)
			centerOfMass = targetPhys.GetCenterOfMass();

		vector targetPos;
		if (centerOfMass == vector.Zero)
			targetPos = target.GetOrigin() + vector.Up;
		else
			targetPos = target.CoordToParent(centerOfMass);

		vector projOrigin = m_eProjectile.GetOrigin();
		bool allowLoft = IsTopProfileLofting();

		// Check target within seeker angle and LOS to target is clear.
		bool seekerAnglesOk = CheckSeekerAngle(projOrigin, projVel, targetPos, allowLoft);
		bool losToTarget = TraceLOS(projOrigin, targetPos);
		if (!seekerAnglesOk || !losToTarget)
		{
			m_iSeekerLostCount = m_iSeekerLostCount + 1;
			if (flightTime - m_fTargetLastSeenTime > m_fNoTargetDestructTime)
			{
				m_eTargetData.detonated = 1;
				m_bSelfDestructed = true;
			}

			m_eTargetData.targetPosition = vector.Zero;
			return m_eTargetData;
		}

		m_fTargetLastSeenTime = flightTime;

		// Calculate lead
		float projectileSpeed = projVel.Length();
		vector calculatedLead = vector.Zero;
		if (projectileSpeed > 0.01 && targetPhys)
		{
			float distanceToTarget = vector.Distance(targetPos, projOrigin);
			float timeToHit = distanceToTarget / projectileSpeed;
			calculatedLead = targetPhys.GetVelocity() * timeToHit;
		}

		targetData.targetPosition = targetPos + calculatedLead;
		m_eTargetData = targetData;
		return m_eTargetData;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsEntityOrChildOf(IEntity ent, IEntity ancestor)
	{
		if (!ent || !ancestor)
			return false;

		IEntity cur = ent;
		while (cur)
		{
			if (cur == ancestor)
				return true;

			cur = cur.GetParent();
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool TraceLOS(vector from, vector to)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};

		m_aTraceExclude.Clear();
		if (m_eProjectile)
			m_aTraceExclude.Insert(m_eProjectile);
		if (target)
			m_aTraceExclude.Insert(target);

		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		m_TraceParam.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.Exclude = null;
		m_TraceParam.ExcludeArray = m_aTraceExclude;
		m_TraceParam.TraceEnt = null;

		float percent = world.TraceMove(m_TraceParam, null);
		if (percent == 1.0)
			return true;

		if (m_TraceParam.TraceEnt && IsEntityOrChildOf(m_TraceParam.TraceEnt, target))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool CheckSeekerAngle(vector seekerPos, vector seekerDirection, vector targetPos, bool allowLoft)
	{
		if (seekerDirection.LengthSq() < 0.000001)
			return false;

		vector toTarget = vector.Direction(seekerPos, targetPos);
		if (toTarget.LengthSq() < 0.000001)
			return true;

		vector seekerDirNorm = seekerDirection.Normalized();
		vector toTargetNorm = toTarget.Normalized();
		float dot3D = Math.Clamp(vector.Dot(seekerDirNorm, toTargetNorm), -1.0, 1.0);

		if (!allowLoft)
			return dot3D > Math.Cos(m_fSeekerFieldOfView * Math.DEG2RAD);

		// During TOP-attack climb/coast, allow steep pitch loft up to 85 deg while still enforcing
		// horizontal azimuth within m_fSeekerFieldOfView.
		if (dot3D <= Math.Cos(85.0 * Math.DEG2RAD))
			return false;

		vector seekerFlat = Vector(seekerDirNorm[0], 0.0, seekerDirNorm[2]);
		vector targetFlat = Vector(toTargetNorm[0], 0.0, toTargetNorm[2]);
		if (seekerFlat.LengthSq() < 0.000001 || targetFlat.LengthSq() < 0.000001)
			return true;

		float dotFlat = Math.Clamp(vector.Dot(seekerFlat.Normalized(), targetFlat.Normalized()), -1.0, 1.0);
		return dotFlat > Math.Cos(m_fSeekerFieldOfView * Math.DEG2RAD);
	}

	//------------------------------------------------------------------------------------------------
	int GetSeekerLostCount()
	{
		return m_iSeekerLostCount;
	}

	//------------------------------------------------------------------------------------------------
	override bool WasSelfDestructed()
	{
		return m_bSelfDestructed;
	}
};
