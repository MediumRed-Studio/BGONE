
[BaseContainerProps()]
class BGONE_SeekerType_SACLOS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How Many Seconds Until The Missile Self Destructs.","0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	[Attribute("60", UIWidgets.Slider, "Degrees The Seeker Can See.","0 360 1", category: "BGONE")]
	protected float m_fSeekerAngle;

	protected float m_fDistanceFromLaunch;
	protected vector m_fProjectilePos;
	protected ref BGONE_TargetData m_eTargetData;

	protected vector ownerAimDir;
	protected vector ownerAimPos;
	protected float lastOwnerUpdate;
	protected bool m_bHasOwnerAimUpdate = false;

	protected SCR_ChimeraCharacter m_eShooter;
	protected TurretControllerComponent m_eTurret;

	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aTraceExclude;
	protected IEntity m_TraceShooterRoot;
	protected IEntity m_TraceTurretRoot;

	//------------------------------------------------------------------------------------------------
	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_eTargetData = targetData;
		ownerAimDir = vector.Zero;
		ownerAimPos = vector.Zero;
		lastOwnerUpdate = 0.0;
		m_bHasOwnerAimUpdate = false;

		if (!m_TraceParam)
			m_TraceParam = new TraceParam();

		if (!m_aTraceExclude)
			m_aTraceExclude = {};
	}

	//------------------------------------------------------------------------------------------------
	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if (!m_eProjectile || !targetData)
			return targetData;

		m_fProjectilePos = m_eProjectile.GetOrigin();
		m_fDistanceFromLaunch = vector.Distance(targetData.launchPos, m_fProjectilePos);

		SCR_ChimeraCharacter shooter = targetData.GetShooterEntity();
		TurretControllerComponent turret = targetData.GetTurretEntity();

		vector aimDir = vector.Zero;
		vector aimPos = vector.Zero;

		BaseWorld world = GetGame().GetWorld();
		float worldTime = 0.0;
		if (world)
			worldTime = world.GetWorldTime();

		// Retain owner's latest aim across frames while fresh (<= 500 ms); otherwise fall back to server aim.
		if (m_bHasOwnerAimUpdate && ownerAimDir.LengthSq() > 0.0001 && (worldTime - lastOwnerUpdate) <= 500.0)
		{
			aimDir = ownerAimDir;
			aimPos = ownerAimPos;
		}
		else
		{
			if (turret)
			{
				BaseSightsComponent sights = turret.GetCurrentSights();
				if (sights)
				{
					aimDir = sights.GetSightsDirectionUntransformed();
					aimPos = sights.GetSightsRearPosition();
				}
			}
			else if (shooter)
			{
				AimingComponent headAim = shooter.GetHeadAimingComponent();
				if (headAim)
				{
					aimDir = headAim.GetAimingDirectionWorld();
					aimPos = shooter.EyePosition();
				}
			}
		}

		if (aimDir.LengthSq() < 0.0001)
		{
			targetData.targetPosition = vector.Zero;
		}
		else
		{
			aimDir.Normalize();
			vector toProj = vector.Direction(aimPos, m_fProjectilePos);
			float distToProjSq = toProj.LengthSq();

			// Guard near-zero distance right after launch (< 0.5m) so dot/LOS checks cannot falsely drop guidance.
			if (distToProjSq < 0.25)
			{
				targetData.targetPosition = aimPos + aimDir * (m_fDistanceFromLaunch + 10.0);
			}
			else
			{
				vector directionNormal = toProj / Math.Sqrt(distToProjSq);
				float dotProd = Math.Clamp(vector.Dot(aimDir, directionNormal), -1.0, 1.0);

				bool losToProjectile = TraceLOS(aimPos, m_fProjectilePos, shooter, turret);
				if (dotProd < Math.Cos(m_fSeekerAngle * Math.DEG2RAD) || !losToProjectile)
					targetData.targetPosition = vector.Zero;
				else
					targetData.targetPosition = aimPos + aimDir * (m_fDistanceFromLaunch + 10.0);
			}
		}

		m_eTargetData = targetData;

		if (flightTime > m_fTimeToLive)
			m_eTargetData.detonated = 1;

		return m_eTargetData;
	}

	//------------------------------------------------------------------------------------------------
	void UpdateAimingDirServer(vector inAimDir, vector inAimPos)
	{
		if (inAimDir.LengthSq() < 0.0001)
			return;

		ownerAimDir = inAimDir.Normalized();
		ownerAimPos = inAimPos;
		m_bHasOwnerAimUpdate = true;

		BaseWorld world = GetGame().GetWorld();
		if (world)
			lastOwnerUpdate = world.GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsDescendantOf(IEntity ent, IEntity root)
	{
		if (!ent || !root)
			return false;

		IEntity cur = ent;
		while (cur)
		{
			if (cur == root)
				return true;

			cur = cur.GetParent();
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool FilterSaclosLOS(IEntity ent)
	{
		if (!ent)
			return false;

		if (ent == m_eProjectile)
			return false;

		if (m_TraceShooterRoot && IsDescendantOf(ent, m_TraceShooterRoot))
			return false;

		if (m_TraceTurretRoot && IsDescendantOf(ent, m_TraceTurretRoot))
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool TraceLOS(vector from, vector to, IEntity shooter, TurretControllerComponent turret)
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

		m_TraceShooterRoot = null;
		if (shooter)
		{
			m_aTraceExclude.Insert(shooter);
			m_TraceShooterRoot = shooter.GetRootParent();
			if (!m_TraceShooterRoot)
				m_TraceShooterRoot = shooter;
			else if (m_TraceShooterRoot != shooter)
				m_aTraceExclude.Insert(m_TraceShooterRoot);
		}

		m_TraceTurretRoot = null;
		if (turret && turret.GetOwner())
		{
			IEntity turretEnt = turret.GetOwner();
			if (!m_aTraceExclude.Contains(turretEnt))
				m_aTraceExclude.Insert(turretEnt);

			m_TraceTurretRoot = turretEnt.GetRootParent();
			if (!m_TraceTurretRoot)
				m_TraceTurretRoot = turretEnt;
			else if (!m_aTraceExclude.Contains(m_TraceTurretRoot))
				m_aTraceExclude.Insert(m_TraceTurretRoot);
		}

		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		m_TraceParam.Flags = TraceFlags.ANY_CONTACT | TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.Exclude = null;
		m_TraceParam.ExcludeArray = m_aTraceExclude;
		m_TraceParam.TraceEnt = null;

		float percent = world.TraceMove(m_TraceParam, FilterSaclosLOS);
		m_TraceShooterRoot = null;
		m_TraceTurretRoot = null;

		if (percent == 1.0)
			return true;

		return false;
	}
};
