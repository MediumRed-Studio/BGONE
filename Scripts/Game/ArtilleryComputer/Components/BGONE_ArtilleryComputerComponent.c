[ComponentEditorProps(category: "GameScripted/Artillery", description: "Artillery Computer Component for ballistic trajectory calculation and automated turret alignment")]
class BGONE_ArtilleryComputerComponentClass : ScriptGameComponentClass
{
}

class BGONE_ArtilleryComputerComponent : ScriptGameComponent
{
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

		vector currentPos = m_eOwner.GetOrigin();

		// ScreenToWorld expects (screenPosX, screenPosY, out worldX, out worldZ)
		float worldX, worldZ;
		m_MapEntity.ScreenToWorld(selectedPos[0], selectedPos[1], worldX, worldZ);
		float terrainHeight = GetGame().GetWorld().GetSurfaceY(worldX, worldZ);
		vector targetWorldPos = Vector(worldX, terrainHeight, worldZ);

		float elevationDifference = targetWorldPos[1] - (currentPos[1] + 1.5);
		float range = vector.Distance(Vector(currentPos[0], 0, currentPos[2]), Vector(targetWorldPos[0], 0, targetWorldPos[2]));

		float muzzleVelocity = 150.0;
		if (m_TurretController.GetWeaponManager())
		{
			BaseWeaponComponent weapon = m_TurretController.GetWeaponManager().GetCurrentWeapon();
			if (weapon)
			{
				BaseMagazineComponent mag = weapon.GetCurrentMagazine();
				if (mag)
					muzzleVelocity = 150.0;
			}
		}

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
