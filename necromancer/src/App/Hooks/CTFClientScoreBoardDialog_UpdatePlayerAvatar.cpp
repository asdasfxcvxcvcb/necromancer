#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/Players/Players.h"

// Signatures for scoreboard hooks
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerAvatar, "client.dll", "4D 85 C0 0F 84 ? ? ? ? 53 41 54 41 57", 0x0);
MAKE_SIGNATURE(SectionedListPanel_SetItemFgColor, "client.dll", "40 53 48 83 EC ? 48 8B D9 3B 91 ? ? ? ? 73 ? 3B 91 ? ? ? ? 7F ? 48 8B 89 ? ? ? ? 48 89 7C 24 ? 8B FA 48 03 FF 39 54 F9 ? 75 ? 39 54 F9 ? 75 ? 48 8B 0C F9 41 8B D0 48 8B 01 FF 90 ? ? ? ? 48 8B 83 ? ? ? ? B2 ? 48 8B 0C F8 48 8B 01 FF 90 ? ? ? ? 48 8B 83 ? ? ? ? 45 33 C0", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_UpdatePlayerList_SetItemFgColor_Call, "client.dll", "49 8B 04 24 8B D5 C7 44 24", 0x0);

// Store the current player index being processed
static int s_iPlayerIndex = 0;

// Get the color for a player based on their tag
static Color_t GetScoreboardColor(int iIndex)
{
	Color_t out = { 0, 0, 0, 0 };

	// Check if this is the local player
	if (iIndex == I::EngineClient->GetLocalPlayer())
	{
		out = CFG::Color_Local;
		return out;
	}

	// Check player tags
	PlayerPriority priority{};
	if (F::Players->GetInfo(iIndex, priority))
	{
		if (priority.Cheater)
			out = CFG::Color_Cheater;
		else if (priority.RetardLegit)
			out = CFG::Color_RetardLegit;
		else if (priority.Ignored)
			out = CFG::Color_Friend;
	}

	return out;
}

// Hook for CTFClientScoreBoardDialog::UpdatePlayerAvatar
// This stores the player index for use in the SetItemFgColor hook
MAKE_HOOK(CTFClientScoreBoardDialog_UpdatePlayerAvatar, Signatures::CTFClientScoreBoardDialog_UpdatePlayerAvatar.Get(), void, __fastcall,
	void* rcx, int playerIndex, void* kv)
{
	s_iPlayerIndex = playerIndex;
	return CALL_ORIGINAL(rcx, playerIndex, kv);
}

// Hook for SectionedListPanel::SetItemFgColor
// This changes the color of player names in the scoreboard
MAKE_HOOK(SectionedListPanel_SetItemFgColor, Signatures::SectionedListPanel_SetItemFgColor.Get(), void, __fastcall,
	void* rcx, int itemID, Color_t color)
{
	// Check if scoreboard colors are enabled
	if (!CFG::Visuals_Scoreboard_Utility)
		return CALL_ORIGINAL(rcx, itemID, color);

	// Check if this call is from the scoreboard update function
	static const auto dwDesired = Signatures::CTFClientScoreBoardDialog_UpdatePlayerList_SetItemFgColor_Call.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	if (dwDesired == dwRetAddr)
	{
		Color_t tColor = GetScoreboardColor(s_iPlayerIndex);
		if (tColor.a > 0)
		{
			color = tColor;
		}
	}

	CALL_ORIGINAL(rcx, itemID, color);
}
