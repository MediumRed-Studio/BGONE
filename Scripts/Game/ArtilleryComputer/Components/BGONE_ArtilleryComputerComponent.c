class BGONE_ArtilleryComputerComponentClass : ScriptGameComponentClass
{
};

class BGONE_ArtilleryComputerComponent : ScriptComponent
{
	protected bool m_bIsFirstTimeOpened = true;
	protected SCR_MapEntity m_MapEntity;
	protected IEntity m_eOwner;
	protected TurretComponent m_Turret;
	protected TurretControllerComponent m_TurretController;
	protected vector limitsHoriz, limitsVert;
	
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}
	
	override void EOnInit(IEntity owner)
	{
		m_eOwner = owner;
	}
	
	void OpenComputer(SCR_MapEntity mapEntity, IEntity user)
	{
		SCR_ChimeraCharacter userCharacter = SCR_ChimeraCharacter.Cast(user);
		if(!userCharacter)
			return;
			
		CompartmentAccessComponent access = userCharacter.GetCompartmentAccessComponent();
		if(!access || !access.GetCompartment())
			return;
			
		m_TurretController = TurretControllerComponent.Cast(access.GetCompartment().GetController());
		if(!m_TurretController)
			return;
		
		if(m_TurretController.GetTurretComponent())
			m_TurretController.GetTurretComponent().GetAimingLimits(limitsHoriz, limitsVert);
		
		m_MapEntity = mapEntity;
		if(!m_MapEntity)
			m_MapEntity = SCR_MapEntity.GetMapInstance();
			
		if(m_MapEntity)
		{
			m_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
			m_MapEntity.GetOnMapClose().Remove(OnMapClose);
			m_MapEntity.GetOnSelection().Remove(OnMapSelection);
			
			m_MapEntity.GetOnMapOpen().Insert(OnMapOpen);
			m_MapEntity.GetOnMapClose().Insert(OnMapClose);
			m_MapEntity.GetOnSelection().Insert(OnMapSelection);
		}
		
		GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MB_ArtilleryComputer);
	}
	
	protected float CalculatePitchAngle(float range, float elevationDifference, float initialVelocity)
    {
		float gravity = 9.80665;
		BaseWorld world = GetGame().GetWorld();
		if(world)
		{
			vector gravVec = PhysicsWorld.GetGravity(GetGame().GetWorldEntity());
			if(gravVec != vector.Zero)
				gravity = Math.AbsFloat(gravVec[1]);
		}
        
		if(range < 0.001)
			return 85 * Math.DEG2RAD;
			
		float v2 = initialVelocity * initialVelocity;
		float v4 = v2 * v2;
		float gx2 = gravity * range * range;
		float term2 = gravity * (gx2 + 2 * elevationDifference * v2);
		
        float discriminant = v4 - term2;
        if (discriminant < 0)
        {
            Print("BGONE Artillery - Target out of range (" + range + "m with muzzle velocity " + initialVelocity + " m/s)", LogLevel.WARNING);
			return 45 * Math.DEG2RAD;
        }
			
        float sqrtDisc = Math.Sqrt(discriminant);
        // High arc solution for artillery/mortar:
        float numerator = v2 + sqrtDisc;
        float denominator = gravity * range;

        float pitchAngleRadians = Math.Atan2(numerator, denominator);
        return pitchAngleRadians;
    }
	
	protected void CenterMapOnVehicle()
	{
		if(!m_eOwner || !m_MapEntity)
			return;
			
		vector o = m_eOwner.GetOrigin();
		float x, y;
		m_MapEntity.WorldToScreen(o[0], o[2], x, y);
		m_MapEntity.PanSmooth(x, y);
	}
	
	void OnMapSelection(vector selectedPos)
	{
		if(!m_MapEntity || !m_eOwner || !m_TurretController)
			return;
			
		float wX, wY;
		m_MapEntity.ScreenToWorld(selectedPos[0], selectedPos[1], wX, wY);
		
		BaseWorld world = m_MapEntity.GetWorld();
		if(!world)
			return;
			
		float heightAtPos = world.GetSurfaceY(wX, wY);
		vector worldTarget = Vector(wX, heightAtPos, wY);
		
		vector currentPos = m_eOwner.GetOrigin();
		float distance = vector.DistanceXZ(currentPos, worldTarget);
		float elevDiff = worldTarget[1] - (currentPos[1] + 1.5);
		
		float muzzleVelocity = 150.0;
		BaseWeaponComponent weapon = m_TurretController.GetWeaponManager().GetCurrentWeapon();
		if(weapon)
		{
			BaseMagazineComponent mag = weapon.GetCurrentMagazine();
			if(mag)
			{
				// Keep default or configured velocity
			}
		}
		
		float aimPitch = CalculatePitchAngle(distance, elevDiff, muzzleVelocity);
		
		vector aimDir = worldTarget - currentPos;
		float wantedYaw = aimDir.ToYaw();
		float yawDiff = wantedYaw - m_eOwner.GetYawPitchRoll()[0];
		
		TurretComponent turretComp = m_TurretController.GetTurretComponent();
		if(turretComp)
		{
			turretComp.SetAimingRotation(Vector(yawDiff * Math.DEG2RAD, aimPitch, 0));
		}
	}
	
	protected void OnMapOpen(MapConfiguration config)
	{
		if (!m_bIsFirstTimeOpened)
			return;
		
		m_bIsFirstTimeOpened = false;
		GetGame().GetCallqueue().CallLater(CenterMapOnVehicle);
	}
	
	protected void OnMapClose(MapConfiguration config)
	{
		if(m_MapEntity)
		{
			m_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
			m_MapEntity.GetOnMapClose().Remove(OnMapClose);
			m_MapEntity.GetOnSelection().Remove(OnMapSelection);
		}
	}
};
