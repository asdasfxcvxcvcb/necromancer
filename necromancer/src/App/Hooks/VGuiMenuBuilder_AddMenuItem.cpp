#include "../../SDK/SDK.h"

#include "../Features/CFG.h"
#include "../Features/Players/Players.h"

MAKE_SIGNATURE(CVoiceStatus_IsPlayerBlocked, "client.dll", "40 53 48 81 EC ? ? ? ? 48 8B D9 4C 8D 44 24", 0x0);
MAKE_SIGNATURE(VGuiMenuBuilder_AddMenuItem, "client.dll", "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8B EA 49 8B F9 48 8B 51 ? 49 8B F0 48 8B D9 48 85 D2 74 ? 49 8B C9 E8 ? ? ? ? 85 C0 74 ? 48 8B 0B 48 8B 01 FF 90 ? ? ? ? 48 8B 0B 4C 8B C6 4C 8B 4B ? 48 8B D5 48 89 7B ? 48 C7 44 24 ? ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 13 3B 82 ? ? ? ? 73 ? 3B 82 ? ? ? ? 7F ? 48 8B 92 ? ? ? ? 8B C8 48 03 C9 39 44 CA ? 75 ? 39 44 CA ? 75 ? 48 8B 04 CA EB ? 33 C0 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 83 C4 ? 5F C3 CC CC CC CC 48 89 5C 24", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_OnCommand, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 48 8B CB 48 8D 15 ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 48 8B 0D", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_IsPlayerBlocked_Call, "client.dll", "84 C0 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 48 0F 45 D1 4C 8D 0D", 0x0);
MAKE_SIGNATURE(CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_AddMenuItem_Call, "client.dll", "48 8B 0D ? ? ? ? 4C 8D 85 ? ? ? ? 48 8D 95 ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 44 8B 85", 0x0);

static int s_iPlayerIndex = -1;
static std::string s_sPlayerName = "";

MAKE_HOOK(CVoiceStatus_IsPlayerBlocked, Signatures::CVoiceStatus_IsPlayerBlocked.Get(), bool, __fastcall,
	void* rcx, int playerIndex)
{
	static const auto dwDesired = Signatures::CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_IsPlayerBlocked_Call.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	if (CFG::Visuals_Scoreboard_Utility && dwRetAddr == dwDesired)
		s_iPlayerIndex = playerIndex;

	return CALL_ORIGINAL(rcx, playerIndex);
}

MAKE_HOOK(VGuiMenuBuilder_AddMenuItem, Signatures::VGuiMenuBuilder_AddMenuItem.Get(), void*, __fastcall,
	void* rcx, const char* pszButtonText, const char* pszCommand, const char* pszCategoryName)
{
	static const auto dwDesired = Signatures::CTFClientScoreBoardDialog_OnScoreBoardMouseRightRelease_AddMenuItem_Call.Get();
	const auto dwRetAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

	if (CFG::Visuals_Scoreboard_Utility && dwRetAddr == dwDesired && s_iPlayerIndex != -1)
	{
		auto pReturn = CALL_ORIGINAL(rcx, pszButtonText, pszCommand, pszCategoryName);

		player_info_t playerInfo{};
		if (I::EngineClient->GetPlayerInfo(s_iPlayerIndex, &playerInfo) && !playerInfo.fakeplayer)
		{
			s_sPlayerName = playerInfo.name;

			// Add custom menu items
			CALL_ORIGINAL(rcx, "Show SteamID", "copysteamid", "profile");

			// Get current player tags
			PlayerPriority info{};
			F::Players->GetInfo(s_iPlayerIndex, info);

			// Add tag management
			CALL_ORIGINAL(rcx, std::format("Tags for {}", s_sPlayerName).c_str(), "listtags", "tags");
			CALL_ORIGINAL(rcx, info.Ignored ? "Remove Ignored" : "Add Ignored", "tag_ignored", "tags");
			CALL_ORIGINAL(rcx, info.Cheater ? "Remove Cheater" : "Add Cheater", "tag_cheater", "tags");
			CALL_ORIGINAL(rcx, info.RetardLegit ? "Remove Retard Legit" : "Add Retard Legit", "tag_retardlegit", "tags");
		}

		return pReturn;
	}

	return CALL_ORIGINAL(rcx, pszButtonText, pszCommand, pszCategoryName);
}

MAKE_HOOK(CTFClientScoreBoardDialog_OnCommand, Signatures::CTFClientScoreBoardDialog_OnCommand.Get(), void, __fastcall,
	void* rcx, const char* command)
{
	if (!CFG::Visuals_Scoreboard_Utility || !command)
		return CALL_ORIGINAL(rcx, command);

	// Handle custom commands
	if (strcmp(command, "copysteamid") == 0)
	{
		// Get player info
		player_info_t playerInfo{};
		if (I::EngineClient->GetPlayerInfo(s_iPlayerIndex, &playerInfo))
		{
			// Show SteamID in chat
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("SteamID: {}", playerInfo.guid).c_str());
		}
		return;
	}
	else if (strcmp(command, "listtags") == 0)
	{
		// Show current tags in chat
		PlayerPriority info{};
		if (F::Players->GetInfo(s_iPlayerIndex, info))
		{
			std::string tags = "Tags: ";
			if (info.Ignored) tags += "[Ignored] ";
			if (info.Cheater) tags += "[Cheater] ";
			if (info.RetardLegit) tags += "[Retard Legit] ";
			if (!info.Ignored && !info.Cheater && !info.RetardLegit) tags += "None";
			I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{} - {}", s_sPlayerName, tags).c_str());
		}
		return;
	}
	else if (strcmp(command, "tag_ignored") == 0)
	{
		PlayerPriority info{};
		F::Players->GetInfo(s_iPlayerIndex, info);
		info.Ignored = !info.Ignored;
		F::Players->Mark(s_iPlayerIndex, info);
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{} {} Ignored tag", s_sPlayerName, info.Ignored ? "added to" : "removed from").c_str());
		return;
	}
	else if (strcmp(command, "tag_cheater") == 0)
	{
		PlayerPriority info{};
		F::Players->GetInfo(s_iPlayerIndex, info);
		info.Cheater = !info.Cheater;
		F::Players->Mark(s_iPlayerIndex, info);
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{} {} Cheater tag", s_sPlayerName, info.Cheater ? "added to" : "removed from").c_str());
		return;
	}
	else if (strcmp(command, "tag_retardlegit") == 0)
	{
		PlayerPriority info{};
		F::Players->GetInfo(s_iPlayerIndex, info);
		info.RetardLegit = !info.RetardLegit;
		F::Players->Mark(s_iPlayerIndex, info);
		I::ClientModeShared->m_pChatElement->ChatPrintf(0, std::format("{} {} Retard Legit tag", s_sPlayerName, info.RetardLegit ? "added to" : "removed from").c_str());
		return;
	}

	CALL_ORIGINAL(rcx, command);
}
