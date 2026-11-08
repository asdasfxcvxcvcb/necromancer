#include "../../../SDK/SDK.h"
#include "Achievements.h"

// The game's CAchievementMgr is obtained through the engine
// (IVEngineClient013::GetAchievementMgr, vtable slot 114). Enumerate with
// GetAchievementCount/GetAchievementByIndex, award with AwardAchievement,
// lock with the Steam-side ClearAchievement by name - the same flow Amalgam
// uses. Steam persistence goes through the flat C API exported by the
// steam_api64.dll the 64-bit client loads (steam_api.dll on the 32-bit
// client), resolved at run time.

namespace
{
	// Steamworks flat API
	using FnGetUserStats = void* (*)();
	using FnGetHSteamUser = int (*)();
	using FnFindOrCreateUserInterface = void* (*)(int, const char*);
	using FnRequestCurrentStats = bool (*)(void*);
	using FnSetAchievement = bool (*)(void*, const char*);
	using FnClearAchievement = bool (*)(void*, const char*);
	using FnStoreStats = bool (*)(void*);

	HMODULE GetSteamApiModule()
	{
		if (const HMODULE hSteamApi = GetModuleHandleA("steam_api64.dll"))
			return hSteamApi;

		return GetModuleHandleA("steam_api.dll");
	}

	void* GetSteamUserStats(HMODULE hSteamApi)
	{
		if (!hSteamApi)
			return nullptr;

		// SteamAPI_SteamUserStats is the direct accessor; fall back to the
		// versioned interface lookup for older runtimes
		if (const auto fnAccessor = reinterpret_cast<FnGetUserStats>(GetProcAddress(hSteamApi, "SteamAPI_SteamUserStats")))
			return fnAccessor();

		const auto fnGetHSteamUser = reinterpret_cast<FnGetHSteamUser>(GetProcAddress(hSteamApi, "SteamAPI_GetHSteamUser"));
		const auto fnFindOrCreate = reinterpret_cast<FnFindOrCreateUserInterface>(GetProcAddress(hSteamApi, "SteamInternal_FindOrCreateUserInterface"));
		if (fnGetHSteamUser && fnFindOrCreate)
			return fnFindOrCreate(fnGetHSteamUser(), "STEAMUSERSTATS_INTERFACE_VERSION");

		return nullptr;
	}
}

namespace Achievements
{
	Result_t UnlockAll()
	{
		Result_t result;

		const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
		if (!pAchievementMgr)
		{
			result.szReason = "no achievement mgr";
			return result;
		}

		// Steam requires current stats to be loaded before they can change
		const HMODULE hSteamApi = GetSteamApiModule();
		void* pUserStats = GetSteamUserStats(hSteamApi);
		const auto fnRequest = pUserStats ? reinterpret_cast<FnRequestCurrentStats>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUserStats_RequestCurrentStats")) : nullptr;
		const auto fnStore = pUserStats ? reinterpret_cast<FnStoreStats>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUserStats_StoreStats")) : nullptr;

		if (fnRequest)
			fnRequest(pUserStats);

		// AwardAchievement also fires the in-game achievement toast; the
		// CheckAchievementsEnabled hook lets the awards through while injected
		int nAwarded = 0;
		const int nCount = pAchievementMgr->GetAchievementCount();
		for (int i = 0; i < nCount; i++)
		{
			if (IAchievement* pAchievement = pAchievementMgr->GetAchievementByIndex(i))
			{
				pAchievementMgr->AwardAchievement(pAchievement->GetAchievementID());
				nAwarded++;
			}
		}

		// Persist right away instead of waiting for the game's own global state save
		if (fnStore)
		{
			fnStore(pUserStats);
			if (fnRequest)
				fnRequest(pUserStats);
		}

		result.bSuccess = true;
		result.nCount = nAwarded;
		return result;
	}

	Result_t LockAll()
	{
		Result_t result;

		const auto pAchievementMgr = I::EngineClient->GetAchievementMgr();
		if (!pAchievementMgr)
		{
			result.szReason = "no achievement mgr";
			return result;
		}

		const HMODULE hSteamApi = GetSteamApiModule();
		void* pUserStats = GetSteamUserStats(hSteamApi);
		const auto fnRequest = pUserStats ? reinterpret_cast<FnRequestCurrentStats>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUserStats_RequestCurrentStats")) : nullptr;
		const auto fnClear = pUserStats ? reinterpret_cast<FnClearAchievement>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUserStats_ClearAchievement")) : nullptr;
		const auto fnStore = pUserStats ? reinterpret_cast<FnStoreStats>(GetProcAddress(hSteamApi, "SteamAPI_ISteamUserStats_StoreStats")) : nullptr;

		// There is no game-side "unaward", locking is purely a Steam-side clear
		if (!pUserStats || !fnClear || !fnStore)
		{
			result.szReason = "no steam stats api";
			return result;
		}

		if (fnRequest)
			fnRequest(pUserStats);

		int nCleared = 0;
		const int nCount = pAchievementMgr->GetAchievementCount();
		for (int i = 0; i < nCount; i++)
		{
			if (IAchievement* pAchievement = pAchievementMgr->GetAchievementByIndex(i))
			{
				if (fnClear(pUserStats, pAchievement->GetName()))
					nCleared++;
			}
		}

		fnStore(pUserStats);
		if (fnRequest)
			fnRequest(pUserStats);

		result.bSuccess = true;
		result.nCount = nCleared;
		return result;
	}
}
