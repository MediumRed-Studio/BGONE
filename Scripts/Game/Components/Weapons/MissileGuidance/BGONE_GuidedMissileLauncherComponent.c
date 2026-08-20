[ComponentEditorProps(category: "GameScripted/Weapons/BGONE", description: "Guided missile launcher component handling lock lifecycle and firing")]
class BGONE_GuidedMissileLauncherComponentClass : ScriptGameComponentClass
{
}

class BGONE_GuidedMissileLauncherComponent : ScriptGameComponent
{
	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Gathering Initial Target Data To Be Sent To The Missile", category: "BGONE")]
	protected ref BGONE_LockType_Base m_eLockTypeComponent;
	
	protected IEntity m_eOwner;
	protected SCR_ChimeraCharacter m_eCurrentPlayer;
	protected EventHandlerManagerComponent m_eventHandler;
	protected EventHandlerManagerComponent m_vehicleEventHandler;
	protected InputManager m_InputManager;
	[Attribute("", UIWidgets.Object, desc: "Supported attack profiles for this launcher", category: "BGONE")]
	protected ref array<ref BGONE_AttackProfile_Base> m_eSupportedAttackProfiles;
	protected int m_iCurrentAttackModeIndex = 0;
	
	[Attribute("20 50 100", UIWidgets.EditBox, desc: "Available arming distances in meters", category: "BGONE")]
	protected ref array<int> m_aAvailableArmingDistances;
	protected int m_iCurrentArmingDistanceIndex = 0;
	protected RplComponent m_RplComponent;
	protected TurretControllerComponent m_eTurretController;
	protected Turret m_eTurret;
	protected bool m_bLocking;
	protected bool m_bListenersRegistered = false;
	
	protected ref BGONE_TargetData m_eLastTargetData;
	protected BGONE_GuidedMissileComponent m_eLastMissile;
	protected BGONE_GuidedMissileComponent m_eLastMissileSaclos;
	
	// Methods for handling ownership change and action context (de)activation.
	protected override void EOnActivate(IEntity owner)
	{
		super.EOnActivate(owner);
		UpdateOccupantAndOwnership();
	}
	
