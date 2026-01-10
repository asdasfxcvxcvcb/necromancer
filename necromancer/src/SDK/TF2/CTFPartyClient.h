#pragma once
#include "../../Utils/Memory/Memory.h"

MAKE_SIGNATURE(Get_TFPartyClient, "client.dll", "48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56", 0x0);
MAKE_SIGNATURE(CTFPartyClient_LoadSavedCasualCriteria, "client.dll", "48 83 79 ? ? C6 81 ? ? ? ? ? 74 ? 80 79 ? ? 74 ? C6 81 ? ? ? ? ? 48 8B 15", 0x0);
MAKE_SIGNATURE(CTFPartyClient_BInQueueForMatchGroup, "client.dll", "48 89 5C 24 ? 57 48 83 EC ? 48 8B F9 8B DA 8B CA E8 ? ? ? ? 84 C0", 0x0);
MAKE_SIGNATURE(CTFPartyClient_RequestQueueForMatch, "client.dll", "40 55 56 48 81 EC ? ? ? ? 48 63 F2", 0x0);

// Match group enum - must match Amalgam/TF2's values exactly
enum ETFMatchGroup
{
	k_eTFMatchGroup_Invalid = -1,
	k_eTFMatchGroup_MvM_Practice = 0,
	k_eTFMatchGroup_MvM_MannUp = 1,
	k_eTFMatchGroup_First = 0,
	k_eTFMatchGroup_MvM_Default = 0,
	k_eTFMatchGroup_MvM_First = 0,
	k_eTFMatchGroup_MvM_Last = 1,
	k_eTFMatchGroup_Ladder_6v6 = 2,
	k_eTFMatchGroup_Ladder_9v9 = 3,
	k_eTFMatchGroup_Ladder_12v12 = 4,
	k_eTFMatchGroup_Ladder_Default = 2,
	k_eTFMatchGroup_Ladder_First = 2,
	k_eTFMatchGroup_Ladder_Last = 4,
	k_eTFMatchGroup_Casual_6v6 = 5,
	k_eTFMatchGroup_Casual_9v9 = 6,
	k_eTFMatchGroup_Casual_12v12 = 7,
	k_eTFMatchGroup_Casual_Default = 7,  // This is the one used for auto casual queue
	k_eTFMatchGroup_Casual_First = 5,
	k_eTFMatchGroup_Casual_Last = 7,
	k_eTFMatchGroup_Event_Placeholder = 8,
	k_eTFMatchGroup_Event_Default = 8,
	k_eTFMatchGroup_Event_First = 8,
	k_eTFMatchGroup_Event_Last = 8
};

class CTFPartyClient
{
public:
	void LoadSavedCasualCriteria()
	{
		static auto fn = reinterpret_cast<void(__fastcall*)(void*)>(Signatures::CTFPartyClient_LoadSavedCasualCriteria.Get());
		if (fn)
			fn(this);
	}

	bool BInQueueForMatchGroup(ETFMatchGroup eMatchGroup)
	{
		static auto fn = reinterpret_cast<bool(__fastcall*)(void*, int)>(Signatures::CTFPartyClient_BInQueueForMatchGroup.Get());
		if (fn)
			return fn(this, eMatchGroup);
		return false;
	}

	void RequestQueueForMatch(ETFMatchGroup eMatchGroup)
	{
		static auto fn = reinterpret_cast<void(__fastcall*)(void*, int)>(Signatures::CTFPartyClient_RequestQueueForMatch.Get());
		if (fn)
			fn(this, eMatchGroup);
	}
};

namespace I { inline CTFPartyClient* TFPartyClient = nullptr; }
