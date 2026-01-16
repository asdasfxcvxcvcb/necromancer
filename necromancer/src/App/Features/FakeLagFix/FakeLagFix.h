#pragma once
#include "../../../SDK/SDK.h"
#include <unordered_map>

// Simple FakeLag Fix - shoots on unchoke
// When enemy unchokes (sends update after choking), their position is accurate - shoot then

constexpr int FAKELAG_MAX_TICKS = 24;
constexpr int FAKELAG_CHOKE_THRESHOLD = 2; // Consider choking if no update for 2+ ticks

class CFakeLagFix
{
public:
	// Returns true if we should shoot (target just unchoked or isn't choking)
	bool ShouldShoot(C_TFPlayer* pTarget);
	
	// Get how many ticks the player choked on their last update
	int GetChokedTicks(C_TFPlayer* pPlayer);

private:
	// Track last simtime for each player to detect unchoke events
	std::unordered_map<int, float> m_mLastSimTime;
	std::unordered_map<int, int> m_mTicksSinceUpdate;
};

MAKE_SINGLETON_SCOPED(CFakeLagFix, FakeLagFix, F);
