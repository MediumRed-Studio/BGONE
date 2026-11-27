void ScriptInvoker_OwnerChangedMethod(RplIdentity ownerId);
typedef func ScriptInvoker_OwnerChangedMethod;
typedef ScriptInvokerBase<ScriptInvoker_OwnerChangedMethod> ScriptInvoker_OwnerChanged;

[ComponentEditorProps(category: "GameScripted/Weapons/BGONE", description: "")]
class BGONE_GuidedMissileLauncherComponentClass : ScriptGameComponentClass
{
};

class BGONE_GuidedMissileLauncherComponent : ScriptGameComponent
{
	[Attribute("", UIWidgets.Object, desc: "Script Responsible For Gathering Initial Target Data To Be Sent To The Missile", category: "BGONE")]
	protected ref BGONE_LockType_Base m_eLockTypeComponent;

	protected IEntity m_eOwner;
	protected BaseWeaponComponent m_WeaponComponent;
	protected SCR_ChimeraCharacter m_eCurrentPlayer;
	protected EventHandlerManagerComponent m_eventHandler;
	protected EventHandlerManagerComponent m_TurretEventHandler;
	protected InputManager m_InputManager;
	protected ref array<ref BGONE_AttackProfile_Base> m_eSupportedAttackProfiles;
	protected int m_iCurrentAttackModeIndex = 0;
	protected ref array<int> m_aAvailableArmingDistances;
	protected int m_iCurrentArmingDistanceIndex = 0;
	protected RplComponent m_RplComponent;
	protected Turret m_eTurret;
	protected TurretControllerComponent m_eTurretController;
	protected bool locking;

	static const float ADS_DROP_GRACE_SEC = 0.15;

	protected bool m_bListenersRegistered;
	protected bool m_bInputListenersRegistered;
	protected bool m_bTurretHandlersRegistered;
	protected ResourceName m_sLastInspectedAmmoTemplate;
	protected BaseMagazineComponent m_LastInspectedMagazine;
	protected float m_fAdsDropTimer;

	protected ref BGONE_TargetData m_eLastTargetData;
	protected BGONE_GuidedMissileComponent m_eLastMissile;
	protected ref array<BGONE_GuidedMissileComponent> m_aActiveSaclosMissiles = {};
	protected float m_fNextSaclosPollTime = 0.0;
	static const float SACLOS_POLL_INTERVAL_MS = 50.0;
	protected int m_iServerLaunchCount;
	protected int m_iLastServerAttackProfileIndex = -1;

	protected ref array<ref Shape> m_aDbgCollisionShapes;

