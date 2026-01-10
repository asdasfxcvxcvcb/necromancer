#include "FakeLagFix.h"
#include "../CFG.h"

bool CFakeLagFix::ShouldShoot(C_TFPlayer* pTarget)
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix || !pTarget)
		return true;

	// Skip FakeLagFix for rapid-fire weapons (SMG, pistol, minigun)
	// These weapons fire too fast for fakelag fix to be useful
	const auto pWeapon = H::Entities->GetWeapon();
	if (pWeapon)
	{
		const int nWeaponID = pWeapon->GetWeaponID();
		if (nWeaponID == TF_WEAPON_SMG ||
			nWeaponID == TF_WEAPON_PISTOL ||
			nWeaponID == TF_WEAPON_PISTOL_SCOUT ||
			nWeaponID == TF_WEAPON_MINIGUN)
		{
			return true; // Always allow shooting for these weapons
		}
	}

	// Calculate how many ticks they choked
	const int nChokedTicks = GetChokedTicks(pTarget);

	// nChokedTicks >= 1 means they just sent an update (simtime changed) - their position is fresh, shoot now
	// nChokedTicks == 0 means no simtime change this tick - they're either idle or we're waiting for update
	// For idle targets (standing still), simtime won't change, so we should still allow shooting
	// The key is: only block when we KNOW they're fakelagging (high choke count in previous frames)
	return nChokedTicks >= 1;
}

int CFakeLagFix::GetChokedTicks(C_TFPlayer* pPlayer)
{
	if (!pPlayer)
		return 0;

	const float flSimTime = pPlayer->m_flSimulationTime();
	const float flOldSimTime = pPlayer->m_flOldSimulationTime();
	const float flTickInterval = I::GlobalVars->interval_per_tick;

	// TIME_TO_TICKS equivalent
	const int nChokedTicks = static_cast<int>((flSimTime - flOldSimTime) / flTickInterval + 0.5f);

	return std::clamp(nChokedTicks, 0, FAKELAG_MAX_TICKS);
}
