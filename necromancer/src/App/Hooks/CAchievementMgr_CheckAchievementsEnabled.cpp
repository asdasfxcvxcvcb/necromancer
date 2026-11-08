#include "../../SDK/SDK.h"

// The game refuses to award achievements while it considers cheats in play
// (sv_cheats, -dev launch flag, ...). Amalgam bypasses this by always
// reporting achievements as enabled unless a demo is being played back - the
// one condition that should still block awarding. Without this hook,
// AwardAchievement calls from the Achievements feature silently do nothing.
MAKE_SIGNATURE(CAchievementMgr_CheckAchievementsEnabled, "client.dll", "40 53 48 83 EC ? 48 8B 05 ? ? ? ? 48 8B D9 48 8B 48 ? 48 85 C9 0F 84", 0x0);

MAKE_HOOK(CAchievementMgr_CheckAchievementsEnabled, Signatures::CAchievementMgr_CheckAchievementsEnabled.Get(), bool, __fastcall,
	void* ecx)
{
	return !I::EngineClient->IsPlayingDemo();
}
