#include "../../SDK/SDK.h"

#include "../Features/Crits/Crits.h"
#include "../Features/CFG.h"

MAKE_SIGNATURE(CTFPlayerShared_IsCritBoosted, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 0F 29 7C 24", 0x0);
MAKE_SIGNATURE(CProxyModelGlowColor_OnBind_IsCritBoosted_Call, "client.dll", "48 8B CF 84 C0 74 ? BA", 0x0);

MAKE_HOOK(CTFPlayerShared_IsCritBoosted, Signatures::CTFPlayerShared_IsCritBoosted.Get(), bool, __fastcall,
	void* rcx)
{
	const auto dwRetAddr = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
	const auto dwDesired = Signatures::CProxyModelGlowColor_OnBind_IsCritBoosted_Call.Get();

	if (dwRetAddr != dwDesired)
		return CALL_ORIGINAL(rcx);

	const auto pLocal = H::Entities->GetLocal();
	if (!pLocal || pLocal->m_SharedPtr() != rcx || !F::CritHack->ShouldForceEffects(pLocal))
		return CALL_ORIGINAL(rcx);

	return true;
}
