[ComponentEditorProps(category: "GameScripted/Weapons/BGONE", description: "Guided missile projectile flight simulator and warhead controller")]
class BGONE_GuidedMissileComponentClass : ScriptGameComponentClass
{
}

class BGONE_GuidedMissileComponent : ScriptComponent
{
	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Updating The Target During Flight - Also Handles Proximity Detonation", category: "BGONE")]
	protected ref BGONE_SeekerType_Base m_eSeekerTypeComponent;
	
	[Attribute("", UIWidgets.Object, desc: "Scripts Responsible For Adjusting The Attack Mode Of The Missile", category: "BGONE")]
	protected ref array<ref BGONE_AttackProfile_Base> m_eAttackProfileComponents;
	
	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Physically Moving The Missile", category: "BGONE")]
	protected ref BGONE_MissileEngine_Base m_eMissileEngineComponent;
	
	protected int m_iArmingDistanceIndex = 0;
	protected int m_eAttackProfileComponentIndex = 0;
	protected Projectile m_eOwner;
	protected float m_fFlightTime;
	protected bool m_bGuidanceActive = false;
	protected ref BGONE_TargetData m_eCurrentTargetData;
	protected RplComponent m_RplComponent;
	protected BGONE_GuidedMissileLauncherComponent m_LauncherComp;
	protected vector m_vLastTargetPosition = Vector(0,0,0);
	
	protected float m_fSaclosFixNextUpdateTime = 0;
	protected float m_fSaclosFixUpdateInterval = 50.0; // 50ms (in milliseconds)
	
