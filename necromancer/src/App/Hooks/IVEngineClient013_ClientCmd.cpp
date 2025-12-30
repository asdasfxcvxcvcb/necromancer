#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/FakeAngle/FakeAngle.h"

// Taunt delay state
namespace TauntState
{
	bool bDisabledForTaunt = false;
	int nTauntDelayTicks = 0;
	bool bWasLegitAAEnabled = false;
	bool bWasAntiAimEnabled = false;
	std::string sPendingTauntCmd = "";
	bool bTauntExecuted = false;  // Track if taunt was executed, wait for it to end
	int nWaitForTauntStartTicks = 0;  // Wait a few ticks after executing for taunt to actually start
}

// Called from CreateMove every tick to handle taunt delay
void ProcessTauntDelay()
{
	if (!TauntState::bDisabledForTaunt)
		return;

	auto pLocal = H::Entities->GetLocal();
	if (!pLocal)
		return;

	// If taunt was executed, wait for it to end before re-enabling AA
	if (TauntState::bTauntExecuted)
	{
		// Wait a few ticks for the taunt to actually start
		if (TauntState::nWaitForTauntStartTicks > 0)
		{
			TauntState::nWaitForTauntStartTicks--;
			return;
		}
		
		// Check if taunt ended (or never started)
		if (!pLocal->InCond(TF_COND_TAUNTING))
		{
			// Taunt ended, re-enable AA
			CFG::Exploits_LegitAA_Enabled = TauntState::bWasLegitAAEnabled;
			CFG::Exploits_AntiAim_Enabled = TauntState::bWasAntiAimEnabled;
			TauntState::bDisabledForTaunt = false;
			TauntState::bTauntExecuted = false;
		}
		return;
	}

	// Waiting for delay ticks before executing taunt
	if (TauntState::nTauntDelayTicks > 0)
	{
		TauntState::nTauntDelayTicks--;
		return;
	}

	// Delay complete, execute the taunt
	if (!TauntState::sPendingTauntCmd.empty())
	{
		I::EngineClient->ClientCmd_Unrestricted(TauntState::sPendingTauntCmd.c_str());
		TauntState::sPendingTauntCmd = "";
		TauntState::bTauntExecuted = true;  // Now wait for taunt to end
		TauntState::nWaitForTauntStartTicks = 10;  // Wait 10 ticks for taunt to actually start
	}
}

