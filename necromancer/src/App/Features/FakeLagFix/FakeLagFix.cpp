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

	const int nEntIndex = pTarget->entindex();
	const float flCurrentSimTime = pTarget->m_flSimulationTime();
	
	// Check if simtime changed (player sent an update)
	const auto it = m_mLastSimTime.find(nEntIndex);
	const bool bSimTimeChanged = (it == m_mLastSimTime.end() || it->second != flCurrentSimTime);
	
	if (bSimTimeChanged)
	{
		// Simtime changed - they sent an update
		const int nChokedTicks = GetChokedTicks(pTarget);
		
		// Check if they were choking before (waited multiple ticks)
		const auto tickIt = m_mTicksSinceUpdate.find(nEntIndex);
		const int nTicksSinceUpdate = (tickIt != m_mTicksSinceUpdate.end()) ? tickIt->second : 0;
		
		// Update tracking
		m_mLastSimTime[nEntIndex] = flCurrentSimTime;
		m_mTicksSinceUpdate[nEntIndex] = 0;
		
		// Only shoot if they were actually choking (unchoke event)
		// nChokedTicks >= FAKELAG_CHOKE_THRESHOLD means they choked multiple ticks
		// OR if they weren't choking, allow shooting (normal case)
		return nChokedTicks >= FAKELAG_CHOKE_THRESHOLD || nTicksSinceUpdate < FAKELAG_CHOKE_THRESHOLD;
	}
	else
	{
		// Simtime didn't change - they're choking or idle
		// Increment ticks since last update
		m_mTicksSinceUpdate[nEntIndex]++;
		
		// Don't shoot while waiting for unchoke
		return false;
	}
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
