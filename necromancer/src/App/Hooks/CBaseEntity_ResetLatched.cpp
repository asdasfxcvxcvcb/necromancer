#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CBaseEntity_ResetLatched, "client.dll", "40 56 48 83 EC ? 48 8B 01 48 8B F1 FF 90 ? ? ? ? 84 C0 75", 0x0);

MAKE_HOOK(CBaseEntity_ResetLatched, Signatures::CBaseEntity_ResetLatched.Get(), void, __fastcall,
	void* ecx)
{
	// Block ResetLatched if Disable Interp is on
	if (CFG::Misc_Pred_Error_Jitter_Fix && CFG::Visuals_Disable_Interp)
	{
		return;
	}

	CALL_ORIGINAL(ecx);
}
