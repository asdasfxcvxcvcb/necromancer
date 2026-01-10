#pragma once
#include "../../Utils/Memory/Memory.h"
#include "../Steam/SteamTypes.h"

// Signatures for GC client system
MAKE_SIGNATURE(CGCClientSharedObjectCache_FindTypeCache, "client.dll", "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B7 59 ? BE", 0x0);
MAKE_SIGNATURE(CTFGCClientSystem_GetParty, "client.dll", "48 83 EC ? 48 8B 89 ? ? ? ? 48 85 C9 74 ? BA ? ? ? ? E8 ? ? ? ? 48 85 C0 74 ? 8B 48 ? 85 C9 74 ? 48 8B 40 ? FF C9", 0x0);
MAKE_SIGNATURE(CTFGCClientSystem_PingThink, "client.dll", "40 55 41 54 41 55 48 8D AC 24", 0x0);
MAKE_SIGNATURE(CTFGCClientSystem_UpdateAssignedLobby, "client.dll", "40 55 53 41 54 41 56 41 57 48 8B EC", 0x0);

// Lobby player proto - contains F2P status and party info
struct CTFLobbyPlayerProto
{
	enum TF_GC_TEAM
	{
		TF_GC_TEAM_DEFENDERS = 0,
		TF_GC_TEAM_INVADERS = 1,
		TF_GC_TEAM_BROADCASTER = 2,
		TF_GC_TEAM_SPECTATOR = 3,
		TF_GC_TEAM_PLAYER_POOL = 4,
		TF_GC_TEAM_NOTEAM = 5
	};

	enum ConnectState
	{
		INVALID = 0,
		RESERVATION_PENDING = 1,
		RESERVED = 2,
		CONNECTED = 3,
		CONNECTED_AD_HOC = 4,
		DISCONNECTED = 5
	};

	enum Type
	{
		INVALID_PLAYER = 0,
		MATCH_PLAYER = 1,
		STANDBY_PLAYER = 2,
		OBSERVING_PLAYER = 3
	};

	byte pad0[24];
	uint64_t id;                    // Steam ID
	TF_GC_TEAM team;
	ConnectState connect_state;
	const char* name;
	uint64_t original_party_id;     // Party ID - players with same ID are in same party
	uint32_t badge_level;
	uint32_t last_connect_time;
	Type type;
	bool squad_surplus;
	bool chat_suspension;           // TRUE = F2P account
	double normalized_rating;
	double normalized_uncertainty;
	uint32_t rank;
};

// Wrapper for lobby player details
class ConstTFLobbyPlayer
{
	void* pad0;
	void* pad1;

public:
	CTFLobbyPlayerProto* Proto()
	{
		using fn_t = CTFLobbyPlayerProto* (__thiscall*)(void*);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(this, 0))(this);
	}
};

// Shared object type cache
class CGCClientSharedObjectTypeCache
{
public:
	int GetCacheCount()
	{
		return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(this) + 40);
	}
};

// Shared object cache
class CGCClientSharedObjectCache
{
public:
	CGCClientSharedObjectTypeCache* FindTypeCache(int nClassID)
	{
		static auto fn = reinterpret_cast<CGCClientSharedObjectTypeCache* (__thiscall*)(void*, int)>(Signatures::CGCClientSharedObjectCache_FindTypeCache.Get());
		if (fn)
			return fn(this, nClassID);
		return nullptr;
	}
};

// TF2 Lobby shared object
class CTFLobbyShared
{
public:
	int GetNumMembers()
	{
		using fn_t = int(__thiscall*)(void*);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(this, 2))(this);
	}

	CSteamID* GetMember(CSteamID* pSteamID, int i)
	{
		using fn_t = CSteamID* (__thiscall*)(void*, CSteamID*, int);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(this, 3))(this, pSteamID, i);
	}

	int GetMemberIndexBySteamID(CSteamID& steamID)
	{
		using fn_t = int(__thiscall*)(void*, CSteamID&);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(this, 4))(this, steamID);
	}

	ConstTFLobbyPlayer* GetMemberDetails(ConstTFLobbyPlayer* pDetails, int i)
	{
		using fn_t = ConstTFLobbyPlayer* (__thiscall*)(void*, ConstTFLobbyPlayer*, int);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(this, 13))(this, pDetails, i);
	}
};

// TF2 Party
class CTFParty
{
public:
	int64_t GetNumMembers()
	{
		void* pOffset = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(this) + 184);
		using fn_t = int64_t(__thiscall*)(void*);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(pOffset, 2))(pOffset);
	}

	CSteamID* GetMember(CSteamID* pSteamID, int i)
	{
		void* pOffset = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(this) + 184);
		using fn_t = CSteamID* (__thiscall*)(void*, CSteamID*, int);
		return reinterpret_cast<fn_t>(Memory::GetVFunc(pOffset, 3))(pOffset, pSteamID, i);
	}
};

// TF2 GC Client System - main interface for lobby/party data
class CTFGCClientSystem
{
public:
	CTFParty* GetParty()
	{
		static auto fn = reinterpret_cast<CTFParty* (__thiscall*)(void*)>(Signatures::CTFGCClientSystem_GetParty.Get());
		if (fn)
			return fn(this);
		return nullptr;
	}

	void PingThink()
	{
		static auto fn = reinterpret_cast<void(__thiscall*)(void*)>(Signatures::CTFGCClientSystem_PingThink.Get());
		if (fn)
			fn(this);
	}

	CGCClientSharedObjectCache* GetSOCache()
	{
		return *reinterpret_cast<CGCClientSharedObjectCache**>(reinterpret_cast<uintptr_t>(this) + 1072);
	}

	CTFLobbyShared* GetLobby()
	{
		auto pSOCache = GetSOCache();
		if (!pSOCache)
			return nullptr;

		auto pTypeCache = pSOCache->FindTypeCache(2004);
		if (!pTypeCache)
			return nullptr;

		int iCacheCount = pTypeCache->GetCacheCount();
		if (!iCacheCount)
			return nullptr;

		auto pLobby = *reinterpret_cast<CTFLobbyShared**>(*reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(pTypeCache) + 8) + 8 * static_cast<uintptr_t>(iCacheCount - 1));
		if (!pLobby)
			return nullptr;

		return reinterpret_cast<CTFLobbyShared*>(reinterpret_cast<uintptr_t>(pLobby) - 8);
	}

	// Set pending ping refresh flag (forces ping data to be refreshed)
	void SetPendingPingRefresh(bool bValue)
	{
		*reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + 1116) = bValue;
	}

	// Set F2P/non-premium account status (false = premium, bypasses chat restriction)
	void SetNonPremiumAccount(bool bValue)
	{
		*reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(this) + 1888) = bValue;
	}
};

// Interface declaration - will be initialized via signature
MAKE_INTERFACE_SIGNATURE(CTFGCClientSystem, TFGCClientSystem, "client.dll", "48 8D 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24", 0x0, 0);
