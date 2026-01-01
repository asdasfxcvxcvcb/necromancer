#pragma once

#include "../../SDK/SDK.h"
#include "../Features/CFG.h"
#include "../Features/Players/Players.h"

#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <set>
#include <queue>
#include <condition_variable>
#include <atomic>

#pragma comment(lib, "winhttp.lib")

// Sourcebans data structure
struct SourcebanInfo_t
{
	bool m_bFetched = false;
	bool m_bFetching = false;
	bool m_bHasBans = false;
	bool m_bAlertDismissed = false;
	std::vector<std::string> m_vecBans;
};

// Verobay database info structure
struct VerobayInfo_t
{
	bool m_bFetched = false;
	bool m_bFetching = false;
	bool m_bFoundInDatabase = false;
	bool m_bAlertDismissed = false;
};

// Pending chat alerts to be processed on main thread
struct PendingBanAlert_t
{
	std::string playerName;
	int banCount;
	uint64_t steamID64;
	bool bVerobayFound;
};

// Initialize the worker thread (call once at startup)
void InitCheaterDatabase();

// Shutdown the worker thread (call at cleanup)
void ShutdownCheaterDatabase();

// Process pending ban alerts on main thread (call from Paint hook)
void ProcessPendingBanAlerts();

// Dismiss alert for a player
void DismissSourcebansAlert(uint64_t steamID64);

// Queue sourcebans fetch for multiple players (batch)
void FetchSourcebansBatch(const std::vector<uint64_t>& steamIDs);

// Single player fetch (for manual refresh)
void FetchSourcebans(uint64_t steamID64);

// Check all players in the server
void CheckAllPlayersSourcebans();

// Helper to check if a player has sourcebans alert (not dismissed)
bool HasSourcebansAlert(uint64_t steamID64);

// Get sourcebans info for a player (for UI display)
bool GetSourcebansInfo(uint64_t steamID64, SourcebanInfo_t& out);

// Clear sourcebans cache for a player (for refresh)
void ClearSourcebansCache(uint64_t steamID64);

// ==================== Verobay Database ====================

// Queue Verobay database fetch
void FetchVerobayDatabase();

// Check if a player is in the Verobay database
bool IsInVerobayDatabase(uint64_t steamID64);

// Get Verobay info for a player (for UI display)
bool GetVerobayInfo(uint64_t steamID64, VerobayInfo_t& out);

// Check if Verobay database has been loaded
bool IsVerobayDatabaseLoaded();

// Refresh Verobay database
void RefreshVerobayDatabase();

// Dismiss Verobay alert for a player
void DismissVerobayAlert(uint64_t steamID64);
