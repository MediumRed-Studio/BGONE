[ComponentEditorProps(category: "GameScripted/Artillery", description: "Artillery Computer Component for ballistic trajectory calculation and automated turret alignment")]
class BGONE_ArtilleryComputerComponentClass : ScriptGameComponentClass
{
}

class BGONE_ArtilleryComputerComponent : ScriptGameComponent
{
	protected const float MUZZLE_VELOCITY_DEFAULT = 150.0;
	protected const float TURRET_EYE_HEIGHT = 1.5;
	
	protected SCR_MapEntity m_MapEntity;
	protected TurretControllerComponent m_TurretController;
	protected IEntity m_eOwner;

	override void OnPostInit(IEntity owner)
	{
		m_eOwner = owner;
	}

	void ~BGONE_ArtilleryComputerComponent()
	{
		if(m_MapEntity)
		{
			m_MapEntity.GetOnSelection().Remove(OnMapSelection);
			m_MapEntity = null;
		}
	}

	void OpenComputer(SCR_MapEntity mapEntity, SCR_ChimeraCharacter userCharacter)
	{
		if (!mapEntity || !userCharacter)
			return;

		m_MapEntity = mapEntity;

		// Resolve turret controller from user's compartment
		CompartmentAccessComponent accessComp = CompartmentAccessComponent.Cast(userCharacter.FindComponent(CompartmentAccessComponent));
		if (accessComp)
		{
			BaseCompartmentSlot compartment = accessComp.GetCompartment();
			if (compartment)
			{
				IEntity turretEntity = compartment.GetOwner();
				if (turretEntity)
					m_TurretController = TurretControllerComponent.Cast(turretEntity.FindComponent(TurretControllerComponent));
			}
		}

		if (!m_TurretController && m_eOwner)
		{
			m_TurretController = TurretControllerComponent.Cast(m_eOwner.FindComponent(TurretControllerComponent));
		}

		GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MB_ArtilleryComputer);
		mapEntity.GetOnSelection().Insert(OnMapSelection);
		mapEntity.GetOnMapClose().Insert(OnMapClosed);

		GetGame().GetCallqueue().CallLater(CenterMapOnVehicle, 100, false);
	}

	protected void OnMapClosed(MapConfiguration config)
	{
		if(m_MapEntity)
		{
			m_MapEntity.GetOnSelection().Remove(OnMapSelection);
			m_MapEntity.GetOnMapClose().Remove(OnMapClosed);
		}
	}

	protected float CalculatePitchAngle(float initialVelocity, float range, float elevationDifference)
	{
		if (range < 0.001)
			return 85.0 * Math.DEG2RAD;

		float gravity = 9.80665;
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			IEntity worldEntity = GetGame().GetWorldEntity();
			if (worldEntity)
				gravity = PhysicsWorld.GetGravity(worldEntity).Length();
		}

		float v2 = initialVelocity * initialVelocity;
		float v4 = v2 * v2;
		float gx2 = gravity * range * range;
		float term2 = gravity * (gx2 + 2.0 * elevationDifference * v2);
		float discriminant = v4 - term2;

		if (discriminant < 0.0)
		{
			// Target beyond maximum physical ballistic range
			return 45.0 * Math.DEG2RAD;
		}

		float sqrtDisc = Math.Sqrt(discriminant);
		float numerator = v2 + sqrtDisc;
		float denominator = gravity * range;

		return Math.Atan2(numerator, denominator);
	}

	protected void OnMapSelection(vector selectedPos)
	{
		if (!m_TurretController || !m_eOwner || !m_MapEntity)
			return;
		
		BaseWorld world = GetGame().GetWorld();
		if(!world)
			return;

		// GetOnSelection carries scaled screen coordinates packed as
		// (x, 0, y): transform to canvas-world with indices [0],[2], the
		// vanilla SCR_MapCommandCursor pattern and what upstream used. The
		// fork's [0],[1] variant read the constant 0 and aimed off-map.
		// Rounded to whole pixels: ScreenToWorld takes ints (editors show
		// the implicit float->int narrowing as a warning otherwise).
		// In-game check: two clicks 2 km apart N/S must move impacts in Z;
		// if every click lands on one latitude, this assumption is wrong.
		float worldX, worldZ;
		m_MapEntity.ScreenToWorld(Math.Round(selectedPos[0]), Math.Round(selectedPos[2]), worldX, worldZ);
		float terrainHeight = world.GetSurfaceY(worldX, worldZ);
		vector targetWorldPos = Vector(worldX, terrainHeight, worldZ);

		vector currentPos = m_eOwner.GetOrigin();
		float elevationDifference = targetWorldPos[1] - (currentPos[1] + TURRET_EYE_HEIGHT);
		float range = vector.Distance(Vector(currentPos[0], 0, currentPos[2]), Vector(targetWorldPos[0], 0, targetWorldPos[2]));

		// TODO: derive from the loaded shell once a verified 1.8 source is
		// available. TurretControllerComponent exposes no GetWeaponManager
		// (API-verified), so the old manager chain could not be ported;
		// 150 m/s is the M72 rocket placeholder the ballistics were tuned
		// against (see BGONE_Ammo_Missile_* MissileMoveComponent InitSpeed).
		float muzzleVelocity = MUZZLE_VELOCITY_DEFAULT;

		float pitchAngleRad = CalculatePitchAngle(muzzleVelocity, range, elevationDifference);
		float pitchAngleDeg = pitchAngleRad * Math.RAD2DEG;

		vector toTargetWorld = targetWorldPos - currentPos;
		vector worldAngles = toTargetWorld.VectorToAngles();
		float targetYawWorld = worldAngles[0];

		// Transform world yaw into local turret coordinate frame
		vector vehicleAngles = m_eOwner.GetYawPitchRoll();
		float vehicleYaw = vehicleAngles[0];
		float localYaw = Math.MapAngle(targetYawWorld - vehicleYaw);

		if(m_TurretController.GetOwner())
		{
			TurretComponent turretComp = TurretComponent.Cast(m_TurretController.GetOwner().FindComponent(TurretComponent));
			if(turretComp)
				turretComp.SetAimingRotationWanted(Vector(localYaw, pitchAngleDeg, 0));
		}

		m_MapEntity.GetOnSelection().Remove(OnMapSelection);
		m_MapEntity.CloseMap();
	}

	protected void CenterMapOnVehicle()
	{
		if (m_MapEntity && m_eOwner)
		{
			m_MapEntity.SetPan(m_eOwner.GetOrigin()[0], m_eOwner.GetOrigin()[2], true, true);
		}
	}
}
