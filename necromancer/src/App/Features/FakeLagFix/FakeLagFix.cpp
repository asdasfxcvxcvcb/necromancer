#include "FakeLagFix.h"
#include "../CFG.h"

bool CFakeLagFix::ShouldShoot(C_TFPlayer* pTarget)
{
	if (!CFG::Aimbot_Hitscan_FakeLagFix || !pTarget)
		return true;

	// Calculate how many ticks they choked
	const int nChokedTicks = GetChokedTicks(pTarget);

	// If they're not choking (1-2 ticks = normal), shoot
	// If they just unchoked (sent update with choked ticks), shoot - their position is now accurate
	// The key insight: when simtime updates, that's when we have their real position
	return nChokedTicks >= 1; // They just sent an update, shoot now
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
