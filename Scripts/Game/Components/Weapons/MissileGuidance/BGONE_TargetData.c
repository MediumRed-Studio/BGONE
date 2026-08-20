enum EBGONE_DetonationState
{
	NONE = 0,
	IMPACT = 1,
	AIRBURST = 2
}

class BGONE_TargetData : ScriptAndConfig
{
	RplId shooterRplId;
	RplId turretRplId;
	int attackProfileIndex;
	int armingDistancesIndex;
	int detonated = EBGONE_DetonationState.NONE;
	vector launchPos;
	vector launchDir;
	vector targetPosition;
	float yawChange;
	float pitchChange;
	RplId targetRplId;
	
	protected IEntity m_eTargetEntity;
	protected SCR_ChimeraCharacter m_eShooterEntity;
	protected TurretControllerComponent m_eTurretEntity;
	
	SCR_ChimeraCharacter GetShooterEntity()
	{
		if(!m_eShooterEntity && shooterRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(shooterRplId));
			if(rpl)
				m_eShooterEntity = SCR_ChimeraCharacter.Cast(rpl.GetEntity());
		}
		return m_eShooterEntity;
	}
	
	IEntity GetTargetEntity()
	{
		if(!m_eTargetEntity && targetRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(targetRplId));
			if(rpl)
				m_eTargetEntity = rpl.GetEntity();
		}
		return m_eTargetEntity;
	}
	
	TurretControllerComponent GetTurretEntity()
	{
		if(!m_eTurretEntity && turretRplId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(turretRplId));
			if(rpl && rpl.GetEntity())
				m_eTurretEntity = TurretControllerComponent.Cast(rpl.GetEntity().FindComponent(TurretControllerComponent));
		}
		return m_eTurretEntity;
	}
	
	void InvalidateEntities()
	{
		m_eTargetEntity = null;
		m_eShooterEntity = null;
		m_eTurretEntity = null;
	}
	
	// Replication Methods (68 Bytes Exact Snapshot)
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
		instance.InvalidateEntities();
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

	static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
	{
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
		int newInt;
		int deltaInt;

		for(int i = 0; i < 5; i++)
		{
			oldSnapshot.SerializeInt(oldInt);
			newSnapshot.SerializeInt(newInt);
			deltaInt = newInt - oldInt;
			packet.SerializeInt(deltaInt);
		}
		
		vector oldVector;
		vector newVector;
		bool vectorChanged;

		for(int v = 0; v < 3; v++)
		{
			oldSnapshot.SerializeVector(oldVector);
			newSnapshot.SerializeVector(newVector);
			vectorChanged = (newVector != oldVector);
			packet.Serialize(vectorChanged, 1);
			if (vectorChanged)
				packet.Serialize(newVector, 96);
		}
		
		float oldFloat;
		float newFloat;
		bool floatChanged;

		for(int f = 0; f < 2; f++)
		{
			oldSnapshot.SerializeFloat(oldFloat);
			newSnapshot.SerializeFloat(newFloat);
			floatChanged = (newFloat != oldFloat);
			packet.Serialize(floatChanged, 1);
			if (floatChanged)
				packet.Serialize(newFloat, 32);
		}
		
		oldSnapshot.SerializeInt(oldInt);
		newSnapshot.SerializeInt(newInt);
		deltaInt = newInt - oldInt;
		packet.SerializeInt(deltaInt);
	}

	static void DecodeDelta(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase oldSnapshot, SSnapSerializerBase newSnapshot)
	{
		int oldInt;
		int newInt;
		int deltaInt;

		for(int i = 0; i < 5; i++)
		{
			oldSnapshot.SerializeInt(oldInt);
			packet.SerializeInt(deltaInt);
			newInt = oldInt + deltaInt;
			newSnapshot.SerializeInt(newInt);
		}
		
		vector oldVector;
		vector newVector;
		bool vectorChanged;

		for(int v = 0; v < 3; v++)
		{
			oldSnapshot.SerializeVector(oldVector);
			packet.Serialize(vectorChanged, 1);
			if (vectorChanged)
				packet.Serialize(newVector, 96);
			else
				newVector = oldVector;
			newSnapshot.SerializeVector(newVector);
		}
		
		float oldFloat;
		float newFloat;
		bool floatChanged;

		for(int f = 0; f < 2; f++)
		{
			oldSnapshot.SerializeFloat(oldFloat);
			packet.Serialize(floatChanged, 1);
			if (floatChanged)
				packet.Serialize(newFloat, 32);
			else
				newFloat = oldFloat;
			newSnapshot.SerializeFloat(newFloat);
		}
		
		oldSnapshot.SerializeInt(oldInt);
		packet.SerializeInt(deltaInt);
		newInt = oldInt + deltaInt;
		newSnapshot.SerializeInt(newInt);
	}
}
