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
	
	protected BGONE_GuidedMissileComponent m_eLastMissile;
	// Legacy name: holds the last launched missile for ANY seeker (the
	// SACLOS aim relay reads it, other seekers ignore it).
	protected BGONE_GuidedMissileComponent m_eLastMissileSaclos;
	// Single-shot assumption: one pending server-launch slot (reload >> 500
	// ms). A second launch inside the retry window drops with a warning.
	protected ref BGONE_TargetData m_PendingServerLaunch;
	protected RplId m_PendingServerMissile;
	// Missile the outstanding retry timer was scheduled for. The timer
	// consumes the slot only on identity match, so it can never launch a
	// different missile staged afterwards.
	protected RplId m_PendingRetryMissile;
	protected const int PENDING_LAUNCH_RETRY_MS = 500;
	
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
			ReleaseVehicleHandler();
		}
		
		if(m_eventHandler)
		{
			RemoveListeners();
		}
		
		// Cancel any pending server-launch retry so the timer cannot fire
		// into a destroyed/despawned launcher and bind a live missile to it.
		CancelPendingLaunch();
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
			m_eTurret = null;
			m_eTurretController = null;
			ReleaseVehicleHandler();
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
	
	// Holder liveness for the FixedFrame self-heal: true while the bound
	// player still has this weapon (hands or turret compartment).
	protected bool IsStillHeldByPlayer()
	{
		if(!m_eCurrentPlayer)
			return false;
		
		if(m_eTurret)
		{
			if(!m_eTurretController)
				return false;
			
			BaseCompartmentSlot slot = m_eTurretController.GetCompartmentSlot();
			return slot && slot.GetOccupant() == m_eCurrentPlayer;
		}
		
		return m_eOwner.GetRootParent() == m_eCurrentPlayer;
	}
	
	// Shared teardown fragments (single owners; called from EOnDeactivate,
	// the FixedFrame unbind path, and compartment/holder transitions).
	protected void ReleaseVehicleHandler()
	{
		if(!m_vehicleEventHandler)
			return;
		
		m_vehicleEventHandler.RemoveScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered);
		m_vehicleEventHandler.RemoveScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft);
		m_vehicleEventHandler = null;
	}
	
	protected void CancelPendingLaunch()
	{
		m_PendingServerLaunch = null;
		if(GetGame())
			GetGame().GetCallqueue().Remove(TryPendingServerLaunch);
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
			CancelPendingLaunch();
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
		if(m_RplComponent && m_RplComponent.IsRemoteProxy())
			return;
		
		// Self-healing occupant binding: EOnActivate fires at spawn/stream,
		// not on equip, so a table-picked launcher would otherwise never
		// bind its player (no listeners, no lock, no fire-handshake).
		// Re-resolve until bound; release when the holder is gone/changed.
		if(!m_eCurrentPlayer)
		{
			UpdateOccupantAndOwnership();
		}
		else if(!IsStillHeldByPlayer())
		{
			RemoveListeners();
			ReleaseVehicleHandler();
			CancelPendingLaunch();
			m_eCurrentPlayer = null;
			m_eTurret = null;
			m_eTurretController = null;
			return;
		}
		
		if(!m_eCurrentPlayer)
			return;
		
		if(!m_eventHandler || !IsAdsActive())
		{
			if(m_bLocking && m_eLockTypeComponent)
				m_eLockTypeComponent.StopLock();
			return;
		}
		
		if(m_eLockTypeComponent)
			m_eLockTypeComponent.UpdateLock(timeSlice);
	}
	
	// ADS check for the frame loop (UpdateLock runs ADS-gated; lock stops
	// on ADS loss).
	protected bool IsAdsActive()
	{
		if(m_eCurrentPlayer && m_eCurrentPlayer.GetWeaponManager())
		{
			BaseWeaponComponent weaponComp = m_eCurrentPlayer.GetWeaponManager().GetCurrentWeapon();
			if(weaponComp && weaponComp.IsSightADSActive())
				return true;
		}
		
		if(m_eTurretController && m_eTurretController.IsWeaponADS())
			return true;
		
		return false;
	}
	
	protected void SetLockingState(float value, EActionTrigger reason)
	{
		if(m_RplComponent && m_RplComponent.IsRemoteProxy())
			return;
		
		// Origin semantics: DOWN always arms the lock; ADS-only operation
		// is enforced by EOnFixedFrame (UpdateLock ADS-gated, StopLock on
		// ADS loss cancels a press that landed off-ADS). No gate at the
		// input edge by design.
		m_bLocking = (reason == EActionTrigger.DOWN);
		if(m_eLockTypeComponent)
		{
			if(m_bLocking)
				m_eLockTypeComponent.StartLock();
			else
				m_eLockTypeComponent.StopLock();
		}
	}
	
	// Lock tones are per-locker UI feedback and stay local by design: the
	// lock runs where the occupant sits (FixedFrame is proxy-gated), so the
	// locker always hears their own tone without any relay. A game-wide
	// broadcast would blip uninvolved players' launchers, so no relay exists.
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
		
		// Dedicated-server ghost guard: if our own OnProjectileShot fires on
		// a machine that is authority but NOT owner (server observing another
		// machine's shot), there is no lock state here — launching with empty
		// data would arm the missile's single-shot guard and discard the
		// owner's real-data RPC when it arrives. Wait for the RPC instead.
		// (Host shooters are authority+owner, so they still launch below.)
		bool isAuthority = m_RplComponent && m_RplComponent.Role() == RplRole.Authority;
		bool isOwner = m_RplComponent && m_RplComponent.IsOwner();
		if(isAuthority && !isOwner)
			return;
		
		m_eLastMissile = BGONE_GuidedMissileComponent.Cast(entity.FindComponent(BGONE_GuidedMissileComponent));
		if(!m_eLastMissile)
			return;
		
		BGONE_TargetData targetData;
		if(m_eLockTypeComponent)
			targetData = m_eLockTypeComponent.GetCurrentTargetData();
			
		if(!targetData)
		{
			targetData = new BGONE_TargetData();
		}
		else
		{
			// Snapshot: PLOS keeps smoothing the live object until ADS
			// drop, and VIS can still mutate it in the ~10 ms before its
			// deferred StopLock fires. (SACLOS hands a fresh object, so the
			// Clone is a no-op there — kept uniform on purpose.) The missile
			// must fly frozen fire-time values, not a live ref.
			targetData = BGONE_TargetData.Cast(targetData.Clone());
			targetData.InvalidateEntities();
		}
		
		vector launchDir = entity.GetYawPitchRoll().AnglesToVector();
		targetData.launchPos = entity.GetOrigin();
		targetData.launchDir = launchDir;
		targetData.attackProfileIndex = m_iCurrentAttackModeIndex;
		targetData.armingDistancesIndex = m_iCurrentArmingDistanceIndex;
		
		// Zero aimpoint init: SACLOS pre-first-relay and unlocked VIS (which
		// coasts to its no-target timer, never receiving relay data) must
		// coast straight ahead, never steer at the world origin. PLOS
		// overwrites this on its first attack-profile tick (angular
		// guidance needs no position seed).
		if(targetData.targetPosition == Vector(0,0,0))
			targetData.targetPosition = targetData.launchPos + launchDir * 10.0;
		
		// Launcher owns the occupant: stamp shooter provenance for kill
		// credit on the authority copy. Locks only set this for SACLOS, so
		// VIS/PLOS would otherwise detonate unattributed.
		if(!targetData.shooterRplId.IsValid() && m_eCurrentPlayer && m_eCurrentPlayer.GetRplComponent())
			targetData.shooterRplId = m_eCurrentPlayer.GetRplComponent().Id();
		
		if(m_eTurret && m_eTurret.GetRplComponent())
			targetData.turretRplId = m_eTurret.GetRplComponent().Id();
		
		// Local launch: initialize the missile immediately (host-authority
		// guides from here; owner-client copies never simulate, so this is
		// prediction state the server RplProp snapshot will overwrite).
		m_eLastMissile.onLaunched(targetData, this);
		m_eLastMissileSaclos = m_eLastMissile;
		
		// Authority already launched above: skip the server RPC to avoid a
		// listen-server loopback double-launch. Owner-clients forward the
		// launch so the server holds the authoritative copy (replicated via
		// the missile's m_eCurrentTargetData RplProp).
		if(m_RplComponent && !isAuthority)
		{
			RplId missileRplId;
			RplComponent missileRpl = RplComponent.Cast(entity.FindComponent(RplComponent));
			if(!missileRpl)
			{
				Print("BGONE - OnLaunch: missile has no RplComponent, server will not guide it", LogLevel.WARNING);
				m_eLastMissile = null;
				return;
			}
			missileRplId = missileRpl.Id();
			RplId shooterRplId = targetData.shooterRplId;
			// Two calls: Rpc() has a lower arity cap than RplRpc handlers,
			// so the 11-field handshake will not fit in one call. Both are
			// Reliable, hence ordered: move half always lands first.
			Rpc(RpcAsk_ServerLaunchMove, missileRplId, targetData.launchPos, targetData.launchDir, targetData.targetPosition, targetData.yawChange, targetData.pitchChange);
			Rpc(RpcAsk_ServerLaunchData, missileRplId, targetData.attackProfileIndex, targetData.armingDistancesIndex, shooterRplId, targetData.turretRplId, targetData.targetRplId);
		}
		else if(!m_RplComponent)
		{
			Print("BGONE - OnLaunch: launcher has no RplComponent, launch stays local-only", LogLevel.WARNING);
		}
		
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
	
	// Server launch handshake, move half (6 params: Rpc() arity cap).
	// Stashes a partial TargetData; the data half completes it. Reliable +
	// ordered, so this always lands before its RpcAsk_ServerLaunchData.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_ServerLaunchMove(RplId missileRplId, vector launchPos, vector launchDir, vector targetPosition, float yawChange, float pitchChange)
	{
		if(!missileRplId.IsValid() || !IsValidVector(launchPos) || !IsValidVector(launchDir) || !IsValidVector(targetPosition) || yawChange != yawChange || pitchChange != pitchChange)
		{
			Print("BGONE - RpcAsk_ServerLaunchMove: rejected invalid launch data", LogLevel.WARNING);
			return;
		}
		
		if(m_PendingServerLaunch)
		{
			Print("BGONE - RpcAsk_ServerLaunchMove: pending slot busy, dropping launch", LogLevel.WARNING);
			return;
		}
		
		RplId invalidId;
		m_PendingServerMissile = missileRplId;
		m_PendingServerLaunch = BGONE_TargetData.FromLaunchParams(launchPos, launchDir, targetPosition, yawChange, pitchChange, 0, 0, invalidId, invalidId, invalidId);
	}
	
	// Server launch handshake, data half (6 params). Completes the pending
	// partial and launches; falls back to stash + one retry on miss.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_ServerLaunchData(RplId missileRplId, int attackProfileIndex, int armingDistancesIndex, RplId shooterRplId, RplId turretRplId, RplId targetRplId)
	{
		if(!missileRplId.IsValid() || attackProfileIndex < 0 || armingDistancesIndex < 0)
		{
			Print("BGONE - RpcAsk_ServerLaunchData: rejected invalid launch data", LogLevel.WARNING);
			// Only clear our own partial: a sibling launch's retry stash
			// must survive another missile's invalid data.
			if(m_PendingServerLaunch && m_PendingServerMissile == missileRplId)
				m_PendingServerLaunch = null;
			return;
		}
		
		if(!m_PendingServerLaunch || m_PendingServerMissile != missileRplId)
		{
			Print("BGONE - RpcAsk_ServerLaunchData: no matching move half, dropping launch", LogLevel.WARNING);
			return;
		}
		
		BGONE_TargetData targetData = m_PendingServerLaunch;
		m_PendingServerLaunch = null;
		targetData.attackProfileIndex = attackProfileIndex;
		targetData.armingDistancesIndex = armingDistancesIndex;
		targetData.shooterRplId = shooterRplId;
		targetData.turretRplId = turretRplId;
		targetData.targetRplId = targetRplId;
		targetData.InvalidateEntities();
		
		if(!TryServerLaunch(missileRplId, targetData))
		{
			// Replication race: the just-spawned missile is not known to the
			// server yet. Stash and retry once shortly; drop with a warning
			// if the slot is busy or the retry also misses.
			if(m_PendingServerLaunch)
			{
				Print("BGONE - RpcAsk_ServerLaunchData: pending slot busy, dropping launch", LogLevel.WARNING);
				return;
			}
			m_PendingServerMissile = missileRplId;
			m_PendingServerLaunch = targetData;
			m_PendingRetryMissile = missileRplId;
			GetGame().GetCallqueue().CallLater(TryPendingServerLaunch, PENDING_LAUNCH_RETRY_MS, false);
		}
	}
	
	protected bool TryServerLaunch(RplId missileRplId, BGONE_TargetData targetData)
	{
		RplComponent missileRpl = RplComponent.Cast(Replication.FindItem(missileRplId));
		if(!missileRpl || !missileRpl.GetEntity())
			return false;
		
		BGONE_GuidedMissileComponent missile = BGONE_GuidedMissileComponent.Cast(missileRpl.GetEntity().FindComponent(BGONE_GuidedMissileComponent));
		if(!missile)
		{
			// Known entity, not a guided missile (stale/spoofed id): consume
			// the slot without retrying, warn only.
			Print("BGONE - TryServerLaunch: entity is not a guided missile", LogLevel.WARNING);
			return true;
		}
		
		missile.onLaunched(targetData, this);
		m_eLastMissileSaclos = missile;
		m_eLastMissile = null;
		return true;
	}
	
	protected void TryPendingServerLaunch()
	{
		// Empty slot (consumed or cancelled): nothing to do. Identity check:
		// the slot may have been repurposed after this timer was scheduled;
		// never launch anything but the missile this retry was scheduled for.
		// Liveness itself is decided by the FindItem null-check in
		// TryServerLaunch, not by re-validating the id here.
		if(!m_PendingServerLaunch || m_PendingServerMissile != m_PendingRetryMissile)
			return;
		
		RplId missileRplId = m_PendingServerMissile;
		BGONE_TargetData targetData = m_PendingServerLaunch;
		m_PendingServerLaunch = null;
		if(!TryServerLaunch(missileRplId, targetData))
			Print("BGONE - TryPendingServerLaunch: missile still unknown, server will not guide it", LogLevel.WARNING);
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
			// Activated once with the listeners (was per-frame in FixedFrame).
			m_InputManager.ActivateContext("CharacterWeaponGuidedLauncher");
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
			// Full lock teardown (widget, scan state, progress): dismount
			// must not leave a stale lock behind for the next occupant.
			// (Double LockLost with the line above is idempotent by design.)
			m_eLockTypeComponent.StopLock();
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
			// No DeactivateContext: InputManager exposes only
			// ActivateContext(string, int) in 1.8, and the old code never
			// deactivated either (activated per-frame while ADS).
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
		// The list can shrink (missile chamber syncs its own profiles over
		// the launcher defaults): repair a stale selection instead of
		// reading it out of bounds.
		if(m_eSupportedAttackProfiles && !m_eSupportedAttackProfiles.IsIndexValid(m_iCurrentAttackModeIndex))
			m_iCurrentAttackModeIndex = 0;
	}
	
	void SetAvailableArmingDistances(array<int> armingDistances)
	{
		m_aAvailableArmingDistances = armingDistances;
		// Same staleness as above: without the repair, a selection made
		// against a longer list fails IsIndexValid in the seeker and arms
		// at 0 m.
		if(m_aAvailableArmingDistances && !m_aAvailableArmingDistances.IsIndexValid(m_iCurrentArmingDistanceIndex))
			m_iCurrentArmingDistanceIndex = 0;
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
