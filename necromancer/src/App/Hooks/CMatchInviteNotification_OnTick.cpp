#include "../../SDK/SDK.h"
#include "../Features/CFG.h"

MAKE_SIGNATURE(CMatchInviteNotification_OnTick, "client.dll", "40 53 48 83 EC ? 48 8B D9 E8 ? ? ? ? F7 83", 0x0);

MAKE_HOOK(CMatchInviteNotification_OnTick, Signatures::CMatchInviteNotification_OnTick.Get(), void, __fastcall,
	void* rcx)
{
	if (CFG::Misc_Freeze_Queue)
		*reinterpret_cast<double*>(uintptr_t(rcx) + 616) = 0.0;

	CALL_ORIGINAL(rcx);
}
