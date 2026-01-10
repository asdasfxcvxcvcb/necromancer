#include "../../SDK/SDK.h"
#include "../Features/Crits/Crits.h"
#include "../Features/CFG.h"

// Static variable to store the seed from first-time prediction
static int s_iCurrentSeed = -1;

// Static variables for minigun/flamethrower ammo tracking
static int s_iLastAmmoCount = -1;

MAKE_HOOK(
	CTFWeaponBase_CalcIsAttackCritical, Signatures::CTFWeaponBase_CalcIsAttackCritical.Get(), void, __fastcall,
	void* rcx)
{
	auto pWeapon = reinterpret_cast<C_TFWeaponBase*>(rcx);
	if (!pWeapon)
	{
		CALL_ORIGINAL(rcx);
		return;
	}

	const auto nPreviousWeaponMode = pWeapon->m_iWeaponMode();
	pWeapon->m_iWeaponMode() = 0; // TF_WEAPON_PRIMARY_MODE
	
	if (I::Prediction->m_bFirstTimePredicted)
	{
		// Minigun/Flamethrower fix - only process if ammo actually decreased (bullet was fired)
		// This fixes issues where revving causes CalcIsAttackCritical to be called without actually firing
		int nWeaponID = pWeapon->GetWeaponID();
		if (nWeaponID == TF_WEAPON_MINIGUN || nWeaponID == TF_WEAPON_FLAMETHROWER)
		{
			auto pLocal = H::Entities->GetLocal();
			if (pLocal)
			{
				int iCurrentAmmo = pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType());
				bool bHasFiredBullet = (s_iLastAmmoCount != -1) && (iCurrentAmmo != s_iLastAmmoCount);
				s_iLastAmmoCount = iCurrentAmmo;
				
				if (!bHasFiredBullet)
				{
					pWeapon->m_iWeaponMode() = nPreviousWeaponMode;
					return;
				}
			}
		}
		
		// If we're forcing a crit/skip, inject our seed into the global random seed
		// The game's CalcIsAttackCritical will then use this seed
		if (F::CritHack->m_iForcedCommandNumber != 0)
		{
			// Set the global random seed to match what the SERVER will use
			// The server uses MD5_PseudoRandom(command_number) as the base
			int iForcedGlobalSeed = MD5_PseudoRandom(F::CritHack->m_iForcedCommandNumber) & std::numeric_limits<int>::max();
			*SDKUtils::RandomSeed() = iForcedGlobalSeed;
		}
		
		CALL_ORIGINAL(rcx);
		s_iCurrentSeed = pWeapon->m_iCurrentSeed();
	}
	else
	{
		// Re-prediction: save and restore values to fix buggy crit sounds
		float flOldCritTokenBucket = pWeapon->m_flCritTokenBucket();
		int nOldCritChecks = pWeapon->m_nCritChecks();
		int nOldCritSeedRequests = pWeapon->m_nCritSeedRequests();
		float flOldLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		float flOldCritTime = pWeapon->m_flCritTime();
		
		CALL_ORIGINAL(rcx);
		
		pWeapon->m_flCritTokenBucket() = flOldCritTokenBucket;
		pWeapon->m_nCritChecks() = nOldCritChecks;
		pWeapon->m_nCritSeedRequests() = nOldCritSeedRequests;
		pWeapon->m_flLastRapidFireCritCheckTime() = flOldLastRapidFireCritCheckTime;
		pWeapon->m_flCritTime() = flOldCritTime;
		pWeapon->m_iCurrentSeed() = s_iCurrentSeed;
	}
	
	pWeapon->m_iWeaponMode() = nPreviousWeaponMode;
}
