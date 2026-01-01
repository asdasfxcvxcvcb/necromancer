#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CTFPlayerShared_IsPlayerDominated, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 63 F2 48 8B D9 E8", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_IsPlayerDominated_Call, "client.dll", "84 C0 74 ? 45 84 FF 74", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_Jump, "client.dll", "8B E8 E8 ? ? ? ? 3B C7", 0x0);

MAKE_HOOK(CTFPlayerShared_IsPlayerDominated, Signatures::CTFPlayerShared_IsPlayerDominated.Get(), bool, __fastcall,
	void* ecx, int index)
{
	static const auto dwDesired = Signatures::CTFClientScoreBoardDialog_UpdatePlayerList_IsPlayerDominated_Call.Get();
	static const auto dwJump = Signatures::CTFClientScoreBoardDialog_UpdatePlayerList_Jump.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	const bool bResult = CALL_ORIGINAL(ecx, index);

	if (CFG::Visuals_Reveal_Scoreboard && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot()) && dwRetAddr == dwDesired && !bResult)
		*static_cast<uintptr_t*>(_AddressOfReturnAddress()) = dwJump;

	return bResult;
}