	protected override void EOnDeactivate(IEntity owner)
	{
		super.EOnDeactivate(owner);
		
		if(m_eLockTypeComponent)
		{
			m_eLockTypeComponent.StopLock();
			m_eLockTypeComponent.TerminateLockOnAudio();
		}
		
		if(m_vehicleEventHandler)
		{
			m_vehicleEventHandler.RemoveScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered);
			m_vehicleEventHandler.RemoveScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft);
			m_vehicleEventHandler = null;
		}
		
		if(m_eventHandler)
		{
			RemoveListeners();
		}
	}
	
	protected void UpdateOccupantAndOwnership()
	{
		m_eTurret = Turret.Cast(m_eOwner.GetParent());
		if(m_eTurret)
		{
			m_eTurretController = TurretControllerComponent.Cast(m_eTurret.FindComponent(TurretControllerComponent));
			if(m_eTurretController)
			{
				BaseCompartmentSlot slot = m_eTurretController.GetCompartmentSlot();
				if(slot)
					m_eCurrentPlayer = SCR_ChimeraCharacter.Cast(slot.GetOccupant());
			}
			
			// Listen to vehicle compartment entry/exit
			if(!m_vehicleEventHandler)
			{
				IEntity rootVehicle = m_eTurret.GetRootParent();
				if(rootVehicle)
				{
					m_vehicleEventHandler = EventHandlerManagerComponent.Cast(rootVehicle.FindComponent(EventHandlerManagerComponent));
					if(m_vehicleEventHandler)
					{
						m_vehicleEventHandler.RegisterScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered);
						m_vehicleEventHandler.RegisterScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft);
					}
				}
			}
		}
		else
		{
			m_eCurrentPlayer = SCR_ChimeraCharacter.Cast(m_eOwner.GetRootParent());
		}
		
		if(!m_eCurrentPlayer)
			return;
		
		RegisterListeners();
		
		RplComponent playerRpl = m_eCurrentPlayer.GetRplComponent();
		if(playerRpl && m_RplComponent)
		{
			RplIdentity ownerIdentity = Replication.FindOwner(playerRpl.Id());
			if(ownerIdentity.IsValid())
				Rpc(RpcAsk_GiveOwnerShip, ownerIdentity);
		}
	}
	
	protected void OnCompartmentEntered(IEntity vehicle, BaseCompartmentManagerComponent manager, int mgrID, int slotID, IEntity occupant)
	{
		if(!m_eTurretController)
			return;
			
		BaseCompartmentSlot mySlot = m_eTurretController.GetCompartmentSlot();
		if(mySlot && mySlot.GetOccupant() == occupant)
		{
			m_eCurrentPlayer = SCR_ChimeraCharacter.Cast(occupant);
			if(m_eCurrentPlayer)
			{
				RplComponent playerRpl = m_eCurrentPlayer.GetRplComponent();
				if(playerRpl && m_RplComponent)
				{
					RplIdentity ownerIdentity = Replication.FindOwner(playerRpl.Id());
					if(ownerIdentity.IsValid())
						Rpc(RpcAsk_GiveOwnerShip, ownerIdentity);
				}
			}
		}
	}
	
	protected void OnCompartmentLeft(IEntity vehicle, BaseCompartmentManagerComponent manager, int mgrID, int slotID, IEntity occupant)
	{
		if(m_eCurrentPlayer && m_eCurrentPlayer == occupant)
		{
			RemoveListeners();
			m_eCurrentPlayer = null;
		}
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_GiveOwnerShip(RplIdentity identity)
	{
		if(m_RplComponent && identity.IsValid())
		{
			m_RplComponent.Give(identity);
		}
		RegisterListeners();
		Rpc(RpcDo_GiveOwnerShip, identity);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_GiveOwnerShip(RplIdentity identity)
	{
		RegisterListeners();
	}
	
	protected override void EOnInit(IEntity owner)
	{
		m_eOwner = owner;
		m_RplComponent = RplComponent.Cast(m_eOwner.FindComponent(RplComponent));
		
		if(m_eLockTypeComponent)
		{
			m_eLockTypeComponent.InitLockType(owner);
		}
		else
		{
			Print("BGONE - Guided missile launcher initialized without a LockType component!", LogLevel.WARNING);
		}
		
		if(!m_aAvailableArmingDistances || m_aAvailableArmingDistances.IsEmpty())
		{
			m_aAvailableArmingDistances = {20, 50, 100};
		}
		
		if(!m_eSupportedAttackProfiles || m_eSupportedAttackProfiles.IsEmpty())
		{
			m_eSupportedAttackProfiles = new array<ref BGONE_AttackProfile_Base>();
			if(BGONE_LockType_VIS.Cast(m_eLockTypeComponent))
			{
				m_eSupportedAttackProfiles.Insert(new BGONE_AttackProfile_DIR());
				m_eSupportedAttackProfiles.Insert(new BGONE_AttackProfile_TOP());
			}
			else if(BGONE_LockType_PLOS.Cast(m_eLockTypeComponent))
			{
				m_eSupportedAttackProfiles.Insert(new BGONE_AttackProfile_PLOS());
				m_eSupportedAttackProfiles.Insert(new BGONE_AttackProfile_PLOS_FLYOVER());
			}
			else if(BGONE_LockType_SACLOS.Cast(m_eLockTypeComponent))
			{
				m_eSupportedAttackProfiles.Insert(new BGONE_AttackProfile_SACLOS());
			}
		}
		
		m_InputManager = GetGame().GetInputManager();
	}
	
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		if((m_RplComponent && m_RplComponent.IsRemoteProxy()) || !m_eCurrentPlayer)
			return;
		
		bool turretAds = false;
		bool weaponAds = false;
		
		if(m_eCurrentPlayer.GetWeaponManager())
		{
			BaseWeaponComponent weaponComp = m_eCurrentPlayer.GetWeaponManager().GetCurrentWeapon();
			if(weaponComp)
				weaponAds = weaponComp.IsSightADSActive();
		}
		
		if(m_eTurretController)
			turretAds = m_eTurretController.IsWeaponADS();
		 	
		if(!m_eventHandler || (!weaponAds && !turretAds))
		{
			if(m_eLockTypeComponent)
				m_eLockTypeComponent.StopLock();
			m_bLocking = false;
			return;
		}
		
		if(m_InputManager)
			m_InputManager.ActivateContext("CharacterWeaponGuidedLauncher");
		
		if(m_eLockTypeComponent)
		{
			if(!m_eLockTypeComponent.IsLocking())
				m_eLockTypeComponent.StartLock();
				
			m_eLockTypeComponent.UpdateLock(timeSlice);
		}
	}
	
	protected void SetLockingState(float value, EActionTrigger reason)
	{
		if(!m_RplComponent || !m_RplComponent.IsOwner())
			return;
		
		m_bLocking = (reason == EActionTrigger.DOWN);
		if(m_eLockTypeComponent)
		{
			if(m_bLocking)
				m_eLockTypeComponent.StartLock();
			else
				m_eLockTypeComponent.StopLock();
		}
	}
	
	protected void LockStartAcquire(BGONE_LockingData_BASE lockingData)
	{
		if(m_eLockTypeComponent)
			m_eLockTypeComponent.PlayLockOnAudio(lockingData.lockingProgress);
	}
	
	protected void LockAcquired(BGONE_LockingData_BASE lockingData)
	{
		if(m_eLockTypeComponent)
			m_eLockTypeComponent.PlayLockOnAudio(lockingData.lockingProgress);
	}
	
	protected void LockLost()
	{
		if(m_eLockTypeComponent)
			m_eLockTypeComponent.TerminateLockOnAudio();
	}
	
	protected void LockStartAquire(BGONE_LockingData_BASE lockingData)
	{
		LockStartAcquire(lockingData);
	}
	
	protected void OnLaunch(int playerID, BaseWeaponComponent weapon, IEntity entity)
	{	
		if(m_RplComponent && m_RplComponent.IsRemoteProxy())
			return;
		
		m_eLastMissile = BGONE_GuidedMissileComponent.Cast(entity.FindComponent(BGONE_GuidedMissileComponent));
		if(!m_eLastMissile)
			return;
		
		BGONE_TargetData targetData;
		if(m_eLastTargetData)
		{
			targetData = m_eLastTargetData;
		}
		else
		{
			if(m_eLockTypeComponent)
				targetData = m_eLockTypeComponent.GetCurrentTargetData();
				
			if(!targetData)
				targetData = new BGONE_TargetData();
			
			targetData.launchPos = entity.GetOrigin();
			targetData.launchDir = entity.GetYawPitchRoll().AnglesToVector();
			targetData.attackProfileIndex = m_iCurrentAttackModeIndex;
			targetData.armingDistancesIndex = m_iCurrentArmingDistanceIndex;
			
			if(m_eTurret && m_eTurret.GetRplComponent())
				targetData.turretRplId = m_eTurret.GetRplComponent().Id();
		}
		
		m_eLastMissile.onLaunched(targetData, this);
		Rpc(RpcAsk_SendTargetData, targetData);
		
		m_eLastTargetData = null;
		m_eLastMissile = null;
	}
	
	void SaclosFix()
	{
		Rpc(RpcDo_SaclosFix);
	}
	
	protected bool IsValidVector(vector v)
	{
		if(v[0] != v[0] || v[1] != v[1] || v[2] != v[2])
			return false;
		if(Math.AbsFloat(v[0]) > 100000.0 || Math.AbsFloat(v[1]) > 100000.0 || Math.AbsFloat(v[2]) > 100000.0)
			return false;
		return true;
	}

	[RplRpc(RplChannel.Unreliable, RplRcver.Owner)]
	void RpcDo_SaclosFix()
	{
		vector aimDir = Vector(0,0,1);
		vector aimPos = Vector(0,0,0);
		
		if(m_eLockTypeComponent)
		{
			m_eLockTypeComponent.GetAimDirAndPosOfLauncher(m_eOwner, aimDir, aimPos);
		}
		
		Rpc(RpcAsk_SaclosFix, aimDir, aimPos);
	}
	
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	void RpcAsk_SaclosFix(vector aimDir, vector aimPos)
	{
		if(!IsValidVector(aimDir) || !IsValidVector(aimPos))
			return;
			
		if(m_eLastMissileSaclos)
		{
			m_eLastMissileSaclos.UpdateTurretAim(aimDir, aimPos);
		}
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_SendTargetData(BGONE_TargetData targetData)
	{
		if(!m_eLastMissile)
		{
			m_eLastTargetData = targetData;
			return;
		}
		
		m_eLastMissileSaclos = m_eLastMissile;
		m_eLastMissile.onLaunched(targetData, this);
		
		m_eLastTargetData = null;
		m_eLastMissile = null;
	}
	
	protected void RegisterListeners()
	{
		if(m_bListenersRegistered || !m_eCurrentPlayer)
			return;
			
		if(!m_eventHandler)
		{
			m_eventHandler = EventHandlerManagerComponent.Cast(m_eCurrentPlayer.FindComponent(EventHandlerManagerComponent));
			if(!m_eventHandler)
			{
				Print("BGONE GuidedMissileLauncher - EventHandler Missing On Equipped Player", LogLevel.ERROR);
				return;
			}
		}
		
		if(m_eLockTypeComponent)
		{
			m_eLockTypeComponent.GetOnLockStartAcquire().Insert(LockStartAcquire);
			m_eLockTypeComponent.GetOnLockAcquired().Insert(LockAcquired);
			m_eLockTypeComponent.GetOnLockLost().Insert(LockLost);
		}
		
		m_eventHandler.RegisterScriptHandler("OnProjectileShot", this, OnLaunch);

		if(m_InputManager)
		{
			m_InputManager.AddActionListener("BGONELock", EActionTrigger.DOWN, SetLockingState);
			m_InputManager.AddActionListener("BGONELock", EActionTrigger.UP, SetLockingState);
		}
		
		m_bListenersRegistered = true;
	}
	
	protected void RemoveListeners()
	{
		if(!m_bListenersRegistered)
			return;
			
		if(m_eLockTypeComponent)
		{
			m_eLockTypeComponent.GetOnLockStartAcquire().Remove(LockStartAcquire);
			m_eLockTypeComponent.GetOnLockAcquired().Remove(LockAcquired);
			m_eLockTypeComponent.GetOnLockLost().Remove(LockLost);
			LockLost();
		}
		
		if(m_eventHandler)
		{
			m_eventHandler.RemoveScriptHandler("OnProjectileShot", this, OnLaunch);
			m_eventHandler = null;
		}
		
		if(m_InputManager)
		{
			m_InputManager.RemoveActionListener("BGONELock", EActionTrigger.DOWN, SetLockingState);
			m_InputManager.RemoveActionListener("BGONELock", EActionTrigger.UP, SetLockingState);
		}
		
		m_bListenersRegistered = false;
	}
	
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FIXEDFRAME);
	}
	
	void SetAvailableAttackProfiles(array<ref BGONE_AttackProfile_Base> attackProfiles)
	{
		m_eSupportedAttackProfiles = attackProfiles;
	}
	
	void SetAvailableArmingDistances(array<int> armingDistances)
	{
		m_aAvailableArmingDistances = armingDistances;
	}
	
	int GetArmingDistancesCount() 
	{
		if(!m_aAvailableArmingDistances)
			return 0;
		
		return m_aAvailableArmingDistances.Count();
	}
	
	void CycleArmingDistance()
	{
		int count = GetArmingDistancesCount();
		if(count <= 0)
			return;
			
		if(m_iCurrentArmingDistanceIndex < count - 1)
			m_iCurrentArmingDistanceIndex++;
		else
			m_iCurrentArmingDistanceIndex = 0;
	}
	
	int GetCurrentArmingDistance()
	{
		if(m_aAvailableArmingDistances && m_aAvailableArmingDistances.IsIndexValid(m_iCurrentArmingDistanceIndex))
			return m_aAvailableArmingDistances[m_iCurrentArmingDistanceIndex];
		return 0;
	}
	
	int GetAttackModesCount() 
	{
		if(!m_eSupportedAttackProfiles)
			return 0;
		return m_eSupportedAttackProfiles.Count();
	}
	
	void CycleAttackMode()
	{
		int count = GetAttackModesCount();
		if(count <= 0)
			return;
			
		if(m_iCurrentAttackModeIndex < count - 1)
			m_iCurrentAttackModeIndex++;
		else
			m_iCurrentAttackModeIndex = 0;
	}
	
	BGONE_AttackProfile_Base GetCurrentAttackMode()
	{
		if(m_eSupportedAttackProfiles && m_eSupportedAttackProfiles.IsIndexValid(m_iCurrentAttackModeIndex))
			return m_eSupportedAttackProfiles[m_iCurrentAttackModeIndex];
		return null;
	}
}
