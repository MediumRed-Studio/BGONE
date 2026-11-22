class BGONE_ArtilleryComputerComponentClass : ScriptComponentClass
{

}

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
		if (!mapEntity || !user)
			return;

		SCR_ChimeraCharacter userCharacter = SCR_ChimeraCharacter.Cast(user);
		if (!userCharacter)
			return;

		CompartmentAccessComponent compAccess = userCharacter.GetCompartmentAccessComponent();
		if (!compAccess)
			return;

		BaseCompartmentSlot compartment = compAccess.GetCompartment();
		if (!compartment)
			return;

		m_TurretController = TurretControllerComponent.Cast(compartment.GetController());
		if (!m_TurretController)
			return;

		TurretComponent turretComp = m_TurretController.GetTurretComponent();
		if (!turretComp)
			return;
		
		// Todo, check aiming limits.
		turretComp.GetAimingLimits(limitsHoriz, limitsVert);
		
		m_MapEntity = mapEntity;
		
		m_MapEntity.GetOnMapOpen().Insert(OnMapOpen);
		m_MapEntity.GetOnMapClose().Insert(OnMapClose);
		m_MapEntity.GetOnSelection().Insert(OnMapSelection);
		GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MB_ArtilleryComputer);
	}
	protected float CalculatePitchAngle(float range, float elevationDifference, float initialVelocity)
    {
		IEntity worldEntity = GetGame().GetWorldEntity();
		if (!worldEntity)
			return 45 * Math.DEG2RAD;

		float gravity = PhysicsWorld.GetGravity(worldEntity)[1] * -1;
        
		Math.Pow(initialVelocity,4) - (gravity * ((gravity * Math.Pow(range,2)) + (2 * elevationDifference * Math.Pow(initialVelocity,2))));
        float term2 = (gravity * Math.Pow(range, 2)) - (2 * elevationDifference * Math.Pow(initialVelocity, 2));

        // Calculate the discriminant
        float discriminant = Math.Pow(initialVelocity, 2) + (gravity * term2);
        // Check if the projectile can reach the target
        if (discriminant < 0)
        {
            Print("Cant Reach Target Distance");
			return 45 * Math.DEG2RAD;
        }
			
        float numerator = initialVelocity * initialVelocity + Math.Sqrt(discriminant);
        float denominator = gravity * range;

        // Calculate the pitch angle in radians
        float pitchAngleRadians = Math.Atan2(numerator, denominator);

        return pitchAngleRadians;
    }
	protected void CenterMapOnVehicle()
	{
		if (!m_eOwner || !m_MapEntity)
			return;

		vector o = m_eOwner.GetOrigin();
		int x, y;
		m_MapEntity.WorldToScreen(o[0], o[2], x, y);
		m_MapEntity.PanSmooth(x, y);
	}
	void OnMapSelection(vector selectedPos)
	{
		if (!m_MapEntity || !m_eOwner || !m_TurretController)
			return;

		TurretComponent turretComp = m_TurretController.GetTurretComponent();
		BaseWorld world = m_MapEntity.GetWorld();
		if (!turretComp || !world)
			return;

		float wX, wY;
		m_MapEntity.ScreenToWorld(selectedPos[0],selectedPos[2],wX,wY);
		float heightAtPos = world.GetSurfaceY(wX,wY);
		vector worldTarget = Vector(wX,heightAtPos,wY);
		
		vector currentPos = m_eOwner.GetOrigin();
		float distance = vector.DistanceXZ(currentPos, worldTarget);
		float elevDiff = currentPos[1] + 3 - worldTarget[1];
		Print("Selected Pos : " + worldTarget);
		Print("Distance : " + distance);
		Print("Elevation Difference : " + elevDiff);
		
		
		float aimPitch = CalculatePitchAngle(distance,elevDiff,150);
		Print("Required Angle :" + aimPitch * Math.RAD2DEG);
		
		vector aimDir = worldTarget - currentPos;
		float wantedYaw = aimDir.ToYaw();
		float yawDiff = wantedYaw - m_eOwner.GetYawPitchRoll()[0];
		turretComp.SetAimingRotation(Vector(yawDiff * Math.DEG2RAD,aimPitch,0));
		Print("AimDir: " + turretComp.GetAimingDirectionWorld().VectorToAngles());
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
		if (!m_MapEntity)
			return;

		m_MapEntity.GetOnMapOpen().Remove(OnMapOpen);
		m_MapEntity.GetOnMapClose().Remove(OnMapClose);
		m_MapEntity.GetOnSelection().Remove(OnMapSelection);
	}
}