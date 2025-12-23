#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(KeyValues_SetInt, "client.dll", "40 53 48 83 EC ? 41 8B D8 41 B0", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_SetInt_Call, "client.dll", "48 8B 05 ? ? ? ? 83 78 ? ? 0F 84 ? ? ? ? 48 8B 0D ? ? ? ? 8B D7", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_Jump, "client.dll", "8B E8 E8 ? ? ? ? 3B C7", 0x0);

MAKE_HOOK(KeyValues_SetInt, Signatures::KeyValues_SetInt.Get(), void, __fastcall,
	KeyValues* ecx, const char* keyName, int value)
{
	static const auto dwDesired = Signatures::CTFClientScoreBoardDialog_UpdatePlayerList_SetInt_Call.Get();
	static const auto dwJump = Signatures::CTFClientScoreBoardDialog_UpdatePlayerList_Jump.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	CALL_ORIGINAL(ecx, keyName, value);

	if (CFG::Visuals_Reveal_Scoreboard && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot()) && dwRetAddr == dwDesired && keyName && std::string_view(keyName).find("nemesis") != std::string_view::npos)
		*static_cast<uintptr_t*>(_AddressOfReturnAddress()) = dwJump;
}