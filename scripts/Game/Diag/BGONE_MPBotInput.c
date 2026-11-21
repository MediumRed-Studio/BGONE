/*!
	\brief Headless input injection adapter for BGONE MP QA bots.

	Feeds named action values (aim, fire, lock, movement) into SCR_CharacterControllerComponent.OnPrepareControls
	without touching the OS mouse or keyboard. 100% inert unless -bgoneMpBot is present on the CLI.
	Auto-clears all held/pulsed inputs on stop or character death.
*/
class BGONE_BotInput
{
	//! Configurable BGONE missile lock action (bound in Configs/System/chimeraInputCommon.conf).
	static const string LOCK_ACTION = "BGONELock";

	protected static ref map<string, float> s_mHeld;
	protected static ref map<string, float> s_mPulse;
	protected static ref array<string> s_aContexts;
	protected static bool s_bArmChecked;
	protected static bool s_bArmed;

	//------------------------------------------------------------------------------------------------
	static bool IsArmed()
	{
		if (!s_bArmChecked)
		{
			s_bArmChecked = true;
			string cliVal;
			s_bArmed = System.GetCLIParam("bgoneMpBot", cliVal);
		}

		return s_bArmed;
	}

	//------------------------------------------------------------------------------------------------
	protected static void EnsureInit()
	{
		if (!s_mHeld)
			s_mHeld = new map<string, float>();

		if (!s_mPulse)
			s_mPulse = new map<string, float>();

		if (!s_aContexts)
			s_aContexts = {};
	}

	//------------------------------------------------------------------------------------------------
	static void Hold(string actionName, float val)
	{
		if (!IsArmed())
			return;

		EnsureInit();
		s_mHeld.Set(actionName, val);
	}

	//------------------------------------------------------------------------------------------------
	static void Release(string actionName)
	{
		if (!s_mHeld)
			return;

		s_mHeld.Remove(actionName);
	}

	//------------------------------------------------------------------------------------------------
	static void Pulse(string actionName, float val)
	{
		if (!IsArmed())
			return;

		EnsureInit();
		s_mPulse.Set(actionName, val);
	}

	//------------------------------------------------------------------------------------------------
	static void HoldContext(string contextName)
	{
		if (!IsArmed())
			return;

		EnsureInit();
		if (!s_aContexts.Contains(contextName))
			s_aContexts.Insert(contextName);
	}

	//------------------------------------------------------------------------------------------------
	static void ReleaseContext(string contextName)
	{
		if (!s_aContexts)
			return;

		int idx = s_aContexts.Find(contextName);
		if (idx >= 0)
			s_aContexts.Remove(idx);
	}

	//------------------------------------------------------------------------------------------------
	static float HeldValue(string actionName)
	{
		if (!s_mHeld)
			return 0;

		float val;
		if (s_mHeld.Find(actionName, val))
			return val;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	static void ReleaseAll()
	{
		if (s_mHeld)
			s_mHeld.Clear();

		if (s_mPulse)
			s_mPulse.Clear();

		if (s_aContexts)
			s_aContexts.Clear();
	}

	//------------------------------------------------------------------------------------------------
	static void EndRun()
	{
		ReleaseAll();
	}

	//------------------------------------------------------------------------------------------------
	//! Applies held/pulsed values for the local player's alive controlled character.
	static void Apply(IEntity owner, ActionManager am, bool isDead)
	{
		if (!am || !IsArmed())
			return;

		if (!s_mHeld && !s_mPulse && !s_aContexts)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc || pc.GetControlledEntity() != owner)
			return;

		if (isDead)
		{
			ReleaseAll();
			return;
		}

		if (s_aContexts)
		{
			foreach (string ctxName : s_aContexts)
			{
				am.ActivateContext(ctxName, 250);
			}
		}

		if (s_mHeld)
		{
			foreach (string heldName, float heldVal : s_mHeld)
			{
				am.SetActionValue(heldName, heldVal);
			}
		}

		if (s_mPulse)
		{
			foreach (string pulseName, float pulseVal : s_mPulse)
			{
				am.SetActionValue(pulseName, pulseVal);
			}

			s_mPulse.Clear();
		}
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_CharacterControllerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnPrepareControls(IEntity owner, ActionManager am, float dt, bool player)
	{
		if (player)
			BGONE_BotInput.Apply(owner, am, IsDead());

		super.OnPrepareControls(owner, am, dt, player);
	}
}
