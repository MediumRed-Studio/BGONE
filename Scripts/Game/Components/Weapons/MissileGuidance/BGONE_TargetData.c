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
	
	SCR_ChimeraCharacter GetShooterEntity()
	{
		if(!shooterEntity && shooterRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(shooterRplId));
			if(rpl)
				shooterEntity = SCR_ChimeraCharacter.Cast(rpl.GetEntity());
		}
		return shooterEntity;
	}
	
	IEntity GetTargetEntity()
	{
		if(!targetEntity && targetRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(targetRplId));
			if(rpl)
				targetEntity = rpl.GetEntity();
		}
		return targetEntity;
	}
	
	TurretControllerComponent GetTurretEntity()
	{
		if(!turretEntity && turretRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(turretRplId));
			if(rpl && rpl.GetEntity())
				turretEntity = TurretControllerComponent.Cast(rpl.GetEntity().FindComponent(TurretControllerComponent));
		}
		return turretEntity;
	}
	
	void DEBUG_PrintValues()
	{
		Print("----- Target Data Values ------");
		Print("shooterRplId: " + shooterRplId);
		Print("turretRplId: " + turretRplId);
		Print("attackProfileMode: " + attackProfileIndex);
		Print("armingDistance: " + armingDistancesIndex);
		Print("detonated: " + detonated);
		Print("launchPos: " + launchPos);
		Print("launchDir: " + launchDir);
		Print("targetPosition: " + targetPosition);
		Print("yawChange: " + yawChange);
		Print("pitchChange: " + pitchChange);
		Print("targetRplId: " + targetRplId);
	}
	
	// Replication Methods
	static bool Extract(BGONE_TargetData instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		snapshot.SerializeInt(instance.shooterRplId);
		snapshot.SerializeInt(instance.turretRplId);
		snapshot.SerializeInt(instance.attackProfileIndex);
		snapshot.SerializeInt(instance.armingDistancesIndex);
		snapshot.SerializeInt(instance.detonated);
		snapshot.SerializeVector(instance.launchPos);
		snapshot.SerializeVector(instance.launchDir);
		snapshot.SerializeVector(instance.targetPosition);
		snapshot.SerializeFloat(instance.yawChange);
		snapshot.SerializeFloat(instance.pitchChange);
		snapshot.SerializeInt(instance.targetRplId);
		return true;
	}

	static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, BGONE_TargetData instance)
	{
		snapshot.SerializeInt(instance.shooterRplId);
		snapshot.SerializeInt(instance.turretRplId);
		snapshot.SerializeInt(instance.attackProfileIndex);
		snapshot.SerializeInt(instance.armingDistancesIndex);
		snapshot.SerializeInt(instance.detonated);
		snapshot.SerializeVector(instance.launchPos);
		snapshot.SerializeVector(instance.launchDir);
		snapshot.SerializeVector(instance.targetPosition);
		snapshot.SerializeFloat(instance.yawChange);
		snapshot.SerializeFloat(instance.pitchChange);
		snapshot.SerializeInt(instance.targetRplId);
		return true;
	}

	static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
	{
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeVector(packet);
		snapshot.EncodeFloat(packet);
		snapshot.EncodeFloat(packet);
		snapshot.EncodeInt(packet);
	}

	static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeVector(packet);
		snapshot.DecodeFloat(packet);
		snapshot.DecodeFloat(packet);
		snapshot.DecodeInt(packet);
		return true;
	}

	static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs , ScriptCtx ctx)
	{
		// 5 ints (20) + 3 vectors (36) + 2 floats (8) + 1 int (4) = 68 bytes
		return lhs.CompareSnapshots(rhs, 68);
	}

	static bool PropCompare(BGONE_TargetData instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
	{
		return snapshot.CompareInt(instance.shooterRplId)
		 	&& snapshot.CompareInt(instance.turretRplId)
		    && snapshot.CompareInt(instance.attackProfileIndex)
		    && snapshot.CompareInt(instance.armingDistancesIndex)
		    && snapshot.CompareInt(instance.detonated)
		    && snapshot.CompareVector(instance.launchPos)
		    && snapshot.CompareVector(instance.launchDir)
		    && snapshot.CompareVector(instance.targetPosition)
		    && snapshot.CompareFloat(instance.yawChange)
		    && snapshot.CompareFloat(instance.pitchChange)
		    && snapshot.CompareInt(instance.targetRplId);
	}

	static void EncodeDelta(SSnapSerializerBase oldSnapshot, SSnapSerializerBase newSnapshot, ScriptCtx ctx, ScriptBitSerializer packet)
	{
		int oldInt;
		oldSnapshot.SerializeInt(oldInt);
		int newInt;
		newSnapshot.SerializeInt(newInt);
		int deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);

		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);
		
		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);

		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);
		
		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);
		
		vector oldVector;
		oldSnapshot.SerializeVector(oldVector);
		vector newVector;
		newSnapshot.SerializeVector(newVector);
		bool vectorChanged = newVector != oldVector;
		packet.Serialize(vectorChanged, 1);
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		
		oldSnapshot.SerializeVector(oldVector);
		newSnapshot.SerializeVector(newVector);
		vectorChanged = newVector != oldVector;
		packet.Serialize(vectorChanged, 1);
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		
		oldSnapshot.SerializeVector(oldVector);
		newSnapshot.SerializeVector(newVector);
		vectorChanged = newVector != oldVector;
		packet.Serialize(vectorChanged, 1);
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		
		float oldFloat;
		oldSnapshot.SerializeFloat(oldFloat);
		float newFloat;
		newSnapshot.SerializeFloat(newFloat);
		bool floatChanged = newFloat != oldFloat;
		packet.Serialize(floatChanged, 1);
		if (floatChanged)
			packet.Serialize(newFloat, 32);
		
		oldSnapshot.SerializeFloat(oldFloat);
		newSnapshot.SerializeFloat(newFloat);
		floatChanged = newFloat != oldFloat;
		packet.Serialize(floatChanged, 1);
		if (floatChanged)
			packet.Serialize(newFloat, 32);
		
		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);
	}

	static void DecodeDelta(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase oldSnapshot, SSnapSerializerBase newSnapshot)
	{
		int oldInt;
		oldSnapshot.SerializeInt(oldInt);
		int deltaInt;
		packet.SerializeInt(deltaInt);
		int newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);

		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
		
		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
		
		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
		
		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
		
		vector oldVector;
		oldSnapshot.SerializeVector(oldVector);
		bool vectorChanged;
		packet.Serialize(vectorChanged, 1);
		vector newVector;
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		else
			newVector = oldVector;
		newSnapshot.SerializeVector(newVector);
		
		oldSnapshot.SerializeVector(oldVector);
		packet.Serialize(vectorChanged, 1);
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		else
			newVector = oldVector;
		newSnapshot.SerializeVector(newVector);
		
		oldSnapshot.SerializeVector(oldVector);
		packet.Serialize(vectorChanged, 1);
		if (vectorChanged)
			packet.Serialize(newVector, 96);
		else
			newVector = oldVector;
		newSnapshot.SerializeVector(newVector);
		
		float oldFloat;
		oldSnapshot.SerializeFloat(oldFloat);
		bool floatChanged;
		packet.Serialize(floatChanged, 1);
		float newFloat;
		if (floatChanged)
			packet.Serialize(newFloat, 32);
		else
			newFloat = oldFloat;
		newSnapshot.SerializeFloat(newFloat);
		
		oldSnapshot.SerializeFloat(oldFloat);
		packet.Serialize(floatChanged, 1);
		if (floatChanged)
			packet.Serialize(newFloat, 32);
		else
			newFloat = oldFloat;
		newSnapshot.SerializeFloat(newFloat);
		
		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
	}
}
