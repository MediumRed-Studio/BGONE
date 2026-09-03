[BaseContainerProps()]
class BGONE_SeekerType_SACLOS : BGONE_SeekerType_Base
{
	[Attribute("5.6", UIWidgets.Slider, "How Many Seconds Until The Missile Self Destructs (origin value; engine TTL 5.6 fires first)", "0 30 0.1", category: "BGONE")]
	protected float m_fTimeToLive;

	[Attribute("60", UIWidgets.Slider, "Seeker Half-Angle In Degrees: Missile Must Stay Within This Deviation From The Shooter Aim Line (upstream parity values: 30 VIS / 60 SACLOS)", "0 90 0.1", category: "BGONE")]
	protected float m_fSeekerFOV;
	
	[Attribute("100", UIWidgets.Slider, "Min Distance From Launch Before Missile Arms", "0 1000 1", category: "BGONE")]
	protected int m_iArmingDistance;

	protected vector m_vAimDir;
	protected vector m_vAimPos;
	protected vector m_vProjectilePos;
	protected float m_fTimeOfLastAimUpdate;
	
	protected ref TraceParam m_TraceParam;
	protected ref array<IEntity> m_aExcludeEntities;

	override void InitSeeker(Projectile projectile, BGONE_TargetData targetData)
	{
		super.InitSeeker(projectile, targetData);
		m_TraceParam = new TraceParam();
		m_aExcludeEntities = new array<IEntity>();
	}

	override array<int> GetAvailableArmingDistances()
	{
		return {m_iArmingDistance};
	}

	void UpdateAimingDirServer(vector aimDir, vector aimPos)
	{
		m_vAimDir = aimDir;
		m_vAimPos = aimPos;
		m_fTimeOfLastAimUpdate = GetGame().GetWorld().GetWorldTime();
	}

	override BGONE_TargetData ProcessFrame(BGONE_TargetData targetData, float flightTime)
	{
		if(!targetData || !m_eProjectile)
			return targetData;

		if(m_fTimeToLive > 0 && flightTime > m_fTimeToLive)
		{
			targetData.detonated = EBGONE_DetonationState.IMPACT;
			return targetData;
		}
		
		m_vProjectilePos = m_eProjectile.GetOrigin();

		SCR_ChimeraCharacter shooter = targetData.GetShooterEntity();
		TurretControllerComponent turret = targetData.GetTurretEntity();
		
		vector aimDir = m_vAimDir;
		vector aimPos = m_vAimPos;

		// Fallback to server values if no client update for > 0.5 seconds (500ms, origin value).
		// Owner-client copies never simulate (proxy-gated), so this fallback
		// steers host-authority and dedicated-server missiles; owners see the
		// server-driven proxy via the 20 Hz transform sync.
		if(GetGame().GetWorld().GetWorldTime() - m_fTimeOfLastAimUpdate > 500.0)
		{
			if(turret && turret.GetOwner())
			{
				TurretComponent turretComp = TurretComponent.Cast(turret.GetOwner().FindComponent(TurretComponent));
				if(turretComp)
				{
					aimDir = turretComp.GetAimingDirectionWorld();
					aimPos = turret.GetOwner().GetOrigin();
				}
				else
				{
					vector mat[4];
					turret.GetOwner().GetWorldTransform(mat);
					aimDir = mat[2];
					aimPos = turret.GetOwner().GetOrigin();
				}
			}
			else if(shooter)
			{
				AimingComponent headAim = shooter.GetHeadAimingComponent();
				if(headAim)
				{
					aimDir = headAim.GetAimingDirectionWorld();
					aimDir[0] = -aimDir[0]; // Upstream character azimuth correction
					aimPos = shooter.EyePosition();
				}
				else
				{
					aimDir = shooter.EyePosition() - m_eProjectile.GetOrigin();
					aimDir.Normalize();
					aimPos = shooter.EyePosition();
				}
			}
		}

		if(aimDir == Vector(0,0,0))
			return targetData;

		// FOV validation
		vector toProj = m_vProjectilePos - aimPos;
		if(toProj.Length() > 0.01)
		{
			float dot = Math.Clamp(vector.Dot(aimDir.Normalized(), toProj.Normalized()), -1.0, 1.0);
			float angle = Math.Acos(dot) * Math.RAD2DEG;
			if(angle > m_fSeekerFOV)
			{
				// Projectile outside line of sight cone
				return targetData;
			}
		}

		// Line of Sight check
		IEntity shooterEnt = shooter;
		if(!shooterEnt && turret)
			shooterEnt = turret.GetOwner();
			
		if(!TraceLOS(aimPos, m_vProjectilePos, shooterEnt))
		{
			return targetData;
		}

		// Compute forward aim target
		float distTraveled = GetDistanceFromLaunch(targetData);
		targetData.targetPosition = aimPos + (aimDir * (distTraveled + 10.0));
		
		return targetData;
	}

	protected bool TraceLOS(vector from, vector to, IEntity shooter)
	{
		if(vector.DistanceSq(from, to) < 0.01)
			return true;
			
		m_aExcludeEntities.Clear();
		m_aExcludeEntities.Insert(m_eProjectile);
		if(shooter)
		{
			m_aExcludeEntities.Insert(shooter);
			if(shooter.GetRootParent())
				m_aExcludeEntities.Insert(shooter.GetRootParent());
		}
		
		m_TraceParam.Start = from;
		m_TraceParam.End = to;
		m_TraceParam.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		m_TraceParam.ExcludeArray = m_aExcludeEntities;
		m_TraceParam.LayerMask = EPhysicsLayerDefs.Projectile;
		
		float fraction = GetGame().GetWorld().TraceMove(m_TraceParam, null);
		return (fraction >= 0.98);
	}
}
