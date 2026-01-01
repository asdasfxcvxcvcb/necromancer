#pragma once
#include "../../../SDK/SDK.h"

// Simple FakeLag Fix - shoots on unchoke
// When enemy unchokes (sends update after choking), their position is accurate - shoot then

constexpr int FAKELAG_MAX_TICKS = 24;

class CFakeLagFix
{
public:
	// Returns true if we should shoot (target just unchoked or isn't choking)
	bool ShouldShoot(C_TFPlayer* pTarget);
	
	// Get how many ticks the player choked on their last update
	int GetChokedTicks(C_TFPlayer* pPlayer);
};

MAKE_SINGLETON_SCOPED(CFakeLagFix, FakeLagFix, F);