MAKE_HOOK(IVEngineClient013_ClientCmd, Memory::GetVFunc(I::EngineClient, 7), void, __fastcall,
	IVEngineClient013* ecx, const char* szCmdString)
{
	auto runFakeTaunt = [&]()
	{
		if (!CFG::Misc_Fake_Taunt)
			return false;

		static constexpr auto Hash0 = HASH_CT("taunt 0");
		static constexpr auto Hash1 = HASH_CT("taunt 1");
		static constexpr auto Hash2 = HASH_CT("taunt 2");
		static constexpr auto Hash3 = HASH_CT("taunt 3");
		static constexpr auto Hash4 = HASH_CT("taunt 4");
		static constexpr auto Hash5 = HASH_CT("taunt 5");
		static constexpr auto Hash6 = HASH_CT("taunt 6");
		static constexpr auto Hash7 = HASH_CT("taunt 7");
		static constexpr auto Hash8 = HASH_CT("taunt 8");
		static constexpr auto Hash9 = HASH_CT("taunt");

		if ((HASH_RT(szCmdString) == Hash1
				|| HASH_RT(szCmdString) == Hash2
				|| HASH_RT(szCmdString) == Hash3
				|| HASH_RT(szCmdString) == Hash4
				|| HASH_RT(szCmdString) == Hash5
				|| HASH_RT(szCmdString) == Hash6
				|| HASH_RT(szCmdString) == Hash7
				|| HASH_RT(szCmdString) == Hash8
				|| HASH_RT(szCmdString) == Hash9)
			&& G::bStartedFakeTaunt)
			return true;

		if (HASH_RT(szCmdString) != Hash0 && HASH_RT(szCmdString) != Hash9)
			return false;

		if (G::bStartedFakeTaunt)
			return true;

		const auto pLocal = H::Entities->GetLocal();

		if (!pLocal)
			return false;

		const auto pWeapon = H::Entities->GetWeapon();

		if (!pWeapon)
			return false;

		const auto pAnimState = pLocal->GetAnimState();

		if (!pAnimState)
			return false;

		auto& gestureSlotCustom = pAnimState->m_aGestureSlots[GESTURE_SLOT_VCD];

		const auto pAnimLayer = gestureSlotCustom.m_pAnimLayer;

		if (!pAnimLayer)
			return false;

		auto getSequenceName = [&](std::string& strOut) -> bool
		{
			const int nWeaponId = pWeapon->GetWeaponID();
			const int nClass = pLocal->m_iClass();
			const int nItemIdx = pWeapon->m_iItemDefinitionIndex();

			if (nWeaponId == TF_WEAPON_LUNCHBOX && nClass == TF_CLASS_SCOUT)
				return false;

			if (nWeaponId == TF_WEAPON_FIREAXE && nClass == TF_CLASS_HEAVYWEAPONS)
				return false;

			if (nWeaponId == TF_WEAPON_JAR && nClass == TF_CLASS_SNIPER)
				return false;

			if (nWeaponId == TF_WEAPON_BUILDER && nClass == TF_CLASS_SPY)
				return false;

			strOut = std::string("taunt0" + std::to_string(pWeapon->GetSlot() + 1));

			if (nWeaponId == TF_WEAPON_LUNCHBOX && nClass == TF_CLASS_HEAVYWEAPONS)
				strOut = "taunt04";

			if (nWeaponId == TF_WEAPON_PARTICLE_CANNON && nClass == TF_CLASS_SOLDIER)
				strOut = "taunt08";

			if (nWeaponId == TF_WEAPON_SHOTGUN_SOLDIER && nClass == TF_CLASS_SOLDIER)
				strOut = "taunt04";

			if (nWeaponId == TF_WEAPON_BUFF_ITEM && nClass == TF_CLASS_SOLDIER)
				strOut = "taunt02";

			if (nWeaponId == TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT && nClass == TF_CLASS_SOLDIER)
				strOut = "taunt07";

			if (nWeaponId == TF_WEAPON_ROCKETPACK && nClass == TF_CLASS_PYRO)
				strOut = "taunt_gasblast";

			if (nWeaponId == TF_WEAPON_SLAP && nClass == TF_CLASS_PYRO)
				strOut = "taunt02";

			if ((nWeaponId == TF_WEAPON_GRENADELAUNCHER || nWeaponId == TF_WEAPON_CANNON) && nClass == TF_CLASS_DEMOMAN)
				strOut = "taunt02";

			if (nWeaponId == TF_WEAPON_STICKBOMB && nClass == TF_CLASS_DEMOMAN)
				strOut = "taunt04";

			if (nWeaponId == TF_WEAPON_SENTRY_REVENGE && nClass == TF_CLASS_ENGINEER)
				strOut = "taunt07";

			if (nWeaponId == TF_WEAPON_COMPOUND_BOW && nClass == TF_CLASS_SNIPER)
				strOut = "taunt04";

			if (nWeaponId == TF_WEAPON_BAT_WOOD && nClass == TF_CLASS_SCOUT)
				strOut = "taunt05";

			if (nWeaponId == TF_WEAPON_SHOVEL && nClass == TF_CLASS_SOLDIER)
			{
				if (nItemIdx == Soldier_t_TheEqualizer || nItemIdx == Soldier_t_TheEscapePlan)
					strOut = "taunt05";
			}

			if (nWeaponId == TF_WEAPON_BONESAW && nClass == TF_CLASS_MEDIC)
			{
				if (nItemIdx == Medic_t_TheUbersaw)
					strOut = "taunt08";
			}

			if (nWeaponId == TF_WEAPON_MEDIGUN && nClass == TF_CLASS_MEDIC)
			{
				if (nItemIdx == Medic_s_TheKritzkrieg)
					strOut = "taunt06";
			}

			if (nWeaponId == TF_WEAPON_WRENCH && nClass == TF_CLASS_ENGINEER)
			{
				if (nItemIdx == Engi_t_TheGunslinger)
					strOut = "taunt09";
			}

			if (nWeaponId == TF_WEAPON_SWORD && nClass == TF_CLASS_DEMOMAN)
			{
				if (nItemIdx == Demoman_t_TheScotsmansSkullcutter)
					strOut = "taunt02";

				else strOut = "taunt09";
			}

			if (nWeaponId == TF_WEAPON_BOTTLE && nClass == TF_CLASS_DEMOMAN)
			{
				if (nItemIdx == Demoman_t_ThePainTrain)
					strOut = "taunt02";
			}

			if (nWeaponId == TF_WEAPON_PIPEBOMBLAUNCHER && nClass == TF_CLASS_DEMOMAN)
			{
				if (nItemIdx == Demoman_s_TheScottishResistance)
					strOut = "taunt08";

				else strOut = "taunt01";
			}

			if (nWeaponId == TF_WEAPON_FISTS && nClass == TF_CLASS_HEAVYWEAPONS)
			{
				if (nItemIdx == Heavy_t_FestiveGlovesofRunningUrgently
					|| nItemIdx == Heavy_t_FistsofSteel
					|| nItemIdx == Heavy_t_GlovesofRunningUrgently
					|| nItemIdx == Heavy_t_TheKillingGlovesofBoxing
					|| nItemIdx == Heavy_t_TheBreadBite
					|| nItemIdx == Heavy_t_TheEvictionNotice
					|| nItemIdx == Heavy_t_WarriorsSpirit)
					strOut = "taunt05";
			}

			if (nWeaponId == TF_WEAPON_FLAREGUN && nClass == TF_CLASS_PYRO)
			{
				if (nItemIdx == Pyro_s_TheScorchShot)
					strOut = "taunt_scorch_shot";
			}

			return true;
		};

		std::string sSequence = {};

		if (!getSequenceName(sSequence))
			return false;

		gestureSlotCustom.m_bActive = true;
		gestureSlotCustom.m_bAutoKill = true;
		gestureSlotCustom.m_iGestureSlot = GESTURE_SLOT_VCD;
		gestureSlotCustom.m_iActivity = ACT_MP_VCD;

		pAnimLayer->m_nSequence = SDKUtils::LookupSequence(pLocal->GetModelPtr(), sSequence.c_str());
		pAnimLayer->m_flPrevCycle = 0.0f;
		pAnimLayer->m_flWeight = 1.0f;
		pAnimLayer->m_nOrder = 5;
		pAnimLayer->m_flPlaybackRate = 1.0f;
		pAnimLayer->m_flCycle = 0.0f;
		pAnimLayer->m_flLayerAnimtime = 0.0f;
		pAnimLayer->m_flLayerFadeOuttime = 0.0f;
		pAnimLayer->m_flBlendIn = 0.0f;
		pAnimLayer->m_flBlendOut = 0.0f;
		pAnimLayer->m_bClientBlend = false;

		G::bStartedFakeTaunt = true;
		G::flFakeTauntStartYaw = G::vUserCmdAngles.y;

		pLocal->m_nForceTauntCam() = 1;

		return true;
	};

	if (runFakeTaunt())
		return;

	// Check if this is a taunt command and AA is enabled - if so, delay it
	{
		static constexpr auto HashTaunt = HASH_CT("taunt");
		static constexpr auto HashTaunt0 = HASH_CT("taunt 0");
		static constexpr auto HashTaunt1 = HASH_CT("taunt 1");
		static constexpr auto HashTaunt2 = HASH_CT("taunt 2");
		static constexpr auto HashTaunt3 = HASH_CT("taunt 3");
		static constexpr auto HashTaunt4 = HASH_CT("taunt 4");
		static constexpr auto HashTaunt5 = HASH_CT("taunt 5");
		static constexpr auto HashTaunt6 = HASH_CT("taunt 6");
		static constexpr auto HashTaunt7 = HASH_CT("taunt 7");
		static constexpr auto HashTaunt8 = HASH_CT("taunt 8");
		static constexpr auto HashPlusTaunt = HASH_CT("+taunt");

		const auto cmdHash = HASH_RT(szCmdString);
		const bool bIsTauntCmd = (cmdHash == HashTaunt || cmdHash == HashTaunt0 || cmdHash == HashTaunt1 ||
			cmdHash == HashTaunt2 || cmdHash == HashTaunt3 || cmdHash == HashTaunt4 ||
			cmdHash == HashTaunt5 || cmdHash == HashTaunt6 || cmdHash == HashTaunt7 ||
			cmdHash == HashTaunt8 || cmdHash == HashPlusTaunt);

		if (bIsTauntCmd)
		{
			// If AA is enabled and we're not already processing a taunt, block and delay
			if ((CFG::Exploits_AntiAim_Enabled || CFG::Exploits_LegitAA_Enabled) && !TauntState::bDisabledForTaunt)
			{
		
				
				// Store the taunt command to execute later
				TauntState::sPendingTauntCmd = szCmdString;
				
				// Save and disable AA
				TauntState::bWasLegitAAEnabled = CFG::Exploits_LegitAA_Enabled;
				TauntState::bWasAntiAimEnabled = CFG::Exploits_AntiAim_Enabled;
				CFG::Exploits_LegitAA_Enabled = false;
				CFG::Exploits_AntiAim_Enabled = false;
				
				// Set state and delay
				TauntState::bDisabledForTaunt = true;
				TauntState::nTauntDelayTicks = 10;  // Wait 44 ticks before executing taunt
				TauntState::bTauntExecuted = false;
				
				return;  // Don't execute the taunt yet
			}
		}
	}

	CALL_ORIGINAL(ecx, szCmdString);
}
