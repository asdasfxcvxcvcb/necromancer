#pragma once

#include "../../../SDK/SDK.h"

// Vtable reconstructions of the game's achievement interfaces, matching the
// layout the game itself uses (same order Amalgam compiles against). The SDK
// only forward-declares IAchievementMgr for IVEngineClient013, so this header
// supplies the full definitions. Include after SDK.h.
class IAchievement
{
public:
	virtual int GetAchievementID() = 0;                                          // 0
	virtual const char* GetName() = 0;                                           // 1
	virtual int GetFlags() = 0;                                                  // 2
	virtual int GetGoal() = 0;                                                   // 3
	virtual int GetCount() = 0;                                                  // 4
	virtual bool IsAchieved() = 0;                                               // 5
	virtual int GetPointValue() = 0;                                             // 6
	virtual bool ShouldSaveWithGame() = 0;                                       // 7
	virtual bool ShouldHideUntilAchieved() = 0;                                  // 8
	virtual bool ShouldShowOnHUD() = 0;                                          // 9
	virtual void SetShowOnHUD(bool bShow) = 0;                                   // 10
};

class CBaseAchievement;

class IAchievementMgr
{
public:
	virtual IAchievement* GetAchievementByIndex(int iAchievement) = 0;           // 0
	virtual CBaseAchievement* GetAchievementByID(int iAchievementID) = 0;       // 1 (never called, kept for layout)
	virtual int GetAchievementCount() = 0;                                      // 2
	virtual void InitializeAchievements() = 0;                                  // 3
	virtual void AwardAchievement(int iAchievementID) = 0;                      // 4
	virtual void OnMapEvent(const char* pchEventName) = 0;                      // 5
	virtual void DownloadUserData() = 0;                                        // 6
	virtual void EnsureGlobalStateLoaded() = 0;                                 // 7
	virtual void SaveGlobalStateIfDirty(bool bAsync) = 0;                       // 8
	virtual bool HasAchieved(const char* pchName) = 0;                          // 9
	virtual bool WereCheatsEverOn() = 0;                                        // 10
};

namespace Achievements
{
	// Outcome of an unlock/lock attempt, surfaced in the menu so a silent
	// no-op (like a missing Steam interface) is visible instead of mysterious
	struct Result_t
	{
		bool bSuccess = false;
		int nCount = 0;
		const char* szReason = ""; // static storage, only read on failure
	};

	// Amalgam-style unlock/lock: awards flow through the game's own
	// CAchievementMgr so the in-game toast fires and the game's Steam
	// plumbing stays consistent, while the flat steam_api calls persist the
	// change immediately instead of waiting for the game's next save
	Result_t UnlockAll();
	Result_t LockAll();
}
