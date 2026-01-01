#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

// CTFPlayerInventory::GetMaxItemCount - Returns the maximum number of items allowed in backpack
MAKE_SIGNATURE(CTFPlayerInventory_GetMaxItemCount, "client.dll", "40 53 48 83 EC ? 48 8B 89 ? ? ? ? BB", 0x0);

MAKE_HOOK(CTFPlayerInventory_GetMaxItemCount, Signatures::CTFPlayerInventory_GetMaxItemCount.Get(), int, __fastcall,
	void* rcx)
{
	// When backpack expander is enabled, return 4000 instead of the default limit
	if (CFG::Misc_Backpack_Expander)
		return 4000;
	
	return CALL_ORIGINAL(rcx);
}
