#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CClientScoreBoardDialog_NeedsUpdate, "client.dll", "48 8B 05 ? ? ? ? F3 0F 10 41 ? 0F 2F 40 ? 0F 92 C0", 0x0);

MAKE_HOOK(CClientScoreBoardDialog_NeedsUpdate, Signatures::CClientScoreBoardDialog_NeedsUpdate.Get(), bool, __fastcall,
	void* rcx)
{
	static bool bStaticMod = false;
	const bool bLastMod = bStaticMod;
	const bool bCurrMod = bStaticMod = CFG::Visuals_Reveal_Scoreboard && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot());

	// Force scoreboard update when the setting changes
	if (bCurrMod != bLastMod)
		return true;

	return CALL_ORIGINAL(rcx);
}
