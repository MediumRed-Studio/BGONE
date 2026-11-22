/*!
	\brief Headless MP QA bot scenarios for BGONE launcher lifecycle, input context, muzzle profile
	discovery, and turret mount/dismount cleanup.

	Scenarios:
	  - launcher_pickup : server spawns BGONE_Launcher_VIS near player; client picks up & equips,
	                      verifies self-healing binding, MuzzleInMagComponent profile discovery,
	                      attack-profile cycling, ADS context activation, and clean lock stop on ADS exit.
	  - turret_dismount : server spawns BGONE_QA_TurretVehicle; client teleports into turret,
	                      verifies turret launcher binding, turret ADS + lock activation, clean
	                      dismount teardown, and 2s post-dismount stability hold.
*/
class BGONE_BotLauncherHelper
{
	static const ResourceName PREFAB_LAUNCHER_VIS = "{82C0AFB409A3AE0B}Prefabs/Weapons/Launchers/BGONE_Launcher_VIS.et";
	static const ResourceName PREFAB_QA_TURRET = "{6F4B9A21C8E3D074}Prefabs/QA/BGONE_QA_TurretVehicle.et";
	static const string LAUNCHER_INPUT_CONTEXT = "CharacterWeaponGuidedLauncher";

	//------------------------------------------------------------------------------------------------
	static BGONE_GuidedMissileLauncherComponent FindLauncherOnEntity(IEntity ent, int depth = 0)
	{
		if (!ent || depth > 6)
			return null;

		BGONE_GuidedMissileLauncherComponent comp = BGONE_GuidedMissileLauncherComponent.Cast(ent.FindComponent(BGONE_GuidedMissileLauncherComponent));
		if (comp)
			return comp;

		BaseWeaponComponent weaponComp = BaseWeaponComponent.Cast(ent.FindComponent(BaseWeaponComponent));
		if (weaponComp)
		{
			comp = BGONE_GuidedMissileLauncherComponent.Cast(weaponComp.FindComponent(BGONE_GuidedMissileLauncherComponent));
			if (comp)
				return comp;
		}

		IEntity child = ent.GetChildren();
		while (child)
		{
			comp = FindLauncherOnEntity(child, depth + 1);
			if (comp)
				return comp;

			child = child.GetSibling();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	static WeaponSlotComponent FindSlotHoldingEntity(BaseWeaponManagerComponent weaponMgr, IEntity weaponEnt)
	{
		if (!weaponMgr || !weaponEnt)
			return null;

		array<WeaponSlotComponent> slots = {};
		weaponMgr.GetWeaponsSlots(slots);
		foreach (WeaponSlotComponent slot : slots)
		{
			if (slot && slot.GetWeaponEntity() == weaponEnt)
				return slot;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	static TurretCompartmentSlot FindTurretSlot(IEntity turretEnt)
	{
		if (!turretEnt)
			return null;

		BaseCompartmentManagerComponent compMgr = BaseCompartmentManagerComponent.Cast(turretEnt.FindComponent(BaseCompartmentManagerComponent));
		if (!compMgr)
			return null;

		array<BaseCompartmentSlot> slots = {};
		compMgr.GetCompartments(slots);
		foreach (BaseCompartmentSlot slot : slots)
		{
			TurretCompartmentSlot turretSlot = TurretCompartmentSlot.Cast(slot);
			if (turretSlot)
				return turretSlot;
		}

		return null;
	}
}

//------------------------------------------------------------------------------------------------
//! Pick up a server-staged launcher near the player, equip it, and verify self-healing binding.
class BGONE_BotBeat_PickupAndEquipLauncher : BGONE_BotBeat
{
	protected int m_iLastAttemptMs;
	protected bool m_bInserted;
	protected bool m_bEquipped;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_PickupAndEquipLauncher(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not pick up, equip, and bind the guided missile launcher.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iLastAttemptMs = -1000;
		m_bInserted = false;
		m_bEquipped = false;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		if (!ch || !ctrl)
		{
			m_sDetail = "no controlled character";
			return EBGONE_BotBeatState.FAIL;
		}

		IEntity launcherEnt = run.FixtureEntity();
		if (!launcherEnt)
			return EBGONE_BotBeatState.RUNNING;

		BaseWeaponManagerComponent weaponMgr = ch.GetWeaponManager();
		if (!weaponMgr)
		{
			m_sDetail = "character missing BaseWeaponManagerComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		WeaponSlotComponent slot = BGONE_BotLauncherHelper.FindSlotHoldingEntity(weaponMgr, launcherEnt);
		if (!slot)
		{
			if (elapsedMs - m_iLastAttemptMs >= 500)
			{
				m_iLastAttemptMs = elapsedMs;
				SCR_InventoryStorageManagerComponent invMgr = SCR_InventoryStorageManagerComponent.Cast(ch.FindComponent(SCR_InventoryStorageManagerComponent));
				if (!invMgr)
				{
					m_sDetail = "character missing SCR_InventoryStorageManagerComponent";
					return EBGONE_BotBeatState.FAIL;
				}

				if (!invMgr.TryInsertItem(launcherEnt, EStoragePurpose.PURPOSE_WEAPON_PROXY))
					invMgr.EquipWeapon(launcherEnt);
			}
			return EBGONE_BotBeatState.RUNNING;
		}

		m_bInserted = true;

		BaseWeaponComponent curWeapon = weaponMgr.GetCurrentWeapon();
		if (!curWeapon || curWeapon.GetOwner() != launcherEnt)
		{
			if (elapsedMs - m_iLastAttemptMs >= 400)
			{
				m_iLastAttemptMs = elapsedMs;
				ctrl.SelectWeapon(slot);
			}
			return EBGONE_BotBeatState.RUNNING;
		}

		m_bEquipped = true;

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "equipped launcher missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (!launcher.IsLauncherBound() || launcher.GetCurrentOwnerEntity() != ch)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry(string.Format(
			"launcher_bound=1 profiles=%1 arming=%2",
			launcher.GetAvailableAttackProfileCount(), launcher.GetAvailableArmingDistanceCount()));
		m_sDetail = string.Format(
			"picked up, equipped, and bound in %1 ms (profiles=%2, arming=%3)",
			elapsedMs, launcher.GetAvailableAttackProfileCount(), launcher.GetAvailableArmingDistanceCount());
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		IEntity launcherEnt = run.FixtureEntity();
		bool bound = false;
		if (launcherEnt)
		{
			BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
			if (launcher)
				bound = launcher.IsLauncherBound();
		}

		return string.Format(
			"pickup/equip timed out after %1 ms (fixture=%2 inserted=%3 equipped=%4 bound=%5)",
			m_iTimeoutMs, launcherEnt != null, m_bInserted, m_bEquipped, bound);
	}
}

//------------------------------------------------------------------------------------------------
//! Verify MuzzleInMagComponent attack-profile discovery and cycle attack profile via inspection action.
class BGONE_BotBeat_VerifyProfilesAndCycle : BGONE_BotBeat
{
	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyProfilesAndCycle(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Launcher did not discover or cycle attack profiles from MuzzleInMagComponent.");
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ch || !launcherEnt)
		{
			m_sDetail = "missing character or launcher fixture";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "launcher missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		int profileCount = launcher.GetAvailableAttackProfileCount();
		if (profileCount <= 0)
		{
			m_sDetail = "GetAvailableAttackProfileCount() == 0 (MuzzleInMagComponent discovery failed)";
			return EBGONE_BotBeatState.FAIL;
		}

		int beforeIdx = launcher.GetSelectedAttackProfileIndex();
		string beforeName = "";
		BGONE_AttackProfile_Base beforeProfile = launcher.GetCurrentAttackMode();
		if (beforeProfile)
			beforeName = beforeProfile.GetProfileName();

		bool usedAction = false;
		string actionName = "";
		BaseActionsManagerComponent actionsMgr = BaseActionsManagerComponent.Cast(launcherEnt.FindComponent(BaseActionsManagerComponent));
		if (actionsMgr)
		{
			array<BaseUserAction> actions = {};
			actionsMgr.GetActionsList(actions);
			foreach (BaseUserAction act : actions)
			{
				BGONE_AttackProfileSwitchUserAction switchAct = BGONE_AttackProfileSwitchUserAction.Cast(act);
				if (switchAct)
				{
					switchAct.GetActionNameScript(actionName);
					switchAct.PerformAction(launcherEnt, ch);
					usedAction = true;
					break;
				}
			}
		}

		if (!usedAction)
			launcher.CycleAttackMode();

		int afterIdx = launcher.GetSelectedAttackProfileIndex();
		string afterName = "";
		BGONE_AttackProfile_Base afterProfile = launcher.GetCurrentAttackMode();
		if (afterProfile)
			afterName = afterProfile.GetProfileName();

		if (profileCount > 1 && afterIdx == beforeIdx)
		{
			m_sDetail = string.Format(
				"attack profile index did not change (count=%1 before=%2 after=%3 usedAction=%4)",
				profileCount, beforeIdx, afterIdx, usedAction);
			return EBGONE_BotBeatState.FAIL;
		}

		run.LogTelemetry(string.Format(
			"profile_count=%1 profile_before=%2 profile_after=%3 used_action=%4",
			profileCount, beforeIdx, afterIdx, usedAction));
		m_sDetail = string.Format(
			"discovered %1 profile(s), cycled %2 ('%3') -> %4 ('%5') via action=%6 ('%7')",
			profileCount, beforeIdx, beforeName, afterIdx, afterName, usedAction, actionName);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Enter hand-held ADS, verify CharacterWeaponGuidedLauncher context activates, and engage lock.
class BGONE_BotBeat_VerifyAdsContextAndLock : BGONE_BotBeat
{
	protected static const int LOCK_HOLD_MS = 300;
	protected bool m_bContextSeen;
	protected bool m_bLockStarted;
	protected int m_iLockStartMs;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_VerifyAdsContextAndLock(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Launcher ADS input context or lock activation did not work.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bContextSeen = false;
		m_bLockStarted = false;
		m_iLockStartMs = -1;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ctrl || !launcherEnt)
		{
			m_sDetail = "missing character controller or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		ctrl.SetWeaponRaised(true);
		ctrl.SetWeaponADS(true);
		ctrl.SetWeaponADSInput(true);

		if (!ctrl.IsWeaponADS())
			return EBGONE_BotBeatState.RUNNING;

		InputManager inputMgr = GetGame().GetInputManager();
		if (!inputMgr || !inputMgr.IsContextActive(BGONE_BotLauncherHelper.LAUNCHER_INPUT_CONTEXT))
			return EBGONE_BotBeatState.RUNNING;

		m_bContextSeen = true;

		if (!m_bLockStarted)
		{
			launcher.SetLockInputState(true);
			m_bLockStarted = true;
			m_iLockStartMs = elapsedMs;
		}

		if (!launcher.IsLocking())
		{
			m_sDetail = "launcher IsLocking() became false while still in ADS";
			return EBGONE_BotBeatState.FAIL;
		}

		if (elapsedMs - m_iLockStartMs < LOCK_HOLD_MS)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry("ads_context=1 locking_in_ads=1");
		m_sDetail = string.Format("ADS context active and lock held for %1 ms", LOCK_HOLD_MS);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		bool isAds = false;
		if (ctrl)
			isAds = ctrl.IsWeaponADS();

		return string.Format(
			"ADS/lock timed out after %1 ms (isAds=%2 contextSeen=%3 lockStarted=%4)",
			m_iTimeoutMs, isAds, m_bContextSeen, m_bLockStarted);
	}
}

//------------------------------------------------------------------------------------------------
//! Exit ADS and verify locking stops cleanly and stays stopped.
class BGONE_BotBeat_ExitAdsVerifyLockCleared : BGONE_BotBeat
{
	protected static const int POST_EXIT_HOLD_MS = 500;
	protected int m_iClearedAtMs;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_ExitAdsVerifyLockCleared(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Lock did not clear cleanly on ADS exit.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iClearedAtMs = -1;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		SCR_CharacterControllerComponent ctrl = run.CharacterController();
		IEntity launcherEnt = run.FixtureEntity();
		if (!ctrl || !launcherEnt)
		{
			m_sDetail = "missing character controller or launcher";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(launcherEnt);
		if (!launcher)
		{
			m_sDetail = "missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		ctrl.SetWeaponADSInput(false);
		ctrl.SetWeaponADS(false);

		if (ctrl.IsWeaponADS() || launcher.IsLocking())
		{
			m_iClearedAtMs = -1;
			return EBGONE_BotBeatState.RUNNING;
		}

		if (m_iClearedAtMs < 0)
			m_iClearedAtMs = elapsedMs;

		if (elapsedMs - m_iClearedAtMs < POST_EXIT_HOLD_MS)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry("ads_exit_lock_cleared=1");
		m_sDetail = string.Format("ADS exited and IsLocking() stayed false for %1 ms", POST_EXIT_HOLD_MS);
		return EBGONE_BotBeatState.PASS;
	}
}

//------------------------------------------------------------------------------------------------
//! Teleport into the staged QA turret vehicle and verify the turret-mounted launcher binds cleanly.
class BGONE_BotBeat_MountTurret : BGONE_BotBeat
{
	protected int m_iLastAttemptMs;
	protected bool m_bMounted;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_MountTurret(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Could not mount QA turret and bind its guided missile launcher.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iLastAttemptMs = -1000;
		m_bMounted = false;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		if (!ch)
		{
			m_sDetail = "no controlled character";
			return EBGONE_BotBeatState.FAIL;
		}

		IEntity turretEnt = run.FixtureEntity();
		if (!turretEnt)
			return EBGONE_BotBeatState.RUNNING;

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (!access)
		{
			m_sDetail = "character missing CompartmentAccessComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (!access.IsInCompartment())
		{
			if (elapsedMs - m_iLastAttemptMs >= 500)
			{
				m_iLastAttemptMs = elapsedMs;
				TurretCompartmentSlot turretSlot = BGONE_BotLauncherHelper.FindTurretSlot(turretEnt);
				if (!turretSlot)
				{
					m_sDetail = "QA turret missing TurretCompartmentSlot";
					return EBGONE_BotBeatState.FAIL;
				}

				access.GetInVehicle(turretEnt, turretSlot, true, -1, ECloseDoorAfterActions.INVALID, false);
			}
			return EBGONE_BotBeatState.RUNNING;
		}

		m_bMounted = true;

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(turretEnt);
		if (!launcher)
		{
			m_sDetail = "QA turret missing child BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (!launcher.IsLauncherBound() || launcher.GetCurrentOwnerEntity() != ch)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry(string.Format(
			"turret_bound=1 profiles=%1", launcher.GetAvailableAttackProfileCount()));
		m_sDetail = string.Format("mounted turret and bound launcher in %1 ms", elapsedMs);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		IEntity turretEnt = run.FixtureEntity();
		bool bound = false;
		if (turretEnt)
		{
			BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(turretEnt);
			if (launcher)
				bound = launcher.IsLauncherBound();
		}

		return string.Format(
			"mount turret timed out after %1 ms (fixture=%2 mounted=%3 bound=%4)",
			m_iTimeoutMs, turretEnt != null, m_bMounted, bound);
	}
}

//------------------------------------------------------------------------------------------------
//! Enter turret ADS, verify input context activates, and engage lock while mounted.
class BGONE_BotBeat_TurretAdsAndLock : BGONE_BotBeat
{
	protected static const int LOCK_HOLD_MS = 400;
	protected bool m_bAdsReached;
	protected bool m_bAdsInputSent;
	protected bool m_bContextSeen;
	protected bool m_bLockStarted;
	protected int m_iLockStartMs;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_TurretAdsAndLock(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Turret ADS or lock activation did not work.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_bAdsReached = false;
		m_bAdsInputSent = false;
		m_bContextSeen = false;
		m_bLockStarted = false;
		m_iLockStartMs = -1;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		IEntity turretEnt = run.FixtureEntity();
		if (!turretEnt)
		{
			m_sDetail = "missing turret fixture";
			return EBGONE_BotBeatState.FAIL;
		}

		TurretControllerComponent turretCtrl = TurretControllerComponent.Cast(turretEnt.FindComponent(TurretControllerComponent));
		if (!turretCtrl)
		{
			m_sDetail = "turret missing TurretControllerComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(turretEnt);
		if (!launcher)
		{
			m_sDetail = "turret missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		bool adsBefore = turretCtrl.IsWeaponADS();
		if (!turretCtrl.GetWeaponADSEnabled())
			turretCtrl.SetWeaponADSEnabled(true);

		if (!adsBefore)
		{
			turretCtrl.SetWeaponADS(true);
			if (!m_bAdsInputSent && elapsedMs >= 300)
			{
				turretCtrl.SetWeaponADSInput(true);
				m_bAdsInputSent = true;
			}
		}

		if (!turretCtrl.IsWeaponADS())
			return EBGONE_BotBeatState.RUNNING;

		m_bAdsReached = true;

		InputManager inputMgr = GetGame().GetInputManager();
		if (!inputMgr || !inputMgr.IsContextActive(BGONE_BotLauncherHelper.LAUNCHER_INPUT_CONTEXT))
			return EBGONE_BotBeatState.RUNNING;

		m_bContextSeen = true;

		if (!m_bLockStarted)
		{
			launcher.SetLockInputState(true);
			m_bLockStarted = true;
			m_iLockStartMs = elapsedMs;
		}

		if (!launcher.IsLocking())
		{
			BaseWeaponManagerComponent tMgr = turretCtrl.GetWeaponManager();
			string curWepClass = "none";
			bool wepOwnerMatches = false;
			if (tMgr && tMgr.GetCurrentWeapon())
			{
				BaseWeaponComponent curWep = tMgr.GetCurrentWeapon();
				curWepClass = curWep.ClassName();
				wepOwnerMatches = (curWep.GetOwner() == launcher.GetOwner());
			}

			m_sDetail = string.Format(
				"turret launcher IsLocking() became false while in turret ADS (t=%1/%2 adsBefore=%3 isAds=%4 ctx=%5 bound=%6 wep=%7)",
				elapsedMs, m_iLockStartMs, adsBefore, turretCtrl.IsWeaponADS(),
				inputMgr.IsContextActive(BGONE_BotLauncherHelper.LAUNCHER_INPUT_CONTEXT),
				launcher.IsLauncherBound(), string.Format("%1:%2", curWepClass, wepOwnerMatches));
			return EBGONE_BotBeatState.FAIL;
		}

		if (elapsedMs - m_iLockStartMs < LOCK_HOLD_MS)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry("turret_ads_context=1 turret_locking=1");
		m_sDetail = string.Format("turret ADS active and lock held for %1 ms", LOCK_HOLD_MS);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		return string.Format(
			"turret ADS/lock timed out after %1 ms (ads=%2 context=%3 lockStarted=%4)",
			m_iTimeoutMs, m_bAdsReached, m_bContextSeen, m_bLockStarted);
	}
}

//------------------------------------------------------------------------------------------------
//! Dismount turret via teleport and verify lock, listeners, and occupant references clear cleanly.
class BGONE_BotBeat_DismountTurretVerifyClean : BGONE_BotBeat
{
	protected int m_iLastAttemptMs;
	protected bool m_bDismounted;

	//------------------------------------------------------------------------------------------------
	void BGONE_BotBeat_DismountTurretVerifyClean(string beatName, int timeoutMs)
	{
		Setup(beatName, timeoutMs);
		SetFailSay("Turret dismount did not cleanly stop lock and unbind listeners.");
	}

	//------------------------------------------------------------------------------------------------
	override void OnStart(BGONE_BotRun run)
	{
		m_iLastAttemptMs = -1000;
		m_bDismounted = false;
	}

	//------------------------------------------------------------------------------------------------
	override EBGONE_BotBeatState Tick(BGONE_BotRun run, int elapsedMs)
	{
		ChimeraCharacter ch = run.Character();
		IEntity turretEnt = run.FixtureEntity();
		if (!ch || !turretEnt)
		{
			m_sDetail = "missing character or turret fixture";
			return EBGONE_BotBeatState.FAIL;
		}

		CompartmentAccessComponent access = ch.GetCompartmentAccessComponent();
		if (!access)
		{
			m_sDetail = "character missing CompartmentAccessComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (access.IsInCompartment())
		{
			if (elapsedMs - m_iLastAttemptMs >= 500)
			{
				m_iLastAttemptMs = elapsedMs;
				access.GetOutVehicle(EGetOutType.TELEPORT, -1, ECloseDoorAfterActions.INVALID, false);
			}
			return EBGONE_BotBeatState.RUNNING;
		}

		m_bDismounted = true;

		BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(turretEnt);
		if (!launcher)
		{
			m_sDetail = "turret missing BGONE_GuidedMissileLauncherComponent";
			return EBGONE_BotBeatState.FAIL;
		}

		if (launcher.IsLocking() || launcher.IsLauncherBound() || launcher.GetCurrentOwnerEntity() != null)
			return EBGONE_BotBeatState.RUNNING;

		run.LogTelemetry("turret_dismount_clean=1");
		m_sDetail = string.Format("dismounted turret cleanly in %1 ms (locking=0 bound=0 owner=null)", elapsedMs);
		return EBGONE_BotBeatState.PASS;
	}

	//------------------------------------------------------------------------------------------------
	override string TimeoutDetail(BGONE_BotRun run)
	{
		IEntity turretEnt = run.FixtureEntity();
		bool lockingNow = false;
		bool boundNow = false;
		if (turretEnt)
		{
			BGONE_GuidedMissileLauncherComponent launcher = BGONE_BotLauncherHelper.FindLauncherOnEntity(turretEnt);
			if (launcher)
			{
				lockingNow = launcher.IsLocking();
				boundNow = launcher.IsLauncherBound();
			}
		}

		return string.Format(
			"dismount cleanup timed out after %1 ms (dismounted=%2 locking=%3 bound=%4)",
			m_iTimeoutMs, m_bDismounted, lockingNow, boundNow);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController : PlayerController
{
	static const string BGONE_SCENARIO_LAUNCHER_PICKUP = "launcher_pickup";
	static const string BGONE_SCENARIO_TURRET_DISMOUNT = "turret_dismount";

	//------------------------------------------------------------------------------------------------
	override protected BGONE_BotRun BGONE_BotBuildScenario(string scenarioName)
	{
		if (scenarioName == BGONE_SCENARIO_LAUNCHER_PICKUP)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "Launcher pickup, muzzle profile discovery, ADS context & lock teardown";
			run.m_sSummary = "Picked up BGONE_Launcher_VIS, verified MuzzleInMagComponent profiles, cycled attack mode, verified ADS context and lock teardown.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 15000));
			run.Add(new BGONE_BotBeat_PickupAndEquipLauncher("pickup_and_equip_launcher", 20000));
			run.Add(new BGONE_BotBeat_VerifyProfilesAndCycle("verify_profiles_and_cycle", 10000));
			run.Add(new BGONE_BotBeat_VerifyAdsContextAndLock("verify_ads_context_and_lock", 10000));
			run.Add(new BGONE_BotBeat_ExitAdsVerifyLockCleared("exit_ads_verify_lock_cleared", 10000));
			return run;
		}

		if (scenarioName == BGONE_SCENARIO_TURRET_DISMOUNT)
		{
			BGONE_BotRun run = new BGONE_BotRun(scenarioName);
			run.m_sTitle = "Turret mount, turret ADS lock, clean dismount teardown & stability hold";
			run.m_sSummary = "Mounted BGONE_QA_TurretVehicle, locked in turret ADS, dismounted cleanly with zero leaks, and held 2s post-dismount.";
			run.Add(new BGONE_BotBeat_WaitDeployed("wait_deployed", 15000));
			run.Add(new BGONE_BotBeat_MountTurret("mount_turret", 20000));
			run.Add(new BGONE_BotBeat_TurretAdsAndLock("turret_ads_and_lock", 15000));
			run.Add(new BGONE_BotBeat_DismountTurretVerifyClean("dismount_turret_verify_clean", 15000));
			run.Add(new BGONE_BotBeat_WaitDuration("hold_post_dismount", 2000));
			return run;
		}

		return super.BGONE_BotBuildScenario(scenarioName);
	}

	//------------------------------------------------------------------------------------------------
	override protected int BGONE_BotStageScenario(string scenarioName, IEntity character, out RplId fixtureId, out vector fixturePos, out string detail)
	{
		if (scenarioName == BGONE_SCENARIO_LAUNCHER_PICKUP)
		{
			return BGONE_StageLauncherEntity(
				scenarioName,
				BGONE_BotLauncherHelper.PREFAB_LAUNCHER_VIS,
				character,
				0.9,
				0.35,
				fixtureId,
				fixturePos,
				detail);
		}

		if (scenarioName == BGONE_SCENARIO_TURRET_DISMOUNT)
		{
			return BGONE_StageLauncherEntity(
				scenarioName,
				BGONE_BotLauncherHelper.PREFAB_QA_TURRET,
				character,
				3.0,
				0.0,
				fixtureId,
				fixturePos,
				detail);
		}

		return super.BGONE_BotStageScenario(scenarioName, character, fixtureId, fixturePos, detail);
	}

	//------------------------------------------------------------------------------------------------
	protected int BGONE_StageLauncherEntity(
		string scenarioName,
		ResourceName prefab,
		IEntity character,
		float forwardDist,
		float heightOffset,
		out RplId fixtureId,
		out vector fixturePos,
		out string detail)
	{
		fixtureId = RplId.Invalid();
		fixturePos = vector.Zero;

		if (!character)
		{
			detail = "no character to stage near";
			return 10;
		}

		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
		{
			detail = string.Format("failed to load prefab %1", prefab);
			return 11;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			detail = "no active world";
			return 12;
		}

		vector fwd = character.GetWorldTransformAxis(2);
		fwd[1] = 0;
		if (fwd.LengthSq() < 0.001)
			fwd = Vector(0, 0, 1);
		else
			fwd.Normalize();

		vector origin = character.GetOrigin() + fwd * forwardDist;
		origin[1] = world.GetSurfaceY(origin[0], origin[2]) + heightOffset;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = origin;

		IEntity spawned = GetGame().SpawnEntityPrefab(res, world, params);
		if (!spawned)
		{
			detail = string.Format("failed to spawn %1 at %2", prefab, origin);
			return 13;
		}

		BGONE_BotSpawnLedger.Add(spawned, scenarioName);
		m_BGONE_ServerStagedEntityId = spawned.GetID();
		m_vBGONE_ServerStartPos = character.GetOrigin();
		fixturePos = spawned.GetOrigin();

		RplComponent rpl = RplComponent.Cast(spawned.FindComponent(RplComponent));
		if (rpl && rpl.Id().IsValid())
			fixtureId = rpl.Id();

		detail = string.Format("staged %1 at %2", prefab, fixturePos);
		return 0;
	}
}
