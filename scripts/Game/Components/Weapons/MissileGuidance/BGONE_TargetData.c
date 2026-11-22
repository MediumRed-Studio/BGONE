class BGONE_TargetData : ScriptAndConfig
{
	RplId shooterRplId;
	RplId turretRplId;
	int attackProfileIndex;
	int armingDistancesIndex;
	int detonated = 0;
	vector launchPos;
	vector launchDir;
	vector targetPosition;
	float yawChange;
	float pitchChange;
	RplId targetRplId;
	IEntity targetEntity;
	SCR_ChimeraCharacter shooterEntity;
	TurretControllerComponent turretEntity;

	void BGONE_TargetData()
	{
		shooterRplId = RplId.Invalid();
		turretRplId = RplId.Invalid();
		targetRplId = RplId.Invalid();
	}

	static RplId ResolveEntityRplId(IEntity entity)
	{
		if (!entity)
			return RplId.Invalid();

		IEntity cur = entity;
		while (cur)
		{
			RplComponent rpl = RplComponent.Cast(cur.FindComponent(RplComponent));
			if (rpl && rpl.Id().IsValid())
				return rpl.Id();

			cur = cur.GetParent();
		}

		return Replication.FindItemId(entity);
	}

	SCR_ChimeraCharacter GetShooterEntity()
	{
		if (!shooterEntity && shooterRplId.IsValid())
		{
			Managed item = Replication.FindItem(shooterRplId);
			RplComponent rpl = RplComponent.Cast(item);
			if (rpl)
				shooterEntity = SCR_ChimeraCharacter.Cast(rpl.GetEntity());
			else
				shooterEntity = SCR_ChimeraCharacter.Cast(item);
		}
		return shooterEntity;
	}

	IEntity GetTargetEntity()
	{
		if (!targetEntity && targetRplId.IsValid())
		{
			Managed item = Replication.FindItem(targetRplId);
			RplComponent rpl = RplComponent.Cast(item);
			if (rpl)
				targetEntity = rpl.GetEntity();
			else
				targetEntity = IEntity.Cast(item);
		}
		return targetEntity;
	}

	TurretControllerComponent GetTurretEntity()
	{
		if (!turretEntity && turretRplId.IsValid())
		{
			Managed item = Replication.FindItem(turretRplId);
			turretEntity = TurretControllerComponent.Cast(item);
			if (!turretEntity)
			{
				RplComponent rpl = RplComponent.Cast(item);
				if (rpl && rpl.GetEntity())
					turretEntity = TurretControllerComponent.Cast(rpl.GetEntity().FindComponent(TurretControllerComponent));
			}
		}
		return turretEntity;
	}

	void DEBUG_PrintValues()
	{
		Print("----- Target Data Values ------");
		Print("shooterRplId: " + shooterRplId.AsString());
		Print("turretRplId: " + turretRplId.AsString());
		Print("attackProfileMode: " + attackProfileIndex);
		Print("armingDistance: " + armingDistancesIndex);
		Print("detonated: " + detonated);
		Print("launchPos: " + launchPos);
		Print("launchDir: " + launchDir);
		Print("targetPosition: " + targetPosition);
		Print("yawChange: " + yawChange);
		Print("pitchChange: " + pitchChange);
		Print("targetRplId: " + targetRplId.AsString());
	}

	// Replication Methods
	static bool Extract(BGONE_TargetData instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		snapshot.SerializeBytes(instance.shooterRplId, 4);
		snapshot.SerializeBytes(instance.turretRplId, 4);
		snapshot.SerializeInt(instance.attackProfileIndex);
		snapshot.SerializeInt(instance.armingDistancesIndex);
		snapshot.SerializeInt(instance.detonated);
		snapshot.SerializeVector(instance.launchPos);
		snapshot.SerializeVector(instance.launchDir);
		snapshot.SerializeVector(instance.targetPosition);
		snapshot.SerializeFloat(instance.yawChange);
		snapshot.SerializeFloat(instance.pitchChange);
		snapshot.SerializeBytes(instance.targetRplId, 4);
		return true;
	}

	static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, BGONE_TargetData instance)
	{
		snapshot.SerializeBytes(instance.shooterRplId, 4);
		snapshot.SerializeBytes(instance.turretRplId, 4);
		snapshot.SerializeInt(instance.attackProfileIndex);
		snapshot.SerializeInt(instance.armingDistancesIndex);
		snapshot.SerializeInt(instance.detonated);
		snapshot.SerializeVector(instance.launchPos);
		snapshot.SerializeVector(instance.launchDir);
		snapshot.SerializeVector(instance.targetPosition);
		snapshot.SerializeFloat(instance.yawChange);
		snapshot.SerializeFloat(instance.pitchChange);
		snapshot.SerializeBytes(instance.targetRplId, 4);
		return true;
	}

	static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
	{
		RplId shooterId;
		snapshot.SerializeBytes(shooterId, 4);
		packet.SerializeRplId(shooterId);

		RplId turretId;
		snapshot.SerializeBytes(turretId, 4);
		packet.SerializeRplId(turretId);

		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeFloat(packet);
		snapshot.EncodeFloat(packet);

		RplId targetId;
		snapshot.SerializeBytes(targetId, 4);
		packet.SerializeRplId(targetId);
	}

	static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		RplId shooterId;
		packet.SerializeRplId(shooterId);
		snapshot.SerializeBytes(shooterId, 4);

		RplId turretId;
		packet.SerializeRplId(turretId);
		snapshot.SerializeBytes(turretId, 4);

		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeFloat(packet);
		snapshot.DecodeFloat(packet);

		RplId targetId;
		packet.SerializeRplId(targetId);
		snapshot.SerializeBytes(targetId, 4);
		return true;
	}

	static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
	{
		return lhs.CompareSnapshots(rhs, 4 + 4 + 4 + 4 + 4 + 12 + 12 + 12 + 4 + 4 + 4);
	}

	// Realistically only values that should change are detonated and targetposition. maybe targetRplId in case of target switching.
	static bool PropCompare(BGONE_TargetData instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
	{
		return snapshot.Compare(instance.shooterRplId, 4)
			&& snapshot.Compare(instance.turretRplId, 4)
			&& snapshot.CompareInt(instance.attackProfileIndex)
			&& snapshot.CompareInt(instance.armingDistancesIndex)
			&& snapshot.CompareInt(instance.detonated)
			&& snapshot.CompareVector(instance.launchPos)
			&& snapshot.CompareVector(instance.launchDir)
			&& snapshot.CompareVector(instance.targetPosition)
			&& snapshot.CompareFloat(instance.yawChange)
			&& snapshot.CompareFloat(instance.pitchChange)
			&& snapshot.Compare(instance.targetRplId, 4);
	}
}