	protected float m_fSyncPosNextUpdateTime = 0;
	protected float m_fSyncPosUpdateInterval = 50.0;   // 50ms (in milliseconds)
	
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.SIMULATE);
	}
	
	override void EOnInit(IEntity owner)
	{
		m_eOwner = Projectile.Cast(owner);
		if(m_eOwner)
			m_RplComponent = RplComponent.Cast(m_eOwner.FindComponent(RplComponent));
	}
	
	protected void DeleteMissile(IEntity owner)
	{
		if(!owner)
			return;
			
		RplComponent rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		if(rpl)
			rpl.DeleteRplEntity(owner, false);
		else
			delete owner;
	}
	
	void SetAttackAndFireModes(int attackModeIndex, int armingDistanceIndex)
	{
		m_eAttackProfileComponentIndex = attackModeIndex;
		m_iArmingDistanceIndex = armingDistanceIndex;
	}
	
	void onLaunched(BGONE_TargetData targetData, BGONE_GuidedMissileLauncherComponent launcher)
	{
		m_LauncherComp = launcher;
		m_eCurrentTargetData = targetData;
		if(!m_eCurrentTargetData)
			return;
			
		SetAttackAndFireModes(m_eCurrentTargetData.attackProfileIndex, m_eCurrentTargetData.armingDistancesIndex);
		
		if(m_eSeekerTypeComponent)
			m_eSeekerTypeComponent.InitSeeker(m_eOwner, m_eCurrentTargetData);
			
		if(m_eAttackProfileComponents && m_eAttackProfileComponents.IsIndexValid(m_eCurrentTargetData.attackProfileIndex))
		{
			m_eAttackProfileComponents[m_eCurrentTargetData.attackProfileIndex].InitAttackMode(m_eOwner, m_eCurrentTargetData);
		}
		
		m_fFlightTime = 0;
		m_bGuidanceActive = true;
	}
	
	override void EOnSimulate(IEntity owner, float timeSlice)
	{
		if(m_RplComponent && m_RplComponent.Role() == RplRole.Proxy)
			return;
		
		if(!m_bGuidanceActive || !m_eCurrentTargetData)
			return;
		
		m_fFlightTime += timeSlice;

		// Process Seeker
		if(m_eSeekerTypeComponent)
			m_eCurrentTargetData = m_eSeekerTypeComponent.ProcessFrame(m_eCurrentTargetData, m_fFlightTime);
		
		// SACLOS Periodic Update
		if(m_eSeekerTypeComponent && m_eSeekerTypeComponent.Type() == BGONE_SeekerType_SACLOS)
		{
			float currentTime = GetGame().GetWorld().GetWorldTime();
			if(currentTime > m_fSaclosFixNextUpdateTime)
			{
				m_fSaclosFixNextUpdateTime = currentTime + m_fSaclosFixUpdateInterval;
				if(m_LauncherComp)
					m_LauncherComp.SaclosFix();
			}
		}
		
		// Target Position tracking
		if(m_eCurrentTargetData.targetPosition != Vector(0,0,0))
			m_vLastTargetPosition = m_eCurrentTargetData.targetPosition;
		else if(m_vLastTargetPosition != Vector(0,0,0))
			m_eCurrentTargetData.targetPosition = m_vLastTargetPosition;
		
		// Detonation handling
		if(m_eCurrentTargetData.detonated > EBGONE_DetonationState.NONE)
		{
			m_bGuidanceActive = false;
			bool down = (m_eCurrentTargetData.detonated == EBGONE_DetonationState.AIRBURST);
			vector explodePos = m_eOwner.GetOrigin();
			
			if(down)
			{
				vector angles = m_eOwner.GetYawPitchRoll();
				angles[1] = -90;
				m_eOwner.SetYawPitchRoll(angles);
			}
			
			ExplodeWrapper();
			Rpc(RpcDo_Explode, explodePos, down);
			return;
		}
		
		// Process Attack Profile
		if(m_eAttackProfileComponents && m_eAttackProfileComponents.IsIndexValid(m_eCurrentTargetData.attackProfileIndex))
		{
			m_eCurrentTargetData = m_eAttackProfileComponents[m_eCurrentTargetData.attackProfileIndex].ProcessFrame(m_eCurrentTargetData, m_fFlightTime);
		}
		
		// Process Missile Engine
		if(m_eMissileEngineComponent)
		{
			m_eCurrentTargetData.detonated = m_eMissileEngineComponent.ProcessFrame(m_eOwner, m_eCurrentTargetData.targetPosition, m_fFlightTime, timeSlice);
		}
		
		// Periodic network transform synchronization (20 Hz)
		float now = GetGame().GetWorld().GetWorldTime();
		if(now > m_fSyncPosNextUpdateTime)
		{
			m_fSyncPosNextUpdateTime = now + m_fSyncPosUpdateInterval;
			if(m_eOwner && m_eOwner.GetPhysics())
			{
				vector vel = m_eOwner.GetPhysics().GetVelocity();
				vector angles = vector.Zero;
				if(vel.Length() > 0.01)
					angles = vel.VectorToAngles();
				Rpc(RpcDo_UpdatePosVelAng, m_eOwner.GetOrigin(), vel, angles);
			}
		}
	}
	
	[RplRpc(RplChannel.Unreliable, RplRcver.Broadcast)]
	void RpcDo_UpdatePosVelAng(vector position, vector velocity, vector angles)
	{
		if(!m_eOwner)
			return;
			
		m_eOwner.SetYawPitchRoll(angles);
		m_eOwner.SetOrigin(position);
		if(m_eOwner.GetPhysics())
			m_eOwner.GetPhysics().SetVelocity(velocity);
	}

	void UpdateTurretAim(vector aimDir, vector aimPos)
	{
		Rpc(RpcDo_UpdateAimingDir, aimDir, aimPos);
	}
	
	protected bool IsValidVector(vector v)
	{
		if(v[0] != v[0] || v[1] != v[1] || v[2] != v[2])
			return false;
		if(Math.AbsFloat(v[0]) > 100000.0 || Math.AbsFloat(v[1]) > 100000.0 || Math.AbsFloat(v[2]) > 100000.0)
			return false;
		return true;
	}

	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	void RpcDo_UpdateAimingDir(vector aimDir, vector aimPos)
	{
		// Sanitize inputs against NaN / Infinity
		if(!IsValidVector(aimDir) || !IsValidVector(aimPos))
			return;
			
		BGONE_SeekerType_SACLOS seeker = BGONE_SeekerType_SACLOS.Cast(m_eSeekerTypeComponent);
		if(!seeker) 
			return;
		
		seeker.UpdateAimingDirServer(aimDir, aimPos);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcDo_Explode(vector pos, bool down)
	{
		if(!m_eOwner)
			return;
			
		m_eOwner.SetOrigin(pos);
		if(down)
		{
			vector angles = m_eOwner.GetYawPitchRoll();
			angles[1] = -90;
			m_eOwner.SetYawPitchRoll(angles);
		}
		
		ExplodeWrapper();
	}
	
	protected void ExplodeWrapper()
	{
		if(!m_eOwner)
			return;

		BaseTriggerComponent triggerComponent = BaseTriggerComponent.Cast(m_eOwner.FindComponent(BaseTriggerComponent));
		if (!triggerComponent)
			return;
		
		m_eOwner.Update();
		triggerComponent.SetLive();
		
		Instigator instigator;
		if(m_eCurrentTargetData && m_eCurrentTargetData.GetShooterEntity())
		{
			instigator = Instigator.CreateInstigator(m_eCurrentTargetData.GetShooterEntity());
		}
		
		if(instigator)
			triggerComponent.OnUserTriggerOverrideInstigator(m_eOwner, instigator);
		else
			triggerComponent.OnUserTrigger(m_eOwner);
			
		if(m_RplComponent && m_RplComponent.Role() == RplRole.Authority)
		{
			GetGame().GetCallqueue().CallLater(DeleteMissile, 100, false, m_eOwner);
		}
	}
	
	BGONE_AttackProfile_Base GetActiveAttackProfile()
	{
		if(m_eAttackProfileComponents && m_eAttackProfileComponents.IsIndexValid(m_eAttackProfileComponentIndex))
			return m_eAttackProfileComponents[m_eAttackProfileComponentIndex];
		return null;
	}

	override void OnAddedToParent(IEntity child, IEntity parent)
	{
		if(!parent)
			return;
			
		m_LauncherComp = BGONE_GuidedMissileLauncherComponent.Cast(parent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if(!m_LauncherComp)
			return;
			
		if(m_eAttackProfileComponents && !m_eAttackProfileComponents.IsEmpty())
			m_LauncherComp.SetAvailableAttackProfiles(m_eAttackProfileComponents);
			
		if(m_eSeekerTypeComponent)
			m_LauncherComp.SetAvailableArmingDistances(m_eSeekerTypeComponent.GetAvailableArmingDistances());
	}
}
