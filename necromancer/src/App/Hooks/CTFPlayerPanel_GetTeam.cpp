#include "../../SDK/SDK.h"

#include "../Features/CFG.h"

MAKE_SIGNATURE(CTFPlayerPanel_GetTeam, "client.dll", "8B 91 ? ? ? ? 83 FA ? 74 ? 48 8B 05", 0x0);
MAKE_SIGNATURE(CTFTeamStatusPlayerPanel_Update_GetTeam_Call, "client.dll", "8B 9F ? ? ? ? 40 32 F6", 0x0);
MAKE_SIGNATURE(CTFTeamStatus_OnTick, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B 01 48 8B F9 FF 90 ? ? ? ? 48 8B CF 0F B6 D8", 0x0);
MAKE_SIGNATURE(CVGui_RunFrame, "vgui2.dll", "48 8B C4 53 48 81 EC", 0x0);

static int s_iPlayerIndex = 0;
static void* s_pTeamStatus = nullptr;

MAKE_HOOK(CTFPlayerPanel_GetTeam, Signatures::CTFPlayerPanel_GetTeam.Get(), int, __fastcall,
	void* rcx)
{
	static const auto dwDesired = Signatures::CTFTeamStatusPlayerPanel_Update_GetTeam_Call.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	int iReturn = CALL_ORIGINAL(rcx);

	if (dwRetAddr == dwDesired)
	{
		s_iPlayerIndex = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(rcx) + 580);

		if (auto pLocal = H::Entities->GetLocal())
		{
			int iLocalTeam = pLocal->m_iTeamNum();

			if (CFG::Visuals_Reveal_Scoreboard && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot()))
				iReturn = iLocalTeam;

			// Force health bar visibility to update when team changes
			if (auto pHealthBar = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(rcx) + 688);
				pHealthBar && reinterpret_cast<bool(__fastcall*)(void*)>(Memory::GetVFunc(pHealthBar, 34))(pHealthBar) != (iReturn == iLocalTeam))
				*reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(rcx) + 624) = -1;
		}
	}

	return iReturn;
}

MAKE_HOOK(CTFTeamStatus_OnTick, Signatures::CTFTeamStatus_OnTick.Get(), void, __fastcall,
	void* rcx)
{
	s_pTeamStatus = rcx;

	CALL_ORIGINAL(rcx);
}

MAKE_HOOK(CVGui_RunFrame, Signatures::CVGui_RunFrame.Get(), void, __fastcall,
	void* rcx)
{
	if (!s_pTeamStatus)
		return CALL_ORIGINAL(rcx);

	static bool bStaticMod = false;
	const bool bLastMod = bStaticMod;
	const bool bCurrMod = bStaticMod = CFG::Visuals_Reveal_Scoreboard && !(CFG::Misc_Clean_Screenshot && I::EngineClient->IsTakingScreenshot());

	// Force team status panel update when the setting changes
	if (bCurrMod != bLastMod)
		Signatures::CTFTeamStatus_OnTick.Call<void>(s_pTeamStatus);

	CALL_ORIGINAL(rcx);
}
