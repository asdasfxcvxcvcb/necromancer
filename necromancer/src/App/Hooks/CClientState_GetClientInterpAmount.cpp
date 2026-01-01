#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/amalgam_port/AmalgamCompat.h"

MAKE_SIGNATURE(CClientState_GetClientInterpAmount, "engine.dll", "48 83 EC ? 48 8B 0D ? ? ? ? 48 85 C9 75", 0x0);

MAKE_HOOK(CClientState_GetClientInterpAmount, Signatures::CClientState_GetClientInterpAmount.Get(), float, __fastcall,
	void* rcx)
{
	// Always store the real lerp value for backtrack calculations
	G::Lerp = CALL_ORIGINAL(rcx);
	
	// If auto interp is enabled, return 0 for game calculations
	if (CFG::Visuals_Auto_Interp)
		return 0.f;
	
	return G::Lerp;
}
