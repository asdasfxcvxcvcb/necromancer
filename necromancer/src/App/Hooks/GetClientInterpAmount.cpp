#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/amalgam_port/AmalgamCompat.h"

// Signatures for net_graph display calls - we want to show real lerp there
MAKE_SIGNATURE(CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call1, "client.dll", "48 8B 05 ? ? ? ? 4C 8D 05 ? ? ? ? F3 44 0F 10 0D", 0x0);
MAKE_SIGNATURE(CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call2, "client.dll", "48 8B 05 ? ? ? ? F3 0F 10 48 ? F3 0F 5E C1", 0x0);

MAKE_HOOK(GetClientInterpAmount, Signatures::GetClientInterpAmount.Get(), float, __cdecl)
{
	// If auto interp is disabled, return original
	if (!CFG::Visuals_Auto_Interp)
		return CALL_ORIGINAL();

	// Allow net_graph to show real lerp value
	static const auto dwDesired1 = Signatures::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call1.Get();
	static const auto dwDesired2 = Signatures::CNetGraphPanel_DrawTextFields_GetClientInterpAmount_Call2.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	if (dwRetAddr == dwDesired1 || dwRetAddr == dwDesired2)
		return CALL_ORIGINAL();

	// Return 0 lerp for game calculations (better hit registration)
	return 0.f;
}