	//------------------------------------------------------------------------------------------------
	protected override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	protected override void EOnInit(IEntity owner)
	{
		if (!owner)
			return;

		m_eOwner = owner;
		m_WeaponComponent = BaseWeaponComponent.Cast(m_eOwner.FindComponent(BaseWeaponComponent));
		ResolveRplComponent();
		m_InputManager = GetGame().GetInputManager();

		RefreshTurretBinding();
		RefreshMissileCapabilities();

		if (!m_eLockTypeComponent)
		{
			Print("BGONE - Guided missile launcher initialized without a LockType component!");
			return;
		}

		m_eLockTypeComponent.InitLockType(owner);
		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	protected override void EOnActivate(IEntity owner)
	{
		super.EOnActivate(owner);
		if (!owner)
			return;

		if (!m_eOwner)
			m_eOwner = owner;

		ResolveRplComponent();
		RefreshTurretBinding();
		RefreshMissileCapabilities();
		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	protected override void EOnDeactivate(IEntity owner)
	{
		super.EOnDeactivate(owner);
		UnbindOccupant();
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnAddedToParent(IEntity child, IEntity parent)
	{
		super.OnAddedToParent(child, parent);
		if (!m_eOwner)
			m_eOwner = child;

		ResolveRplComponent();
		RefreshTurretBinding();
		RefreshMissileCapabilities();
		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnRemovedFromParent(IEntity child, IEntity parent)
	{
		super.OnRemovedFromParent(child, parent);

		if (parent && parent == m_eTurret)
			UnhookTurretEvents();

		if (m_eCurrentPlayer)
		{
			bool removedFromHolder = false;
			if (!parent || parent == m_eTurret || parent == m_eCurrentPlayer)
			{
				removedFromHolder = true;
			}
			else
			{
				IEntity ancestor = parent.GetParent();
				while (ancestor)
				{
					if (ancestor == m_eCurrentPlayer)
					{
						removedFromHolder = true;
						break;
					}
					ancestor = ancestor.GetParent();
				}
			}

			if (removedFromHolder)
				UnbindOccupant();
		}

		ResolveRplComponent();
	}

	//------------------------------------------------------------------------------------------------
	protected override void OnDelete(IEntity owner)
	{
		RemoveListeners();
		UnhookTurretEvents();
		if (m_eLockTypeComponent)
			m_eLockTypeComponent.OnCleanup();

		m_eCurrentPlayer = null;
		m_eLastMissile = null;
		if (m_aActiveSaclosMissiles)
			m_aActiveSaclosMissiles.Clear();

		m_eLastTargetData = null;
		m_aDbgCollisionShapes = null;
		m_LastInspectedMagazine = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void ResolveRplComponent()
	{
		if (!m_eOwner)
			return;

		m_RplComponent = RplComponent.Cast(m_eOwner.FindComponent(RplComponent));
		if (!m_RplComponent && m_eOwner.GetParent())
			m_RplComponent = RplComponent.Cast(m_eOwner.GetParent().FindComponent(RplComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocallyOwned()
	{
		if (!m_RplComponent)
			return !Replication.IsRunning();

		return m_RplComponent.IsOwner();
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalPlayerControlled()
	{
		if (!m_eCurrentPlayer)
			return false;

		return SCR_PlayerController.GetLocalControlledEntity() == m_eCurrentPlayer;
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshTurretBinding()
	{
		if (!m_eOwner)
			return;

		Turret parentTurret = Turret.Cast(m_eOwner.GetParent());
		if (parentTurret == m_eTurret)
		{
			if (m_eTurret && !m_eTurretController)
				m_eTurretController = TurretControllerComponent.Cast(m_eTurret.FindComponent(TurretControllerComponent));

			if (m_eTurret && !m_bTurretHandlersRegistered)
				HookTurretEvents(m_eTurret);

			return;
		}

		UnhookTurretEvents();

		if (!parentTurret)
			return;

		m_eTurret = parentTurret;
		m_eTurretController = TurretControllerComponent.Cast(m_eTurret.FindComponent(TurretControllerComponent));
		HookTurretEvents(m_eTurret);
	}

	//------------------------------------------------------------------------------------------------
	protected void HookTurretEvents(Turret turret)
	{
		if (!turret || m_bTurretHandlersRegistered)
			return;

		m_TurretEventHandler = EventHandlerManagerComponent.Cast(turret.FindComponent(EventHandlerManagerComponent));
		if (!m_TurretEventHandler)
			return;

		m_TurretEventHandler.RegisterScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered);
		m_TurretEventHandler.RegisterScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft);
		m_bTurretHandlersRegistered = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void UnhookTurretEvents()
	{
		if (m_bTurretHandlersRegistered && m_TurretEventHandler)
		{
			m_TurretEventHandler.RemoveScriptHandler("OnCompartmentEntered", this, OnCompartmentEntered);
			m_TurretEventHandler.RemoveScriptHandler("OnCompartmentLeft", this, OnCompartmentLeft);
		}

		m_bTurretHandlersRegistered = false;
		m_TurretEventHandler = null;
		m_eTurretController = null;
		m_eTurret = null;
	}

	//------------------------------------------------------------------------------------------------
	void OnCompartmentEntered(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerId, int slotID)
	{
		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	void OnCompartmentLeft(IEntity vehicle, BaseCompartmentManagerComponent mgr, IEntity occupant, int managerId, int slotID)
	{
		if (m_eCurrentPlayer && (!occupant || occupant == m_eCurrentPlayer))
			UnbindOccupant();

		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_ChimeraCharacter ResolveOccupant()
	{
		if (!m_eOwner)
			return null;

		if (m_eTurretController)
		{
			BaseCompartmentSlot slot = m_eTurretController.GetCompartmentSlot();
			if (!slot)
				return null;

			return SCR_ChimeraCharacter.Cast(slot.GetOccupant());
		}

		IEntity parent = m_eOwner.GetParent();
		while (parent)
		{
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(parent);
			if (character)
				return character;

			parent = parent.GetParent();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsStillHeldByPlayer()
	{
		if (!m_eCurrentPlayer)
			return false;

		return ResolveOccupant() == m_eCurrentPlayer;
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateOccupantAndOwnership()
	{
		if (!m_eOwner)
			return;

		RefreshTurretBinding();

		if (m_eCurrentPlayer && !IsStillHeldByPlayer())
			UnbindOccupant();

		SCR_ChimeraCharacter resolvedPlayer = ResolveOccupant();
		if (resolvedPlayer != m_eCurrentPlayer)
		{
			if (m_eCurrentPlayer)
				UnbindOccupant();

			if (resolvedPlayer)
			{
				m_eCurrentPlayer = resolvedPlayer;
				TransferOwnershipServer(resolvedPlayer);
			}
		}

		if (m_eCurrentPlayer)
		{
			if (Replication.IsServer())
				TransferOwnershipServer(m_eCurrentPlayer);

			RegisterListeners();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void TransferOwnershipServer(SCR_ChimeraCharacter player)
	{
		if (!Replication.IsServer() || !m_RplComponent || !player)
			return;

		if (!m_RplComponent.Id().IsValid())
			return;

		RplComponent playerRpl = player.GetRplComponent();
		if (!playerRpl || !playerRpl.Id().IsValid())
			return;

		RplIdentity targetIdentity = Replication.FindOwner(playerRpl.Id());
		if (!targetIdentity.IsValid())
			return;

		RplIdentity currentOwner = Replication.FindOwner(m_RplComponent.Id());
		if (currentOwner == targetIdentity)
			return;

		m_RplComponent.GiveExt(targetIdentity, false);
		Rpc(RpcDo_OnOccupantBound);
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindOccupant()
	{
		bool hadPlayer = (m_eCurrentPlayer != null);

		if (hadPlayer && Replication.IsServer() && m_RplComponent && m_RplComponent.Id().IsValid())
		{
			RplIdentity currentOwner = Replication.FindOwner(m_RplComponent.Id());
			if (currentOwner.IsValid() && currentOwner != RplIdentity.Local())
				Rpc(RpcDo_OnOccupantUnbound);
		}

		RemoveListeners();
		m_eCurrentPlayer = null;
		m_eLastTargetData = null;
		m_eLastMissile = null;

		if (hadPlayer && Replication.IsServer() && m_RplComponent && m_RplComponent.GetEntity() == m_eOwner && m_RplComponent.Id().IsValid())
		{
			RplIdentity currentOwner = Replication.FindOwner(m_RplComponent.Id());
			if (currentOwner.IsValid() && currentOwner != RplIdentity.Local())
				m_RplComponent.GiveExt(RplIdentity.Local(), false);
		}
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_OnOccupantBound()
	{
		UpdateOccupantAndOwnership();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_OnOccupantUnbound()
	{
		RemoveListeners();
		m_eCurrentPlayer = null;
		m_eLastTargetData = null;
		m_eLastMissile = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopLockInternal()
	{
		locking = false;
		m_fAdsDropTimer = 0.0;
		if (m_eLockTypeComponent)
		{
			m_eLockTypeComponent.StopLock();
			m_eLockTypeComponent.TerminateLockOnAudio();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool DoesWeaponMatchOwner(BaseWeaponComponent weapon)
	{
		if (!weapon || !m_eOwner)
			return false;

		if (weapon.GetOwner() == m_eOwner)
			return true;

		WeaponSlotComponent slot = WeaponSlotComponent.Cast(weapon);
		if (slot && slot.GetWeaponEntity() == m_eOwner)
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (!m_eOwner)
			m_eOwner = owner;

		UpdateOccupantAndOwnership();
		CheckMuzzleCapabilitiesOnChange();

		if (!IsLocallyOwned() || !IsLocalPlayerControlled() || !m_eCurrentPlayer || !m_eLockTypeComponent)
		{
			m_fAdsDropTimer = 0.0;
			if (locking)
			{
				locking = false;
				if (m_eLockTypeComponent)
					m_eLockTypeComponent.StopLock();
			}
			return;
		}

		bool isLauncherAds = false;
		if (m_eTurretController)
		{
			bool turretAds = m_eTurretController.IsWeaponADS()
				|| m_eTurretController.GetCurrentSightsADS()
				|| (m_WeaponComponent && m_WeaponComponent.IsSightADSActive());
			if (turretAds)
			{
				BaseWeaponManagerComponent turretWeaponMgr = m_eTurretController.GetWeaponManager();
				if (!turretWeaponMgr)
				{
					isLauncherAds = true;
				}
				else
				{
					BaseWeaponComponent turretWeapon = turretWeaponMgr.GetCurrentWeapon();
					if (!turretWeapon || DoesWeaponMatchOwner(turretWeapon))
						isLauncherAds = true;
				}
			}
		}
		else
		{
			BaseWeaponManagerComponent weaponMgr = m_eCurrentPlayer.GetWeaponManager();
			if (weaponMgr)
			{
				BaseWeaponComponent weaponComp = weaponMgr.GetCurrentWeapon();
				if (DoesWeaponMatchOwner(weaponComp))
				{
					CharacterControllerComponent charCtrl = m_eCurrentPlayer.GetCharacterController();
					isLauncherAds = weaponComp.IsSightADSActive() || (charCtrl && charCtrl.IsWeaponADS());
				}
			}
		}

		if (!m_eventHandler)
		{
			m_fAdsDropTimer = 0.0;
			if (locking)
			{
				locking = false;
				m_eLockTypeComponent.StopLock();
			}
			return;
		}

		if (!isLauncherAds)
		{
			if (locking)
			{
				// Tolerate transient 1-frame ADS flickers during turret/camera blends or MP reconciliation,
				// while stopping cleanly on real ADS exit. Dismount/unequip still clears immediately.
				m_fAdsDropTimer = m_fAdsDropTimer + timeSlice;
				if (m_fAdsDropTimer >= ADS_DROP_GRACE_SEC)
				{
					locking = false;
					m_fAdsDropTimer = 0.0;
					m_eLockTypeComponent.StopLock();
				}
			}
			else
			{
				m_fAdsDropTimer = 0.0;
			}
			return;
		}

		m_fAdsDropTimer = 0.0;

		if (!m_InputManager)
			m_InputManager = GetGame().GetInputManager();

		if (m_InputManager)
			m_InputManager.ActivateContext("CharacterWeaponGuidedLauncher");

		m_eLockTypeComponent.UpdateLock(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetLockingState(float value, EActionTrigger reason)
	{
		SetLockInputState(reason == EActionTrigger.DOWN);
	}

	//------------------------------------------------------------------------------------------------
	void SetLockInputState(bool active)
	{
		if (!IsLocallyOwned() || !m_eLockTypeComponent)
			return;

		m_fAdsDropTimer = 0.0;
		locking = active;
		if (locking)
			m_eLockTypeComponent.StartLock();
		else
			m_eLockTypeComponent.StopLock();
	}

	//------------------------------------------------------------------------------------------------
	protected void LockStartAquire(BGONE_LockingData_BASE lockingData)
	{
		if (!lockingData || !m_eLockTypeComponent || !IsLocalPlayerControlled())
			return;

		m_eLockTypeComponent.PlayLockOnAuido(lockingData.lockingProgress);
	}

	//------------------------------------------------------------------------------------------------
	protected void LockAquired(BGONE_LockingData_BASE lockingData)
	{
		if (!lockingData || !m_eLockTypeComponent || !IsLocalPlayerControlled())
			return;

		m_eLockTypeComponent.PlayLockOnAuido(lockingData.lockingProgress);
	}

	//------------------------------------------------------------------------------------------------
	protected void LockLost()
	{
		if (!m_eLockTypeComponent || !IsLocalPlayerControlled())
			return;

		m_eLockTypeComponent.TerminateLockOnAudio();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnLaunch(int playerID, BaseWeaponComponent weapon, IEntity entity)
	{
		if (!m_RplComponent || !entity)
			return;

		if (weapon && !DoesWeaponMatchOwner(weapon))
			return;

		BGONE_GuidedMissileComponent missile = BGONE_GuidedMissileComponent.Cast(entity.FindComponent(BGONE_GuidedMissileComponent));
		if (!missile)
			return;

		if (Replication.IsServer())
		{
			m_eLastMissile = missile;

			if (m_eLastTargetData)
			{
				BGONE_TargetData pendingData = m_eLastTargetData;
				m_eLastTargetData = null;
				CompleteServerLaunch(pendingData);
				return;
			}

			// Dedicated server with remote client owner waits for the owner's RpcAsk_SendTargetData.
			if (!IsLocallyOwned())
				return;
		}
		else if (!IsLocallyOwned())
		{
			return;
		}

		if (!m_eLockTypeComponent)
		{
			if (Replication.IsServer())
				m_eLastMissile = null;
			return;
		}

		BGONE_TargetData rawTargetData = m_eLockTypeComponent.GetCurrentTargetData();
		if (!rawTargetData)
		{
			if (Replication.IsServer())
				m_eLastMissile = null;
			return;
		}

		BGONE_TargetData clonedData = BGONE_TargetData.Cast(rawTargetData.Clone());
		if (!clonedData)
		{
			if (Replication.IsServer())
				m_eLastMissile = null;
			return;
		}

		clonedData.launchPos = entity.GetOrigin();
		clonedData.attackProfileIndex = m_iCurrentAttackModeIndex;
		clonedData.armingDistancesIndex = m_iCurrentArmingDistanceIndex;
		if (m_eTurretController)
		{
			RplId turretId = Replication.FindItemId(m_eTurretController);
			if (!turretId.IsValid() && m_eTurretController.GetOwner())
			{
				RplComponent turretRpl = RplComponent.Cast(m_eTurretController.GetOwner().FindComponent(RplComponent));
				if (turretRpl)
					turretId = turretRpl.Id();
			}
			clonedData.turretRplId = turretId;
		}

		if (Replication.IsServer())
			CompleteServerLaunch(clonedData);
		else
			Rpc(RpcAsk_SendTargetData, clonedData);
	}

	//------------------------------------------------------------------------------------------------
	protected void PruneActiveSaclosMissiles()
	{
		if (!m_aActiveSaclosMissiles)
		{
			m_aActiveSaclosMissiles = {};
			return;
		}

		for (int i = m_aActiveSaclosMissiles.Count() - 1; i >= 0; i--)
		{
			BGONE_GuidedMissileComponent missile = m_aActiveSaclosMissiles[i];
			if (!missile || missile.IsDetonated())
				m_aActiveSaclosMissiles.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool ResolveLocalSaclosAim(out vector aimDir, out vector aimPos)
	{
		aimDir = vector.Zero;
		aimPos = vector.Zero;

		if (m_eTurretController)
		{
			BaseSightsComponent sights = m_eTurretController.GetCurrentSights();
			if (!sights)
				return false;

			aimDir = sights.GetSightsDirectionUntransformed();
			aimPos = sights.GetSightsRearPosition();
			return aimDir.LengthSq() > 0.0001;
		}

		if (m_eCurrentPlayer)
		{
			AimingComponent headAim = m_eCurrentPlayer.GetHeadAimingComponent();
			if (!headAim)
				return false;

			aimDir = headAim.GetAimingDirectionWorld();
			aimPos = m_eCurrentPlayer.EyePosition();
			return aimDir.LengthSq() > 0.0001;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplySaclosAimToActiveMissiles(vector aimDir, vector aimPos)
	{
		if (!Replication.IsServer() || aimDir.LengthSq() < 0.0001)
			return;

		PruneActiveSaclosMissiles();
		foreach (BGONE_GuidedMissileComponent activeMissile : m_aActiveSaclosMissiles)
		{
			if (activeMissile && !activeMissile.IsDetonated())
				activeMissile.UpdateTurretAim(aimDir, aimPos);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CompleteServerLaunch(BGONE_TargetData targetData)
	{
		if (!Replication.IsServer() || !targetData || !m_eLastMissile)
			return;

		// Track all active SACLOS missiles so multiple in-flight missiles receive aim updates.
		PruneActiveSaclosMissiles();
		if (m_eLastMissile.IsSaclosGuided() && !m_aActiveSaclosMissiles.Contains(m_eLastMissile))
			m_aActiveSaclosMissiles.Insert(m_eLastMissile);

		m_aDbgCollisionShapes = m_eLastMissile.onLaunched(targetData, this);
		m_iServerLaunchCount = m_iServerLaunchCount + 1;
		m_iLastServerAttackProfileIndex = targetData.attackProfileIndex;

		// Consumed last missile and targetdata, reset.
		m_eLastTargetData = null;
		m_eLastMissile = null;
	}

	//------------------------------------------------------------------------------------------------
	void SaclosFix()
	{
		if (!Replication.IsServer())
			return;

		PruneActiveSaclosMissiles();
		if (m_aActiveSaclosMissiles.IsEmpty())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float nowMs = world.GetWorldTime();
			if (nowMs < m_fNextSaclosPollTime)
				return;

			m_fNextSaclosPollTime = nowMs + SACLOS_POLL_INTERVAL_MS;
		}

		if (IsLocallyOwned())
		{
			vector aimDir;
			vector aimPos;
			if (ResolveLocalSaclosAim(aimDir, aimPos))
				ApplySaclosAimToActiveMissiles(aimDir, aimPos);
			return;
		}

		Rpc(RpcDo_SaclosFix);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Owner)]
	void RpcDo_SaclosFix()
	{
		vector aimDir;
		vector aimPos;
		if (!ResolveLocalSaclosAim(aimDir, aimPos))
			return;

		if (Replication.IsServer())
		{
			ApplySaclosAimToActiveMissiles(aimDir, aimPos);
			return;
		}

		Rpc(RpcAsk_SaclosFix, aimDir, aimPos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	void RpcAsk_SaclosFix(vector aimDir, vector aimPos)
	{
		if (!Replication.IsServer() || aimDir.LengthSq() < 0.0001)
			return;

		ApplySaclosAimToActiveMissiles(aimDir, aimPos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_SendTargetData(BGONE_TargetData targetData)
	{
		if (!Replication.IsServer() || !targetData)
			return;

		if (!m_eLastMissile)
		{
			m_eLastTargetData = targetData;
			return;
		}

		CompleteServerLaunch(targetData);
	}

	//------------------------------------------------------------------------------------------------
	protected void RegisterListeners()
	{
		if (!m_eCurrentPlayer || !m_eLockTypeComponent)
			return;

		bool canBindShotHandler = Replication.IsServer() || IsLocallyOwned();
		if (canBindShotHandler && !m_bListenersRegistered)
		{
			if (!m_eventHandler)
			{
				m_eventHandler = EventHandlerManagerComponent.Cast(m_eCurrentPlayer.FindComponent(EventHandlerManagerComponent));
				if (!m_eventHandler)
				{
					Print("BGONE GuidedMissileLauncher - EventHandler Missing On Equipped Player", LogLevel.ERROR);
					return;
				}
			}

			m_eventHandler.RegisterScriptHandler("OnProjectileShot", this, OnLaunch);
			m_bListenersRegistered = true;
		}

		if (!m_bInputListenersRegistered && IsLocallyOwned() && IsLocalPlayerControlled())
		{
			if (!m_InputManager)
				m_InputManager = GetGame().GetInputManager();

			if (m_InputManager)
			{
				m_eLockTypeComponent.GetOnLockStartAquire().Insert(LockStartAquire);
				m_eLockTypeComponent.GetOnLockAquired().Insert(LockAquired);
				m_eLockTypeComponent.GetOnLockLost().Insert(LockLost);

				m_InputManager.AddActionListener("BGONELock", EActionTrigger.DOWN, SetLockingState);
				m_InputManager.AddActionListener("BGONELock", EActionTrigger.UP, SetLockingState);
				m_bInputListenersRegistered = true;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveListeners()
	{
		StopLockInternal();

		if (m_bInputListenersRegistered)
		{
			if (m_eLockTypeComponent)
			{
				m_eLockTypeComponent.GetOnLockStartAquire().Remove(LockStartAquire);
				m_eLockTypeComponent.GetOnLockAquired().Remove(LockAquired);
				m_eLockTypeComponent.GetOnLockLost().Remove(LockLost);
			}

			if (m_InputManager)
			{
				m_InputManager.RemoveActionListener("BGONELock", EActionTrigger.DOWN, SetLockingState);
				m_InputManager.RemoveActionListener("BGONELock", EActionTrigger.UP, SetLockingState);
			}

			m_bInputListenersRegistered = false;
		}

		if (m_bListenersRegistered && m_eventHandler)
			m_eventHandler.RemoveScriptHandler("OnProjectileShot", this, OnLaunch);

		m_bListenersRegistered = false;
		m_eventHandler = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckMuzzleCapabilitiesOnChange()
	{
		if (!m_eOwner)
			return;

		if (!m_WeaponComponent)
			m_WeaponComponent = BaseWeaponComponent.Cast(m_eOwner.FindComponent(BaseWeaponComponent));

		if (!m_WeaponComponent)
			return;

		BaseMuzzleComponent muzzle = m_WeaponComponent.GetCurrentMuzzle();
		BaseMagazineComponent curMag = null;
		if (muzzle)
			curMag = muzzle.GetMagazine();

		if (m_sLastInspectedAmmoTemplate.IsEmpty() || curMag != m_LastInspectedMagazine)
			RefreshMissileCapabilities();
	}

	//------------------------------------------------------------------------------------------------
	protected ResourceName ResolveLoadedAmmoTemplate()
	{
		if (!m_eOwner)
			return ResourceName.Empty;

		if (!m_WeaponComponent)
			m_WeaponComponent = BaseWeaponComponent.Cast(m_eOwner.FindComponent(BaseWeaponComponent));

		if (!m_WeaponComponent)
			return ResourceName.Empty;

		BaseMuzzleComponent muzzle = m_WeaponComponent.GetCurrentMuzzle();
		if (!muzzle)
		{
			array<BaseMuzzleComponent> muzzles = {};
			m_WeaponComponent.GetMuzzlesList(muzzles);
			if (!muzzles.IsEmpty())
				muzzle = muzzles[0];
		}

		if (!muzzle)
			return ResourceName.Empty;

		BaseMagazineComponent mag = muzzle.GetMagazine();
		m_LastInspectedMagazine = mag;
		if (mag && mag.GetOwner())
		{
			EntityPrefabData magPrefab = mag.GetOwner().GetPrefabData();
			if (magPrefab)
			{
				ResourceName magName = magPrefab.GetPrefabName();
				if (!magName.IsEmpty())
					return magName;
			}
		}

		ResourceName defaultName = muzzle.GetDefaultMagazineOrProjectileName();
		if (!defaultName.IsEmpty())
			return defaultName;

		BaseContainer muzzleSrc = muzzle.GetComponentSource(m_eOwner);
		if (muzzleSrc)
		{
			ResourceName ammoAttr;
			if (muzzleSrc.Get("AmmoTemplate", ammoAttr) && !ammoAttr.IsEmpty())
				return ammoAttr;
		}

		return ResourceName.Empty;
	}

	//------------------------------------------------------------------------------------------------
	void RefreshMissileCapabilities()
	{
		ResourceName ammoTemplate = ResolveLoadedAmmoTemplate();
		if (ammoTemplate.IsEmpty() || ammoTemplate == m_sLastInspectedAmmoTemplate)
			return;

		m_sLastInspectedAmmoTemplate = ammoTemplate;
		DiscoverMissileCapabilitiesFromTemplate(ammoTemplate);
	}

	//------------------------------------------------------------------------------------------------
	protected void DiscoverMissileCapabilitiesFromTemplate(ResourceName ammoTemplate)
	{
		if (ammoTemplate.IsEmpty())
			return;

		Resource res = Resource.Load(ammoTemplate);
		if (!res || !res.IsValid())
			return;

		BaseResourceObject resObj = res.GetResource();
		if (!resObj)
			return;

		IEntitySource entSrc = resObj.ToEntitySource();
		if (!entSrc)
			return;

		IEntityComponentSource missileCompSrc = SCR_BaseContainerTools.FindComponentSource(entSrc, BGONE_GuidedMissileComponent);
		if (!missileCompSrc)
			return;

		array<ref BGONE_AttackProfile_Base> discoveredProfiles = {};
		BaseContainerList profileList = missileCompSrc.GetObjectArray("m_eAttackProfileComponents");
		if (profileList)
		{
			int profileCount = profileList.Count();
			for (int i = 0; i < profileCount; i++)
			{
				BaseContainer profileContainer = profileList.Get(i);
				if (!profileContainer)
					continue;

				BGONE_AttackProfile_Base profileObj = BGONE_AttackProfile_Base.Cast(BaseContainerTools.CreateInstanceFromContainer(profileContainer));
				if (profileObj)
					discoveredProfiles.Insert(profileObj);
			}
		}

		if (!discoveredProfiles.IsEmpty())
			SetAvailableAttackProfiles(discoveredProfiles);

		array<int> discoveredDistances = {};
		BaseContainer seekerContainer = missileCompSrc.GetObject("m_eSeekerTypeComponent");
		if (seekerContainer)
		{
			array<int> rawDistances;
			if (seekerContainer.Get("m_fArmingDistances", rawDistances) && rawDistances && !rawDistances.IsEmpty())
			{
				foreach (int dist : rawDistances)
				{
					discoveredDistances.Insert(dist);
				}
			}
			else
			{
				BGONE_SeekerType_Base seekerObj = BGONE_SeekerType_Base.Cast(BaseContainerTools.CreateInstanceFromContainer(seekerContainer));
				if (seekerObj)
				{
					array<int> seekerDistances = seekerObj.GetAvailableArmingDistances();
					if (seekerDistances)
					{
						foreach (int fallbackDist : seekerDistances)
						{
							discoveredDistances.Insert(fallbackDist);
						}
					}
				}
			}
		}

		if (!discoveredDistances.IsEmpty())
			SetAvailableArmingDistances(discoveredDistances);
	}

	//------------------------------------------------------------------------------------------------
	// Methods for registering available firemodes and arming distances.
	void SetAvailableAttackProfiles(array<ref BGONE_AttackProfile_Base> attackProfiles)
	{
		m_eSupportedAttackProfiles = attackProfiles;
		if (!m_eSupportedAttackProfiles || m_iCurrentAttackModeIndex < 0 || m_iCurrentAttackModeIndex >= m_eSupportedAttackProfiles.Count())
			m_iCurrentAttackModeIndex = 0;
	}

	//------------------------------------------------------------------------------------------------
	void SetAvailableArmingDistances(array<int> armingDistances)
	{
		m_aAvailableArmingDistances = armingDistances;
		if (!m_aAvailableArmingDistances || m_iCurrentArmingDistanceIndex < 0 || m_iCurrentArmingDistanceIndex >= m_aAvailableArmingDistances.Count())
			m_iCurrentArmingDistanceIndex = 0;
	}

	//------------------------------------------------------------------------------------------------
	// Methods for selecting firemode and armingdistance on the launcher.
	int GetArmingDistancesCount()
	{
		if (!m_aAvailableArmingDistances)
			return 0;

		return m_aAvailableArmingDistances.Count();
	}

	//------------------------------------------------------------------------------------------------
	void CycleArmingDistance()
	{
		int count = GetArmingDistancesCount();
		if (count <= 0)
		{
			m_iCurrentArmingDistanceIndex = 0;
			return;
		}

		if (m_iCurrentArmingDistanceIndex < count - 1)
			m_iCurrentArmingDistanceIndex = m_iCurrentArmingDistanceIndex + 1;
		else
			m_iCurrentArmingDistanceIndex = 0;
	}

	//------------------------------------------------------------------------------------------------
	int GetCurrentArmingDistance()
	{
		if (!m_aAvailableArmingDistances || m_aAvailableArmingDistances.IsEmpty())
			return 0;

		if (m_iCurrentArmingDistanceIndex < 0 || m_iCurrentArmingDistanceIndex >= m_aAvailableArmingDistances.Count())
			m_iCurrentArmingDistanceIndex = 0;

		return m_aAvailableArmingDistances[m_iCurrentArmingDistanceIndex];
	}

	//------------------------------------------------------------------------------------------------
	int GetAttackModesCount()
	{
		if (!m_eSupportedAttackProfiles)
			return 0;

		return m_eSupportedAttackProfiles.Count();
	}

	//------------------------------------------------------------------------------------------------
	void CycleAttackMode()
	{
		int count = GetAttackModesCount();
		if (count <= 0)
		{
			m_iCurrentAttackModeIndex = 0;
			return;
		}

		if (m_iCurrentAttackModeIndex < count - 1)
			m_iCurrentAttackModeIndex = m_iCurrentAttackModeIndex + 1;
		else
			m_iCurrentAttackModeIndex = 0;
	}

	//------------------------------------------------------------------------------------------------
	BGONE_AttackProfile_Base GetCurrentAttackMode()
	{
		if (!m_eSupportedAttackProfiles || m_eSupportedAttackProfiles.IsEmpty())
			return null;

		if (m_iCurrentAttackModeIndex < 0 || m_iCurrentAttackModeIndex >= m_eSupportedAttackProfiles.Count())
			m_iCurrentAttackModeIndex = 0;

		return m_eSupportedAttackProfiles[m_iCurrentAttackModeIndex];
	}

	//------------------------------------------------------------------------------------------------
	// Read-only telemetry getters.
	BGONE_LockType_Base GetLockTypeComponent()
	{
		return m_eLockTypeComponent;
	}

	//------------------------------------------------------------------------------------------------
	bool IsLocking()
	{
		return locking;
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetCurrentOwnerEntity()
	{
		return m_eCurrentPlayer;
	}

	//------------------------------------------------------------------------------------------------
	bool IsLauncherBound()
	{
		return m_eCurrentPlayer != null && m_eventHandler != null && m_bListenersRegistered;
	}

	//------------------------------------------------------------------------------------------------
	int GetAvailableAttackProfileCount()
	{
		return GetAttackModesCount();
	}

	//------------------------------------------------------------------------------------------------
	int GetAvailableArmingDistanceCount()
	{
		return GetArmingDistancesCount();
	}

	//------------------------------------------------------------------------------------------------
	int GetSelectedAttackProfileIndex()
	{
		return m_iCurrentAttackModeIndex;
	}

	//------------------------------------------------------------------------------------------------
	int GetSelectedArmingDistanceIndex()
	{
		return m_iCurrentArmingDistanceIndex;
	}

	//------------------------------------------------------------------------------------------------
	int GetServerLaunchCount()
	{
		return m_iServerLaunchCount;
	}

	//------------------------------------------------------------------------------------------------
	int GetLastServerAttackProfileIndex()
	{
		return m_iLastServerAttackProfileIndex;
	}
};
